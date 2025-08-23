#include "ServerState.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "utils/IO.h"

// Global variable definitions
MySQL g_mysql;
std::map<int, FileTransferState> fd_transfer_states;
std::mutex transfer_states_mutex;
std::unordered_map<std::string, time_t> last_activity;
std::unordered_map<int, std::string> fd_to_uid;
std::mutex activity_mutex;

// A buffer for sending messages, declared here as it's used by sendMsg
extern std::map<int, SendBuffer> sendBuffers;

void updateUserActivity(const std::string& uid) {
    std::lock_guard<std::mutex> lock(activity_mutex);
    last_activity[uid] = time(nullptr);
}

void addFdToUid(int fd, const std::string& uid) {
    std::lock_guard<std::mutex> lock(activity_mutex);
    fd_to_uid[fd] = uid;
    last_activity[uid] = time(nullptr);
    std::cout << "[心跳]  fd=" << fd << " -> uid=" << uid << std::endl;
}

void removeFdFromUid(int fd) {
    std::lock_guard<std::mutex> lock(activity_mutex);
    if (fd_to_uid.find(fd) != fd_to_uid.end()) {
        fd_to_uid.erase(fd);
    }
}

std::string getUidByFd(int fd) {
    std::lock_guard<std::mutex> lock(activity_mutex);
    auto it = fd_to_uid.find(fd);
    return (it != fd_to_uid.end()) ? it->second : "";
}

void removeUserActivity(const std::string& uid) {
    std::lock_guard<std::mutex> lock(activity_mutex);
    last_activity.erase(uid);
    for (auto it = fd_to_uid.begin(); it != fd_to_uid.end();) {
        if (it->second == uid) {
            it = fd_to_uid.erase(it);
        } else {
            ++it;
        }
    }
}

void removeFdMapping(int fd) {
    std::lock_guard<std::mutex> lock(activity_mutex);
    auto it = fd_to_uid.find(fd);
    if (it != fd_to_uid.end()) {
        fd_to_uid.erase(it);
    }
}

void checkHeartbeatTimeout() {
    std::lock_guard<std::mutex> lock(activity_mutex);
    time_t now = time(nullptr);
    Redis redis;
    redis.connect();

    for (auto it = last_activity.begin(); it != last_activity.end();) {
        if (now - it->second > 30) {  // 30秒超时
            std::cout << "[心跳超时] 用户 " << it->first << " 超时，清理状态" << std::endl;

            // 清理用户状态
            redis.hdel("is_online", it->first);
            redis.hdel("unified_receiver", it->first);

            it = last_activity.erase(it);
        } else {
            ++it;
        }
    }
}

// The sendMsg function needs to be defined here as it's a shared utility
void sendMsg(int epfd, int fd, std::string msg) {
    if (fd < 0 || msg.empty()) {
        return;
    }

    SendBuffer& buf = sendBuffers[fd];
    bool was_empty = buf.data.empty();

    int len_net = htonl((int)msg.size());
    buf.data.insert(buf.data.end(), (char*)&len_net, (char*)&len_net + 4);
    buf.data.insert(buf.data.end(), msg.begin(), msg.end());

    if (was_empty) {
        while (buf.sent_bytes < buf.data.size()) {
            ssize_t n = send(fd, buf.data.data() + buf.sent_bytes, buf.data.size() - buf.sent_bytes, 0);
            if (n > 0) {
                buf.sent_bytes += n;
            } else {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    struct epoll_event temp;
                    temp.data.fd = fd;
                    temp.events = EPOLLIN | EPOLLET | EPOLLOUT;
                    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &temp);
                    return;
                }
                // Other error, handled in handleCloseEvent
                return;
            }
        }

        if (buf.sent_bytes == buf.data.size()) {
            buf.data.clear();
            buf.sent_bytes = 0;
            struct epoll_event temp;
            temp.data.fd = fd;
            temp.events = EPOLLIN | EPOLLET;
            epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &temp);
        }
    }
}

