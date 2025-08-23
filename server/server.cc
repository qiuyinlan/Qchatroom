#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>
#include <csignal>
#include <thread>
#include <unordered_set>
#include <mutex>
#include "Group.h"
#include <random>
#include <unordered_map>
#include <ctime>
#include <curl/curl.h>
#include <set>
#include <map>
#include "nlohmann/json.hpp"

#include "../utils/TCP.h"
#include "../utils/IO.h"
#include "ThreadPool.hpp"
#include "proto.h"
#include "Redis.h"
#include "User.h"
#include "MySQL.h"

#include "IO.h"
#include "Group.h"
#include "group_chat.h"
#include "FileTransferState.h"
#include "handlers/GroupHandlers.h"
#include "handlers/FriendHandlers.h"
#include "handlers/UserHandlers.h"

using namespace std;

extern int recvMsg(int fd, std::string &msg); // 来自 IO.cc
#include "ServerState.h"


void handleReadEvent(int epfd, int fd, ThreadPool& pool);
void handleWriteEvent(int epfd, int fd);
void handleCloseEvent(int epfd, int fd);
void handleMessage(int epfd, int fd, const std::string& json_msg);





#include "handlers/ChatHandlers.h"
#include "handlers/FileHandlers.h"




void signalHandler(int signum) {

        // 清理Redis中的在线状态
        Redis redis;
        if (redis.connect()) {
            cout << "[服务器] 清理Redis中的在线状态,安全退出" << endl;
            redis.del("is_online");
            redis.del("is_chat");
        } else {
            cout << "[服务器] Redis连接失败，无法清理" << endl;
        }
        exit(signum);

}



int main(int argc, char *argv[]) {
    if (argc == 1) {
        IP = "10.30.0.146";
        PORT = 8000;
    } else if (argc == 3) {
        IP = argv[1];
        PORT = stoi(argv[2]);
    } else {
        // 错误情况
        cerr << "Invalid number of arguments. Usage: program_name [IP] [port]" << endl;
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);        // 忽略SIGPIPE信号，避免客户端断开导致服务器退出
    signal(SIGINT, signalHandler);   // 处理Ctrl+C

    // Initialize the global MySQL connection
    if (!g_mysql.connect()) {
        cerr << "[FATAL] Failed to connect to MySQL at startup." << endl;
        return 1;
    }



    //服务器启动时删除所有在线用户
    Redis redis;
    if (redis.connect()) {
        cout << "服务器启动，清理残留的在线状态..." << endl;
        redis.del("is_online");
        redis.del("is_chat");
    }

    int ret;
    int num = 0;
    char str[INET_ADDRSTRLEN];
    string msg;

    ThreadPool pool(16);
    int listenfd = Socket();
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void *) &opt, sizeof(opt));
    Bind(listenfd, IP, PORT);
    Listen(listenfd);

     //非阻塞
    int flag = fcntl(listenfd, F_GETFL, 0);
    if (flag == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    flag |= O_NONBLOCK;
    if (fcntl(listenfd, F_SETFL, flag) == -1) {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }

    int epfd = epoll_create(1024);

    struct epoll_event temp, ep[1024];


    temp.data.fd = listenfd;
    temp.events = EPOLLIN | EPOLLET ;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &temp);


    // 记录上次检查心跳的时间
    time_t last_heartbeat_check = time(nullptr);

    while (true) {
        // 使用超时的epoll_wait，每5秒检查一次心跳
        ret = epoll_wait(epfd, ep, 1024, 5000);  // 5秒超时

        // 检查心跳
        time_t now = time(nullptr);
        if (now - last_heartbeat_check >= 10) {  // 每10秒检查一次
            checkHeartbeatTimeout();
            last_heartbeat_check = now;
        }

        //超时，无事件
        if (ret == 0) {
            continue;
        }
        for (int i = 0; i < ret; i++) {
            int fd = ep[i].data.fd;
            //新连接
            if (fd == listenfd) {
                while (true) {
                    struct sockaddr_in cli_addr;
                    memset(&cli_addr, 0, sizeof(cli_addr));
                    socklen_t cli_len = sizeof(cli_addr);

                    int connfd = Accept(listenfd, (struct sockaddr *) &cli_addr, &cli_len);
                    if (connfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        } else if (errno == EINTR) {
                            continue;

                        } else {
                            perror("accept");
                            break;
                        }
                    }

                    cout << "New connection from " << inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, str, sizeof(str))
                         << " at port " << ntohs(cli_addr.sin_port) << " (fd: " << connfd << ")" << endl;

                    // Set non-blocking
                    int flag = fcntl(connfd, F_GETFL);
                    flag |= O_NONBLOCK;
                    fcntl(connfd, F_SETFL, flag);

                    // Add to epoll


                    temp.events = EPOLLIN | EPOLLET;

                    temp.data.fd = connfd;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &temp) < 0) {
                        sys_err("epoll_ctl error for connfd");
                    }
                }
            } else {

                if (ep[i].events & (EPOLLHUP | EPOLLERR)) {

                    handleCloseEvent(epfd, fd);
                } else {
                    if (ep[i].events & EPOLLIN) {
                        handleReadEvent(epfd, fd, pool);
                    }
                    if (ep[i].events & EPOLLOUT) {
                        handleWriteEvent(epfd, fd);
                    }
                }
            }
        }
    }
}



void handleReadEvent(int epfd, int fd, ThreadPool& pool) {
    bool is_file_transfer = false;
    {
        lock_guard<mutex> lock(transfer_states_mutex);
        is_file_transfer = fd_transfer_states.count(fd) &&
                           fd_transfer_states.at(fd).status == TransferStatus::RECEIVING;
    }

    if (is_file_transfer) {
        char buffer[4096];
        ssize_t bytes_read = recv(fd, buffer, sizeof(buffer), 0);
        if (bytes_read > 0) {
            handleFileData(epfd, fd, buffer, bytes_read);
        } else if (bytes_read == 0) {
            handleCloseEvent(epfd, fd);
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                handleCloseEvent(epfd, fd);
            }
        }
    } else {
        string msg;
        int recv_ret = recvMsg(fd, msg);
        if (recv_ret > 0) {
            pool.addTask([epfd, fd, msg]() { handleMessage(epfd, fd, msg); });
        } else if (recv_ret == 0) {
            handleCloseEvent(epfd, fd);
        } else if (recv_ret < 0 && recv_ret != -2) {
            handleCloseEvent(epfd, fd);
        }
    }
}

void handleWriteEvent(int epfd, int fd) {
    bool is_sending_file = false;
    {
        lock_guard<mutex> lock(transfer_states_mutex);
        is_sending_file = fd_transfer_states.count(fd) &&
                          fd_transfer_states.at(fd).status == TransferStatus::SENDING;
    }

    if (is_sending_file) {
        handleFileWriteEvent(epfd, fd);
    } else {
        SendBuffer& buf = sendBuffers[fd];
        while (buf.sent_bytes < buf.data.size()) {
            ssize_t n = send(fd, buf.data.data() + buf.sent_bytes, buf.data.size() - buf.sent_bytes, 0);
            if (n > 0) {
                buf.sent_bytes += n;
            } else {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    return; // Wait for next EPOLLOUT
                }
                handleCloseEvent(epfd, fd);
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

void handleCloseEvent(int epfd, int fd) {
    cout << "[INFO] Client disconnected: " << fd << endl;
    cleanupFileTransfer(fd);

    Redis redis;
    if (redis.connect()) {
        string uid = getUidByFd(fd);
        if (!uid.empty()) {
            redis.hdel("is_online", uid);
            redis.hdel("unified_receiver", uid);
            removeUserActivity(uid);
        }
    }

    removeFdMapping(fd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

void handleMessage(int epfd, int fd, const std::string& json_msg) {
    try {
        nlohmann::json msg = nlohmann::json::parse(json_msg);
        int flag = msg.value("flag", 0);

        switch (flag) {
            // User Handlers
            case C2S_LOGIN_REQUEST: handleLoginRequest(epfd, fd, msg); break;
            case C2S_LOGOUT_REQUEST: handleLogoutRequest(epfd, fd, msg); break;
            case C2S_DEACTIVATE_ACCOUNT_REQUEST: handleDeactivateAccountRequest(epfd, fd, msg); break;
            case C2S_HEARTBEAT: handleHeartbeat(epfd, fd, msg); break;

            // Friend
            case C2S_ADD_FRIEND_REQUEST: handleAddFriendRequest(epfd, fd, msg); break;
            case C2S_GET_FRIEND_REQUESTS: handleGetFriendRequests(epfd, fd, msg); break;
            case C2S_RESPOND_TO_FRIEND_REQUEST: handleRespondToFriendRequest(epfd, fd, msg); break;
            case C2S_DELETE_FRIEND_REQUEST: handleDeleteFriendRequest(epfd, fd, msg); break;
            case C2S_BLOCK_FRIEND_REQUEST: handleBlockFriendRequest(epfd, fd, msg); break;
            case C2S_GET_BLOCKED_LIST_REQUEST: handleGetBlockedListRequest(epfd, fd, msg); break;
            case C2S_UNBLOCK_FRIEND_REQUEST: handleUnblockFriendRequest(epfd, fd, msg); break;

            // Group
            case C2S_CREATE_GROUP_REQUEST: handleCreateGroupRequest(epfd, fd, msg); break;
            case C2S_JOIN_GROUP_REQUEST: handleJoinGroupRequest(epfd, fd, msg); break;
            case C2S_GET_MANAGED_GROUPS_REQUEST: handleGetManagedGroupsRequest(epfd, fd, msg); break;
            case C2S_GET_GROUP_MEMBERS_REQUEST: handleGetGroupMembersRequest(epfd, fd, msg); break;
            case C2S_KICK_GROUP_MEMBER_REQUEST: handleKickGroupMemberRequest(epfd, fd, msg); break;
            case C2S_APPOINT_ADMIN_REQUEST: handleAppointAdminRequest(epfd, fd, msg); break;
            case C2S_GET_GROUP_ADMINS_REQUEST: handleGetGroupAdminsRequest(epfd, fd, msg); break;
            case C2S_REVOKE_ADMIN_REQUEST: handleRevokeAdminRequest(epfd, fd, msg); break;
            case C2S_DELETE_GROUP_REQUEST: handleDeleteGroupRequest(epfd, fd, msg); break;
            case C2S_GET_JOINED_GROUPS_REQUEST: handleGetJoinedGroupsRequest(epfd, fd, msg); break;
            case C2S_QUIT_GROUP_REQUEST: handleQuitGroupRequest(epfd, fd, msg); break;
            case C2S_INVITE_TO_GROUP_REQUEST: handleInviteToGroupRequest(epfd, fd, msg); break;
            case C2S_START_GROUP_CHAT_REQUEST: handleStartGroupChatRequest(epfd, fd, msg); break;

            // Chat
            case C2S_GET_CHAT_LISTS: handleGetChatLists(epfd, fd, msg); break;
            case C2S_START_CHAT_REQUEST: handleStartChatRequest(epfd, fd, msg); break;
            case C2S_PRIVATE_MESSAGE: handlePrivateMessage(epfd, fd, msg); break;
            case C2S_GROUP_MESSAGE: handleGroupMessage(epfd, fd, msg); break;
            case C2S_EXIT_CHAT_REQUEST: handleExitChatRequest(epfd, fd, msg); break;
            case C2S_GET_HISTORY_REQUEST: handleGetHistoryRequest(epfd, fd, msg); break;

            // File
            case C2S_SEND_FILE_REQUEST: handleSendFileRequest(epfd, fd, msg); break;
            case C2S_RECV_FILE_REQUEST: handleRecvFileRequest(epfd, fd, msg); break;

            default:{
                cout << "[WARNING] Unknown message flag received: " << flag << " from fd=" << fd << endl;
                break;
            }
        }
    } catch (const nlohmann::json::parse_error& e) {
        cerr << "[ERROR] 来自fd=" << fd << " 的JSON解析错误: " << e.what() << "\n消息内容: " << json_msg << endl;
    }
}
























