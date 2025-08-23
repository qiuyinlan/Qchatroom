#include "FileHandlers.h"
#include "../ServerState.h"
#include "../proto.h"
#include "../Redis.h"
#include "../User.h"
#include "../Message.h"
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <filesystem>

using namespace std;

// Helper function from the old server.cc, needed for notifications
string getUsernameFromRedis(const string& uid) {
    Redis redis;
    if (redis.connect()) {
        string user_info_str = redis.hget("user_info", uid);
        if (!user_info_str.empty()) {
            try {
                nlohmann::json user_json = nlohmann::json::parse(user_info_str);
                return user_json.value("username", "");
            } catch (const nlohmann::json::parse_error& e) {
                return "";
            }
        }
    }
    return "";
}

string getGroupNameFromRedis(const string& group_uid) {
    Redis redis;
    if (redis.connect()) {
        string group_info_str = redis.hget("group_info", group_uid);
        if (!group_info_str.empty()) {
            try {
                nlohmann::json group_json = nlohmann::json::parse(group_info_str);
                return group_json.value("groupName", "");
            } catch (const nlohmann::json::parse_error& e) {
                return "";
            }
        }
    }
    return "";
}

void handleFileData(int epfd, int fd, const char* data, int len) {
    lock_guard<mutex> lock(transfer_states_mutex);
    if (fd_transfer_states.count(fd) == 0) return;

    FileTransferState& state = fd_transfer_states.at(fd);
    if (state.status != TransferStatus::RECEIVING) return;

    state.file_stream.write(data, len);
    state.transferred_bytes += len;

    if (state.transferred_bytes >= state.file_size) {
        cout << "[文件] fd=" << fd << " 文件接收完成: " << state.file_name << endl;
        state.file_stream.close();

        Redis redis;
        redis.connect();

        Message fileMessage;
        fileMessage.setUsername(getUsernameFromRedis(state.sender_uid));
        fileMessage.setUidFrom(state.sender_uid);
        fileMessage.setContent("[文件]" + state.file_name);

        if (state.chat_type == "private") {
            fileMessage.setUidTo(state.receiver_uid);
            g_mysql.insertPrivateMessage(state.sender_uid, state.receiver_uid, fileMessage.getContent());
            redis.sadd("recv" + state.receiver_uid, state.file_path);

            if (redis.hexists("is_online", state.receiver_uid)) {
                int receiver_fd = stoi(redis.hget("is_online", state.receiver_uid));
                nlohmann::json notification;
                notification["flag"] = S2C_FILE_NOTIFICATION;
                notification["data"]["sender_username"] = fileMessage.getUsername();
                sendMsg(epfd, receiver_fd, notification.dump());
            }
        } else { // group
            fileMessage.setUidTo(state.group_uid);
            g_mysql.insertGroupMessage(state.group_uid, state.sender_uid, fileMessage.getContent());

            redisReply *replies = redis.smembers("group_members:" + state.group_uid);
            if (replies) {
                for (size_t i = 0; i < replies->elements; ++i) {
                    if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
                    string member_uid = replies->element[i]->str;
                    if (member_uid != state.sender_uid) {
                        redis.sadd("recv_group:" + state.group_uid + ":" + member_uid, state.file_path);
                        if (redis.hexists("is_online", member_uid)) {
                            int member_fd = stoi(redis.hget("is_online", member_uid));
                            nlohmann::json notification;
                            notification["flag"] = S2C_FILE_NOTIFICATION;
                            notification["data"]["group_name"] = getGroupNameFromRedis(state.group_uid);
                            sendMsg(epfd, member_fd, notification.dump());
                        }
                    }
                }
                freeReplyObject(replies);
            }
        }
        fd_transfer_states.erase(fd);
    }
}

void handleFileWriteEvent(int epfd, int fd) {
    lock_guard<mutex> lock(transfer_states_mutex);
    if (fd_transfer_states.count(fd) == 0) return;

    FileTransferState& state = fd_transfer_states.at(fd);
    if (state.status != TransferStatus::SENDING) return;

    char buffer[4096];
    state.read_stream.read(buffer, sizeof(buffer));
    streamsize bytes_read = state.read_stream.gcount();

    if (bytes_read > 0) {
        ssize_t n = send(fd, buffer, bytes_read, 0);
        if (n > 0) {
            state.transferred_bytes += n;
        } else {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                state.read_stream.seekg(-bytes_read, ios_base::cur);
                return;
            } else {
                cerr << "[文件] fd=" << fd << " send错误, errno=" << errno << endl;
                cleanupFileTransfer(fd);
                return;
            }
        }
    }

    if (state.transferred_bytes >= state.file_size || state.read_stream.eof()) {
        cout << "[文件] fd=" << fd << " 文件发送完成: " << state.file_path << endl;
        cleanupFileTransfer(fd);
        remove(state.file_path.c_str());

        struct epoll_event temp;
        temp.data.fd = fd;
        temp.events = EPOLLIN | EPOLLET;
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &temp);
    }
}

void cleanupFileTransfer(int fd) {
    lock_guard<mutex> lock(transfer_states_mutex);
    if (fd_transfer_states.count(fd)) {
        FileTransferState& state = fd_transfer_states.at(fd);
        if (state.file_stream.is_open()) {
            state.file_stream.close();
            remove(state.file_path.c_str());
            cout << "[文件] fd=" << fd << " 连接关闭, 删除部分文件: " << state.file_path << endl;
        }
        if (state.read_stream.is_open()) {
            state.read_stream.close();
        }
        fd_transfer_states.erase(fd);
    }
}
#include "../utils/proto.h"
#include "../utils/IO.h"
#include "../Redis.h"
#include "../FileTransferState.h"
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <filesystem>

using namespace std;
using json = nlohmann::json;

#include "../ServerState.h"

void handleSendFileRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的发送文件请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_SEND_FILE_RESPONSE;
    string sender_uid = getUidByFd(fd);

    if (sender_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户未登录或会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    auto data = msg["data"];
    string target_uid = data.value("target_uid", "");
    string chat_type = data.value("chat_type", "");
    string file_name = data.value("file_name", "");
    long long file_size = data.value("file_size", 0LL);

    if (target_uid.empty() || chat_type.empty() || file_name.empty() || file_size == 0) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求参数";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Redis redis;
    if (!redis.connect()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "服务器内部错误 (Redis)";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    lock_guard<mutex> lock(transfer_states_mutex);
    FileTransferState& state = fd_transfer_states[fd];

    size_t dotPos = file_name.rfind('.');
    string name = file_name.substr(0, dotPos);
    string ext = (dotPos != string::npos) ? file_name.substr(dotPos) : "";
    state.file_name = name + "_" + to_string(time(nullptr)) + ext;
    state.file_path = "./fileBuffer_send/" + state.file_name;

    if (!filesystem::exists("./fileBuffer_send")) {
        filesystem::create_directories("./fileBuffer_send");
    }

    state.file_stream.open(state.file_path, ios::binary | ios::trunc);
    if (!state.file_stream.is_open()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "服务器无法创建文件";
        sendMsg(epfd, fd, response.dump());
        fd_transfer_states.erase(fd);
        return;
    }

    state.status = TransferStatus::RECEIVING;
    state.file_size = file_size;
    state.transferred_bytes = 0;
    state.sender_uid = sender_uid;
    state.receiver_uid = target_uid;
    state.chat_type = chat_type;
    if (chat_type == "group") {
        state.group_uid = target_uid;
    }

    cout << "[文件] fd=" << fd << " 准备接收文件: " << state.file_name << ", 大小: " << state.file_size << endl;

    response["data"]["success"] = true;
    response["data"]["reason"] = "服务器准备就绪，请开始发送文件数据";
    sendMsg(epfd, fd, response.dump());
}

void handleRecvFileRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的接收文件请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_RECV_FILE_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户未登录或会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    auto data = msg["data"];
    string file_path = data.value("file_path", "");

    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "服务器上的文件已丢失";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    lock_guard<mutex> lock(transfer_states_mutex);
    FileTransferState& state = fd_transfer_states[fd];
    state.read_stream.open(file_path, ios::binary);

    if (!state.read_stream.is_open()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "服务器无法打开文件";
        sendMsg(epfd, fd, response.dump());
        fd_transfer_states.erase(fd);
        return;
    }

    state.status = TransferStatus::SENDING;
    state.file_path = file_path;
    state.file_size = file_stat.st_size;
    state.transferred_bytes = 0;

    response["data"]["success"] = true;
    response["data"]["file_name"] = filesystem::path(file_path).filename().string();
    response["data"]["file_size"] = state.file_size;
    response["data"]["reason"] = "准备就绪，开始接收文件";
    sendMsg(epfd, fd, response.dump());

    struct epoll_event temp;
    temp.data.fd = fd;
    temp.events = EPOLLIN | EPOLLOUT | EPOLLET;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &temp);
}

