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
#include "Transaction.h"
#include "LoginHandler.h"
#include "IO.h"
#include "Group.h"
#include "group_chat.h"
#include "FileTransferState.h"

using namespace std;

extern int recvMsg(int fd, std::string &msg); // 来自 IO.cc
void sendMsg(int epfd, int fd, std::string msg); // 新的非阻塞发送函数

// 🚀 New Event-Driven Function Declarations
void handleReadEvent(int epfd, int fd, ThreadPool& pool);
void handleWriteEvent(int epfd, int fd);
void handleCloseEvent(int epfd, int fd);
void handleMessage(int epfd, int fd, const std::string& json_msg);
void handleFileData(int epfd, int fd, const char* data, int len);

// 💼 Business Logic Handlers (run in thread pool)
void handleLoginRequest(int epfd, int fd, const nlohmann::json& msg);
void handleHeartbeat(int epfd, int fd, const nlohmann::json& msg);
void handleGetChatLists(int epfd, int fd, const nlohmann::json& msg);
void handleLogoutRequest(int epfd, int fd, const nlohmann::json& msg);
void handleDeactivateRequest(int epfd, int fd, const nlohmann::json& msg);
void handleStartChatRequest(int epfd, int fd, const nlohmann::json& msg);
void handleStartGroupChatRequest(int epfd, int fd, const nlohmann::json& msg);
void handlePrivateMessage(int epfd, int fd, const nlohmann::json& msg);
void handleGroupMessage(int epfd, int fd, const nlohmann::json& msg);
void handleExitChatRequest(int epfd, int fd, const nlohmann::json& msg);
void handleAddFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetFriendRequests(int epfd, int fd, const nlohmann::json& msg);
void handleRespondToFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleDeleteFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleBlockFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetBlockedListRequest(int epfd, int fd, const nlohmann::json& msg);
void handleUnblockFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleDeactivateAccountRequest(int epfd, int fd, const nlohmann::json& msg);
void handleCreateGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleJoinGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetManagedGroupsRequest(int epfd, int fd, const nlohmann::json& msg);

void handleGetGroupMembersRequest(int epfd, int fd, const nlohmann::json& msg);
void handleKickGroupMemberRequest(int epfd, int fd, const nlohmann::json& msg);
void handleAppointAdminRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetGroupAdminsRequest(int epfd, int fd, const nlohmann::json& msg);
void handleRevokeAdminRequest(int epfd, int fd, const nlohmann::json& msg);
void handleDeleteGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetJoinedGroupsRequest(int epfd, int fd, const nlohmann::json& msg);
void handleQuitGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleInviteToGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetHistoryRequest(int epfd, int fd, const nlohmann::json& msg);


// 📤 File Transfer Handlers
void handleSendFileRequest(int epfd, int fd, const nlohmann::json& msg);
void handleRecvFileRequest(int epfd, int fd, const nlohmann::json& msg);


// ET，记录等待JSON数据的fd
unordered_set<int> waiting_for_json;
mutex waiting_mutex;

// 心跳，因为fd值会被复用，所以一定要引入uid,用户唯一标识
unordered_map<string, time_t> last_activity;  // 记最后活跃
unordered_map<int, string> fd_to_uid;         // fd到uid的映射
mutex activity_mutex;

// 📤 File Transfer State Management
std::map<int, FileTransferState> fd_transfer_states; // Tracks the state of file transfers per fd
std::mutex transfer_states_mutex; // Mutex to protect the transfer states map

// Global MySQL connection object
MySQL g_mysql;



// 更新uid活跃时间
void updateUserActivity(const string& uid) {
    lock_guard<mutex> lock(activity_mutex);
    last_activity[uid] = time(nullptr);
}

// 添加fd到uid的映射，写成单独函数，可以更好地控制锁
void addFdToUid(int fd, const string& uid) {
    lock_guard<mutex> lock(activity_mutex);
    fd_to_uid[fd] = uid;
    last_activity[uid] = time(nullptr); // unix时间戳
    cout << "[心跳]  fd=" << fd << " -> uid=" << uid << endl;
}

// 从映射中移除fd
void removeFdFromUid(int fd) {
    lock_guard<mutex> lock(activity_mutex);
    if (fd_to_uid.find(fd) != fd_to_uid.end()) {
        fd_to_uid.erase(fd);
    }
}

// fd--uid
string getUidByFd(int fd) {
    lock_guard<mutex> lock(activity_mutex);
    auto it = fd_to_uid.find(fd);
    return (it != fd_to_uid.end()) ? it->second : "";
}

// 检查心跳超时
void checkHeartbeatTimeout() {
    lock_guard<mutex> lock(activity_mutex);
    time_t now = time(nullptr);
    Redis redis;
    redis.connect();

    for (auto it = last_activity.begin(); it != last_activity.end();) {
        if (now - it->second > 30) {  // 30秒超时
            cout << "[心跳超时] 用户 " << it->first << " 超时，清理状态" << endl;

            // 清理用户状态
            redis.hdel("is_online", it->first);
            redis.hdel("unified_receiver", it->first);

            it = last_activity.erase(it);
        } else {
            ++it;
        }
    }
}

// 移除用户活跃记录
void removeUserActivity(const string& uid) {
    lock_guard<mutex> lock(activity_mutex);
    last_activity.erase(uid);

    // 同时移除fd映射
    for (auto it = fd_to_uid.begin(); it != fd_to_uid.end();) {
        if (it->second == uid) {
            cout << "[心跳] 移除映射 fd=" << it->first << " -> uid=" << uid << endl;
            it = fd_to_uid.erase(it);
        } else {
            ++it;
        }
    }
    cout << "[心跳] 移除用户 " << uid << " 活跃记录" << endl;
}

// 根据fd移除映射
void removeFdMapping(int fd) {
    lock_guard<mutex> lock(activity_mutex);
    auto it = fd_to_uid.find(fd);
    if (it != fd_to_uid.end()) {
        cout << "[心跳] 移除映射 fd=" << fd << " -> uid=" << it->second << endl;
        fd_to_uid.erase(it);
    }
}

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

// 旧的心跳函数已删除，心跳功能现在集成在统一接收连接中


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
    bool is_receiving_file = false;
    {
        lock_guard<mutex> lock(transfer_states_mutex);
        if (fd_transfer_states.count(fd) && fd_transfer_states[fd].status == TransferStatus::RECEIVING) {
            is_receiving_file = true;
        }
    }

    if (is_receiving_file) {
        // Handle raw file data
        char buffer[4096];
        while (true) {
            ssize_t bytes_read = recv(fd, buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                handleFileData(epfd, fd, buffer, bytes_read);
            } else if (bytes_read == 0) {
                cout << "[文件] fd=" << fd << " 在文件传输中关闭了连接." << endl;
                handleCloseEvent(epfd, fd);
                break;
            } else { // bytes_read < 0
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    perror("recv");
                    handleCloseEvent(epfd, fd);
                    break;
                }
            }
        }
    } else {
        // Handle JSON messages
        string msg;
        while (true) {
            int recv_ret = recvMsg(fd, msg);
            if (recv_ret > 0) {
                cout << "[DEBUG] 从fd=" << fd << " 接收到完整消息" << msg << endl;
                pool.addTask([epfd, fd, msg]() {
                    handleMessage(epfd, fd, msg);
                });
                msg.clear();
            } else if (recv_ret == 0) {
                cout << "[INFO] recvMsg返回0, 客户端正常断开. fd=" << fd << endl;
                handleCloseEvent(epfd, fd);
                break;
            } else if (recv_ret == -2) {
                break;
            } else {
                cerr << "[ERROR] fd=" << fd << " 上的recvMsg发生错误, ret=" << recv_ret << endl;
                handleCloseEvent(epfd, fd);
                break;
            }
        }
    }
}

void handleWriteEvent(int epfd, int fd) {
    bool is_sending_file = false;
    {
        lock_guard<mutex> lock(transfer_states_mutex);
        if (fd_transfer_states.count(fd) && fd_transfer_states[fd].status == TransferStatus::SENDING) {
            is_sending_file = true;
        }
    }

    if (is_sending_file) {
        lock_guard<mutex> lock(transfer_states_mutex);
        if (fd_transfer_states.count(fd) == 0) return; // State might have been cleared

        FileTransferState& state = fd_transfer_states[fd];
        char buffer[4096];
        state.read_stream.read(buffer, sizeof(buffer));
        streamsize bytes_read = state.read_stream.gcount();

        if (bytes_read > 0) {
            ssize_t n = send(fd, buffer, bytes_read, 0);
            if (n > 0) {
                state.transferred_bytes += n;
            } else {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Kernel buffer full, seek back and wait for next EPOLLOUT
                    state.read_stream.seekg(-bytes_read, ios_base::cur);
                    return;


                } else {
                    cerr << "[文件] fd=" << fd << " send错误, errno=" << errno << endl;
                    handleCloseEvent(epfd, fd);
                    return;
                }
            }
        }

        if (state.transferred_bytes >= state.file_size) {
            cout << "[文件] fd=" << fd << " 文件发送完成: " << state.file_path << endl;
            state.read_stream.close();

            // Cleanup
            Redis redis;
            if (redis.connect()) {
                string my_uid = getUidByFd(fd);
                // This is complex, need to find the original queue key
                // For now, we assume the client will remove it. A better approach is needed.
            }

            remove(state.file_path.c_str());
            fd_transfer_states.erase(fd);

            struct epoll_event temp;
            temp.data.fd = fd;
            temp.events = EPOLLIN | EPOLLET;
            epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &temp);
        }
    } else {
        // Original logic for JSON messages
        cout << "[DEBUG] fd=" << fd << " 的写事件触发 (JSON)" << endl;
        SendBuffer& buf = sendBuffers[fd];

        while (buf.sent_bytes < buf.data.size()) {
            ssize_t n = send(fd, buf.data.data() + buf.sent_bytes, buf.data.size() - buf.sent_bytes, 0);
            if (n > 0) {
                buf.sent_bytes += n;
            } else {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    return;


                }
                cerr << "[ERROR] fd=" << fd << " 上发生send错误, errno=" << errno << endl;
                handleCloseEvent(epfd, fd);
                return;
            }
        }

        if (buf.sent_bytes == buf.data.size()) {
            cout << "[DEBUG] fd=" << fd << " 的缓冲数据发送完毕" << endl;
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

    // Cleanup file transfer state if any
    {
        lock_guard<mutex> lock(transfer_states_mutex);
        if (fd_transfer_states.count(fd)) {
            FileTransferState& state = fd_transfer_states[fd];
            if (state.file_stream.is_open()) {
                state.file_stream.close();
                remove(state.file_path.c_str());
                cout << "[文件] fd=" << fd << " 连接关闭, 删除部分文件: " << state.file_path << endl;
            }
            fd_transfer_states.erase(fd);
        }
    }

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
        int flag = msg.value("flag", 0); // Use .value for safety

        switch (flag) {
            case C2S_LOGIN_REQUEST:{
                cout << "收到客户端登录请求" << endl;
                handleLoginRequest(epfd, fd, msg);
                break;
            }
            case C2S_HEARTBEAT:{
                handleHeartbeat(epfd, fd, msg);
                break;
            }
            case C2S_GET_CHAT_LISTS:{
                handleGetChatLists(epfd, fd, msg);
                break;
            }
            case C2S_LOGOUT_REQUEST:{
                handleLogoutRequest(epfd, fd, msg);
                break;
            }
            case C2S_DEACTIVATE_REQUEST:{
                handleDeactivateRequest(epfd, fd, msg);
                break;
            }
            case C2S_START_CHAT_REQUEST:{
                handleStartChatRequest(epfd, fd, msg);
                break;
            }
            case C2S_START_GROUP_CHAT_REQUEST:{
                handleStartGroupChatRequest(epfd, fd, msg);
                break;
            }
            case C2S_PRIVATE_MESSAGE:{
                handlePrivateMessage(epfd, fd, msg);
                break;
            }
            case C2S_GROUP_MESSAGE:{
                handleGroupMessage(epfd, fd, msg);
                break;
            }
            case C2S_EXIT_CHAT_REQUEST:{
                handleExitChatRequest(epfd, fd, msg);
                break;
            }
            case C2S_ADD_FRIEND_REQUEST:{
                handleAddFriendRequest(epfd, fd, msg);
                break;
            }
            case C2S_GET_FRIEND_REQUESTS:{
                handleGetFriendRequests(epfd, fd, msg);
                break;
            }
            case C2S_RESPOND_TO_FRIEND_REQUEST:{
                handleRespondToFriendRequest(epfd, fd, msg);
                break;
            }
            case C2S_DELETE_FRIEND_REQUEST:{
                handleDeleteFriendRequest(epfd, fd, msg);
                break;
            }
            case C2S_BLOCK_FRIEND_REQUEST:{
                handleBlockFriendRequest(epfd, fd, msg);
                break;
            }
            case C2S_GET_BLOCKED_LIST_REQUEST:{
                handleGetBlockedListRequest(epfd, fd, msg);
                break;
            }
            case C2S_UNBLOCK_FRIEND_REQUEST:{
                handleUnblockFriendRequest(epfd, fd, msg);
                break;
            }
            case C2S_DEACTIVATE_ACCOUNT_REQUEST:{
                handleDeactivateAccountRequest(epfd, fd, msg);
                break;
            }
            case C2S_CREATE_GROUP_REQUEST:{
                handleCreateGroupRequest(epfd, fd, msg);
                break;
            }
            case C2S_JOIN_GROUP_REQUEST:{
                handleJoinGroupRequest(epfd, fd, msg);
                break;
            }
            case C2S_GET_MANAGED_GROUPS_REQUEST:{
                handleGetManagedGroupsRequest(epfd, fd, msg);
                break;
            }

            case C2S_GET_GROUP_MEMBERS_REQUEST:{
                handleGetGroupMembersRequest(epfd, fd, msg);
                break;
            }
            case C2S_KICK_GROUP_MEMBER_REQUEST:{
                handleKickGroupMemberRequest(epfd, fd, msg);
                break;
            }
            case C2S_APPOINT_ADMIN_REQUEST:{
                handleAppointAdminRequest(epfd, fd, msg);
                break;
            }
            case C2S_GET_GROUP_ADMINS_REQUEST:{
                handleGetGroupAdminsRequest(epfd, fd, msg);
                break;
            }
            case C2S_REVOKE_ADMIN_REQUEST:{
                handleRevokeAdminRequest(epfd, fd, msg);
                break;
            }
            case C2S_DELETE_GROUP_REQUEST:{
                handleDeleteGroupRequest(epfd, fd, msg);
                break;
            }
            case C2S_GET_JOINED_GROUPS_REQUEST:{
                handleGetJoinedGroupsRequest(epfd, fd, msg);
                break;
            }
            case C2S_QUIT_GROUP_REQUEST:{
                handleQuitGroupRequest(epfd, fd, msg);
                break;
            }
            case C2S_INVITE_TO_GROUP_REQUEST:{
                handleInviteToGroupRequest(epfd, fd, msg);
                break;
            }
            case C2S_GET_HISTORY_REQUEST:{
                handleGetHistoryRequest(epfd, fd, msg);
                break;
            }
            case C2S_SYNC_GROUPS_REQUEST: {
                string uid = getUidByFd(fd);
                if (!uid.empty()) {
                    Redis redis;
                    if (redis.connect()) {
                        User user;
                        string user_info_str = redis.hget("user_info", uid);
                        if (!user_info_str.empty()) {
                            user.json_parse(user_info_str);
                            cout << "[业务] 处理fd=" << fd << " 的同步群组列表请求" << endl;
                            GroupChat::sync(fd, user);
                        }
                    }
                }
                break;
            }



            case C2S_SEND_FILE_REQUEST:{
                handleSendFileRequest(epfd, fd, msg);
                break;
            }
            case C2S_RECV_FILE_REQUEST:{
                handleRecvFileRequest(epfd, fd, msg);
                break;
            }
            default:{
                cout << "[WARNING] Unknown message flag received: " << flag << " from fd=" << fd << endl;
                break;
            }
        }
    } catch (const nlohmann::json::parse_error& e) {
        cerr << "[ERROR] 来自fd=" << fd << " 的JSON解析错误: " << e.what() << "\n消息内容: " << json_msg << endl;
    }
}

// 💼 业务逻辑处理器实现

void handleLoginRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的登录请求" << endl;

    // 准备响应JSON对象
    nlohmann::json response;

    // 从请求中提取数据
    if (!msg.contains("data") || !msg["data"].is_object()) {
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "无效的请求格式.";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    auto data = msg["data"];
    string email = data.value("email", "");
    string password = data.value("password", "");

    if (email.empty() || password.empty()) {
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "邮箱或密码不能为空.";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // --- 核心业务逻辑 (从 LoginHandler.cc 迁移) ---
    Redis redis;
    if (!redis.connect()) {
        cerr << "[ERROR] fd=" << fd << " 的登录流程中Redis连接失败" << endl;
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "服务器内部错误 (Redis连接).";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 检查 1: 账户是否存在
    if (!redis.hexists("email_to_uid", email)) {
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "账户不存在.";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string UID = redis.hget("email_to_uid", email);
    string user_info_str = redis.hget("user_info", UID);

    // 检查 2: 用户信息是否有效
    User user;
    try {
        user.json_parse(user_info_str);
    } catch (const std::exception& e) {
        cerr << "[ERROR] UID: " << UID << " 的用户信息JSON解析失败, error: " << e.what() << endl;
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "服务器内部错误 (用户数据损坏).";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 检查 3: 密码是否正确
    if (password != user.getPassword()) {
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "密码错误.";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 检查 4: 用户是否已在线
    if (redis.hexists("is_online", UID)) {
        cout << "[INFO] 用户 " << UID << " 已在线. 拒绝重复登录." << endl;
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "用户已在其他设备登录.";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // --- 登录成功 ---
    cout << "[INFO] 用户 " << UID << " 登录成功, fd=" << fd << endl;
    redis.hset("is_online", UID, to_string(fd));
    addFdToUid(fd, UID); // 用于心跳机制

    response["flag"] = S2C_LOGIN_SUCCESS;
    response["data"] = nlohmann::json::parse(user_info_str); // 将完整的用户信息发回
    sendMsg(epfd, fd, response.dump());
}

void handleHeartbeat(int epfd, int fd, const nlohmann::json& msg) {
    // 新协议的心跳包必须携带UID
    if (msg.contains("data") && msg["data"].contains("uid")) {
        string uid = msg["data"]["uid"].get<string>();
        addFdToUid(fd, uid); // 关联fd和uid，并更新活跃时间
        cout << "[心跳] 收到心跳，fd=" << fd << ", uid=" << uid << endl;
        updateUserActivity(uid);
    } else {
        cout << "[警告] 收到一个不规范的心跳包，已忽略。fd=" << fd << endl;
    }
}

void handleGetChatLists(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[\u4e1a\u52a1] \u5904\u7406fd=" << fd << " \u7684\u83b7\u53d6\u804a\u5929\u5217\u8868\u8bf7\u6c42" << endl;
    nlohmann::json response;
    string uid = getUidByFd(fd);

    if (uid.empty()) {
        response["flag"] = S2C_CHAT_LISTS_RESPONSE;
        response["data"]["reason"] = "\u7528\u6237\u672a\u767b\u5f55或\u4f1a\u8bdd\u5df2\u8fc7\u671f";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Redis redis;
    if (!redis.connect()) {
        response["flag"] = S2C_CHAT_LISTS_RESPONSE;
        response["data"]["reason"] = "\u670d\u52a1\u5668\u5185\u90e8\u9519\u8bef (Redis)";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    response["flag"] = S2C_CHAT_LISTS_RESPONSE;
    response["data"]["friends"] = nlohmann::json::array();
    response["data"]["groups"] = nlohmann::json::array();


    redisReply *friend_replies = redis.smembers(uid);
    if (friend_replies) {
        cout << "[DEBUG] Found friends for UID: " << uid << endl;
        for (size_t i = 0; i < friend_replies->elements; ++i) {
            if (friend_replies->element[i] == nullptr || friend_replies->element[i]->str == nullptr) continue;
            string friend_id = friend_replies->element[i]->str;
            cout << "  [DEBUG] Processing friend ID: " << friend_id << endl;
            string user_info_str = redis.hget("user_info", friend_id);
            cout << "    [DEBUG] Fetched user_info: " << user_info_str << endl;
            if (!user_info_str.empty()) {
                cout << "      [DEBUG] User info is not empty. Parsing and adding to response." << endl;
                try {
                    nlohmann::json friend_json = nlohmann::json::parse(user_info_str);
                    friend_json["is_online"] = redis.hexists("is_online", friend_id);
                    response["data"]["friends"].push_back(friend_json);
                } catch (const nlohmann::json::parse_error& e) {
                    cerr << "[ERROR] Failed to parse user_info_str for friend " << friend_id << ": " << e.what() << endl;
                }
            } else {
                cout << "      [DEBUG] User info is EMPTY. Skipping." << endl;
            }
        }
        freeReplyObject(friend_replies);
    } else {
        cout << "[DEBUG] No friends found for UID: " << uid << " in Redis set." << endl;
    }


    string group_list_key = "user_groups:" + uid;
    redisReply *group_reply = redis.smembers(group_list_key);
    if (group_reply) {
        for (size_t i = 0; i < group_reply->elements; ++i) {
            if (group_reply->element[i] == nullptr || group_reply->element[i]->str == nullptr) continue;
            string group_id = group_reply->element[i]->str;
            string group_info_str = redis.hget("group_info", group_id);
            if (!group_info_str.empty()) {
                response["data"]["groups"].push_back(nlohmann::json::parse(group_info_str));
            }
        }
        freeReplyObject(group_reply);
    }

    sendMsg(epfd, fd, response.dump());
}

void handleLogoutRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[\u4e1a\u52a1] \u5904\u7406fd=" << fd << " \u7684\u767b\u51fa\u8bf7\u6c42" << endl;
    string uid = getUidByFd(fd);
    if (!uid.empty()) {
        Redis redis;
        if (redis.connect()) {
            redis.hdel("is_online", uid);
        }
        removeFdFromUid(fd); // Clean up fd-uid mapping
    }
    // No response is needed for logout.
}

void handleDeactivateRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[\u4e1a\u52a1] \u5904\u7406fd=" << fd << " \u7684\u8d26\u6237\u6ce8\u9500\u8bf7\u6c42" << endl;
    string uid = getUidByFd(fd);
    if (!uid.empty()) {
        // Here you would typically mark the user as deactivated in your database.
        // For now, we'll just log them out.
        Redis redis;
        if (redis.connect()) {
            redis.hdel("is_online", uid);
        }
        removeFdFromUid(fd);
        cout << "[INFO] \u7528\u6237 " << uid << " \u5df2\u88ab\u6ce8\u9500\u5e76\u767b\u51fa." << endl;
    }
    // No response needed, client will close connection.
}

void handleStartChatRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[\u4e1a\u52a1] \u5904\u7406fd=" << fd << " \u7684\u5f00\u59cb\u804a\u5929\u8bf7\u6c42" << endl;
    nlohmann::json response;
    string uid = getUidByFd(fd);

    if (uid.empty()) {
        response["flag"] = S2C_START_CHAT_RESPONSE;
        response["data"]["success"] = false;
        response["data"]["reason"] = "\u7528\u6237\u672a\u767b\u5f55\u6216\u4f1a\u8bdd\u5df2\u8fc7\u671f";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (!msg.contains("data") || !msg["data"].is_object() || !msg["data"].contains("friend_uid")) {
        response["flag"] = S2C_START_CHAT_RESPONSE;
        response["data"]["success"] = false;
        response["data"]["reason"] = "\u65e0\u6548\u7684\u8bf7\u6c42\u683c\u5f0f";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    string friend_uid = msg["data"]["friend_uid"].get<string>();

    // Fetch history messages from MySQL using the global connection
    // Load the most recent 50 messages to prevent overloading the client.
    vector<string> history = g_mysql.getPrivateHistoryAfterTime(uid, friend_uid, 0, 50);

    response["flag"] = S2C_START_CHAT_RESPONSE;
    response["data"]["success"] = true;
    response["data"]["history"] = history;

    sendMsg(epfd, fd, response.dump());
}

void handleStartGroupChatRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[\u4e1a\u52a1] \u5904\u7406fd=" << fd << " \u7684\u5f00\u59cb\u7fa4\u804a\u8bf7\u6c42" << endl;
    nlohmann::json response;
    string uid = getUidByFd(fd);

    if (uid.empty()) {
        response["flag"] = S2C_START_GROUP_CHAT_RESPONSE;
        response["data"]["success"] = false;
        response["data"]["reason"] = "\u7528\u6237\u672a\u767b\u5f55\u6216\u4f1a\u8bdd\u5df2\u8fc7\u671f";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (!msg.contains("data") || !msg["data"].is_object() || !msg["data"].contains("group_uid")) {
        response["flag"] = S2C_START_GROUP_CHAT_RESPONSE;
        response["data"]["success"] = false;
        response["data"]["reason"] = "\u65e0\u6548\u7684\u8bf7\u6c42\u683c\u5f0f";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    string group_uid = msg["data"]["group_uid"].get<string>();

    // Fetch history messages from MySQL using the global connection
    // Load the most recent 50 messages to prevent overloading the client.
    vector<string> history = g_mysql.getGroupHistoryAfterTime(group_uid, 0, 50);

    response["flag"] = S2C_START_GROUP_CHAT_RESPONSE;
    response["data"]["success"] = true;
    response["data"]["history"] = history;

    sendMsg(epfd, fd, response.dump());
}

void handlePrivateMessage(int epfd, int fd, const nlohmann::json& msg) {
    string sender_uid = getUidByFd(fd);
    if (sender_uid.empty()) return; // Not logged in

    auto data = msg["data"];
    string receiver_uid = data.value("receiver_uid", "");
    string content = data.value("content", "");

    // 1. Save to MySQL
    g_mysql.insertPrivateMessage(sender_uid, receiver_uid, content);

    // 2. Forward to receiver if online
    Redis redis;
    if (redis.connect() && redis.hexists("is_online", receiver_uid)) {
        int receiver_fd = stoi(redis.hget("is_online", receiver_uid));
        nlohmann::json forward_msg;
        forward_msg["flag"] = S2C_PRIVATE_MESSAGE;
        forward_msg["data"]["content"] = content;
        forward_msg["data"]["sender_uid"] = sender_uid;
        forward_msg["data"]["username"] = getUsernameFromRedis(sender_uid); // 添加发送者用户名
        sendMsg(epfd, receiver_fd, forward_msg.dump());
    }
}

void handleGroupMessage(int epfd, int fd, const nlohmann::json& msg) {
    string sender_uid = getUidByFd(fd);
    if (sender_uid.empty()) return; // Not logged in

    auto data = msg["data"];
    string group_uid = data.value("group_uid", "");
    string content = data.value("content", "");

    // 1. Save to MySQL
    g_mysql.insertGroupMessage(group_uid, sender_uid, content);

    // 2. Forward to all online group members
    Redis redis;
    if (redis.connect()) {
        redisReply *replies = redis.smembers("group_members:" + group_uid);
        if (replies) {
            for (size_t i = 0; i < replies->elements; ++i) {
                if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
                string member_uid = replies->element[i]->str;
                if (member_uid != sender_uid && redis.hexists("is_online", member_uid)) {
                    int member_fd = stoi(redis.hget("is_online", member_uid));
                    nlohmann::json forward_msg;
                    forward_msg["flag"] = S2C_GROUP_MESSAGE;
                    forward_msg["data"]["content"] = content;
                    forward_msg["data"]["group_uid"] = group_uid;
                    forward_msg["data"]["sender_uid"] = sender_uid;
                    forward_msg["data"]["username"] = getUsernameFromRedis(sender_uid);
                    forward_msg["data"]["group_name"] = getGroupNameFromRedis(group_uid);
                    sendMsg(epfd, member_fd, forward_msg.dump());
                }
            }
            freeReplyObject(replies);
        }
    }
}

void handleExitChatRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的退出聊天请求" << endl;
    // This is mostly a client-side state change.
    // The server just needs to acknowledge it to prevent crashes.
    // No response is needed.
}
// 基于 Transaction.cc 中 add_friend 的现代化改造
void handleAddFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的添加好友请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_ADD_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期，请重新登录";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_username = msg["data"].value("friend_username", "");
    if (friend_username.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "好友用户名不能为空";
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

    if (!redis.hexists("username_to_uid", friend_username)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = redis.hget("username_to_uid", friend_username);
    if (my_uid == friend_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "不能添加自己为好友";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (redis.sismember(my_uid, friend_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "对方已经是您的好友";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 使用你原有的键名规则: [好友UID] + "add_friend"
    string request_key = friend_uid + "add_friend";
    if (redis.sismember(request_key, my_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "已发送过请求，请勿重复发送";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.sadd(request_key, my_uid);

    // 使用你原有的通知键名规则: [好友UID] + "add_f_notify"
    string notify_key = friend_uid + "add_f_notify";
    redis.sadd(notify_key, my_uid);

    // 如果对方在线，发送实时通知
    if (redis.hexists("unified_receiver", friend_uid)) {
        string receiver_fd_str = redis.hget("unified_receiver", friend_uid);
        int receiver_fd = stoi(receiver_fd_str);
        sendMsg(receiver_fd, REQUEST_NOTIFICATION); // 使用旧的通知宏，后续可以升级为JSON
        redis.srem(notify_key, my_uid); // 发送后移除离线通知
    }

    response["data"]["success"] = true;
    response["data"]["reason"] = "好友请求发送成功";
    sendMsg(epfd, fd, response.dump());
}

void handleGetFriendRequests(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取好友请求列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_FRIEND_REQUESTS_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
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

    string request_key = my_uid + "add_friend";
    redisReply *replies = redis.smembers(request_key);

    nlohmann::json requesters = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string requester_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", requester_uid);
            if (!user_info_str.empty()) {
                try {
                    requesters.push_back(nlohmann::json::parse(user_info_str));
                } catch (const nlohmann::json::parse_error& e) {
                    cerr << "[ERROR] 解析好友请求者信息失败: " << e.what() << endl;
                }
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["requests"] = requesters;
    sendMsg(epfd, fd, response.dump());
}

void handleRespondToFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的响应好友请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_RESPOND_TO_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string requester_uid = msg["data"].value("requester_uid", "");
    bool accepted = msg["data"].value("accepted", false);

    Redis redis;
    if (!redis.connect()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "服务器内部错误 (Redis)";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string request_key = my_uid + "add_friend";
    if (!redis.sismember(request_key, requester_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该好友请求不存在或已处理";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.srem(request_key, requester_uid);

    if (accepted) {
        redis.sadd(my_uid, requester_uid);
        redis.sadd(requester_uid, my_uid);

        time_t now = time(nullptr);
        redis.hset("friend_join_time", my_uid + "_" + requester_uid, to_string(now));
        redis.hset("friend_join_time", requester_uid + "_" + my_uid, to_string(now));

        response["data"]["reason"] = "已成功添加好友";
    } else {
        response["data"]["reason"] = "已拒绝好友请求";
    }

    response["data"]["success"] = true;
    sendMsg(epfd, fd, response.dump());
}

void handleDeleteFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的删除好友请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_DELETE_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = msg["data"].value("friend_uid", "");
    if (friend_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的好友ID";
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

    // 1. 从我的好友列表删除对方 (单向)
    redis.srem(my_uid, friend_uid);

    // 2. 清除好友加入时间记录
    redis.hdel("friend_join_time", my_uid + "_" + friend_uid);
    redis.hdel("friend_join_time", friend_uid + "_" + my_uid);

    // 3. 从我的屏蔽列表删除对方 (如果有的话)
    redis.srem("blocked" + my_uid, friend_uid);

    // 4. 删除MySQL中的私聊历史记录
    g_mysql.deleteMessagesCompletely(my_uid, friend_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "好友已成功删除";
    sendMsg(epfd, fd, response.dump());
}

void handleBlockFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的屏蔽好友请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_BLOCK_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = msg["data"].value("friend_uid", "");
    if (friend_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的好友ID";
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

    // 使用你设计的键名 "blocked" + my_uid
    string block_key = "blocked" + my_uid;
    redis.sadd(block_key, friend_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功屏蔽该好友";
    sendMsg(epfd, fd, response.dump());
}

void handleGetBlockedListRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取屏蔽列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_BLOCKED_LIST_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
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

    string block_key = "blocked" + my_uid;
    redisReply *replies = redis.smembers(block_key);

    nlohmann::json blocked_users = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string blocked_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", blocked_uid);
            if (!user_info_str.empty()) {
                try {
                    blocked_users.push_back(nlohmann::json::parse(user_info_str));
                } catch (const nlohmann::json::parse_error& e) {
                    cerr << "[ERROR] 解析被屏蔽用户信息失败: " << e.what() << endl;
                }
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["blocked_list"] = blocked_users;
    sendMsg(epfd, fd, response.dump());
}

void handleUnblockFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的解除屏蔽请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_UNBLOCK_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = msg["data"].value("friend_uid", "");
    if (friend_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的好友ID";
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

    string block_key = "blocked" + my_uid;
    redis.srem(block_key, friend_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功解除屏蔽";
    sendMsg(epfd, fd, response.dump());
}

void handleDeactivateAccountRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的注销账户请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_DEACTIVATE_ACCOUNT_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
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

    // 核心注销逻辑，移植自 Transaction.cc
    // 1. 将用户添加到注销集合
    redis.sadd("deactivated_users", my_uid);

    // 2. 解散该用户创建的所有群聊
    string created_groups_key = "created" + my_uid;
    redisReply *created_groups_reply = redis.smembers(created_groups_key);
    if (created_groups_reply) {
        for (size_t i = 0; i < created_groups_reply->elements; ++i) {
            if (created_groups_reply->element[i] == nullptr || created_groups_reply->element[i]->str == nullptr) continue;
            string group_uid = created_groups_reply->element[i]->str;
            string group_info_str = redis.hget("group_info", group_uid);
            if (group_info_str.empty()) continue;

            Group group;
            group.json_parse(group_info_str);

            // 通知所有群成员
            string members_key = group.getMembers();
            redisReply *members_reply = redis.smembers(members_key);
            if (members_reply) {
                for (size_t j = 0; j < members_reply->elements; ++j) {
                    if (members_reply->element[j] == nullptr || members_reply->element[j]->str == nullptr) continue;
                    string member_uid = members_reply->element[j]->str;
                    if (member_uid == my_uid) continue;
                    // TODO: 此处应发送一个JSON格式的解散通知
                    if (redis.hexists("unified_receiver", member_uid)) {
                        int member_fd = stoi(redis.hget("unified_receiver", member_uid));
                        sendMsg(member_fd, "deleteAC_notify:" + group.getGroupName());
                    }
                }
                freeReplyObject(members_reply);
            }

            // 清理群组相关数据
            redis.del(members_key);
            redis.del(group.getAdmins());
            redis.del(group.getGroupUid() + "history");
            redis.srem("group_Name", group.getGroupName());
            redis.hdel("group_info", group.getGroupUid());
            g_mysql.deleteGroupMessages(group.getGroupUid());
        }
        freeReplyObject(created_groups_reply);
    }

    // 3. 清理用户数据
    string user_info_str = redis.hget("user_info", my_uid);
    if (!user_info_str.empty()) {
        User user;
        user.json_parse(user_info_str);
        redis.hdel("email_to_uid", user.getEmail());
    }
    redis.hdel("user_info", my_uid);

    // 4. 删除所有私聊记录
    redisReply *friends_reply = redis.smembers(my_uid);
    if (friends_reply) {
        for (size_t i = 0; i < friends_reply->elements; ++i) {
            if (friends_reply->element[i] == nullptr || friends_reply->element[i]->str == nullptr) continue;
            string friend_uid = friends_reply->element[i]->str;
            g_mysql.deleteMessagesCompletely(my_uid, friend_uid);
            redis.srem(friend_uid, my_uid); // 从对方好友列表中移除自己
        }
        freeReplyObject(friends_reply);
    }
    redis.del(my_uid); // 删除自己的好友列表

    // 5. 登出
    redis.hdel("is_online", my_uid);
    redis.hdel("unified_receiver", my_uid);
    removeUserActivity(my_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "账户已成功注销";
    sendMsg(epfd, fd, response.dump());

    // 最后关闭连接
    handleCloseEvent(epfd, fd);
}

void handleCreateGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的创建群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_CREATE_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_name = msg["data"].value("group_name", "");
    if (group_name.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群聊名称不能为空";
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

    if (redis.sismember("group_Name", group_name)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该群名已存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 创建群聊对象
    Group group(group_name, my_uid);
    string group_info = group.to_json();
    string group_uid = group.getGroupUid();

    // 存储群聊信息
    redis.hset("group_info", group_uid, group_info);
    redis.sadd("group_Name", group_name);

    // 更新用户与群聊的关系
    redis.sadd("joined" + my_uid, group_uid);
    redis.sadd("managed" + my_uid, group_uid);
    redis.sadd("created" + my_uid, group_uid);
    redis.sadd(group.getMembers(), my_uid);
    redis.sadd(group.getAdmins(), my_uid);
    redis.hset("user_join_time", group_uid + "_" + my_uid, to_string(time(nullptr)));

    response["data"]["success"] = true;
    response["data"]["reason"] = "群聊创建成功";
    response["data"]["group_info"] = nlohmann::json::parse(group_info);
    sendMsg(epfd, fd, response.dump());
}

void handleJoinGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的加入群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_JOIN_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_name = msg["data"].value("group_name", "");
    if (group_name.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群聊名称不能为空";
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

    if (!redis.sismember("group_Name", group_name)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该群不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 你的findGroupUidByName逻辑效率较低，这里暂时保留，后续可以优化为 group_name_to_uid 的哈希表
    string group_uid;
    redisReply* reply = (redisReply*)redisCommand(redis.getContext(), "HGETALL group_info");
    if (reply) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            string current_group_info = reply->element[i + 1]->str;
            try {
                auto current_group_json = nlohmann::json::parse(current_group_info);
                if (current_group_json.value("groupName", "") == group_name) {
                    group_uid = reply->element[i]->str;
                    break;
                }
            } catch (const nlohmann::json::parse_error& e) { continue; }
        }
        freeReplyObject(reply);
    }

    if (group_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该群不存在 (uid lookup failed)";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (redis.sismember("joined" + my_uid, group_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "你已经是该群成员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string request_key = "if_add" + group_uid;
    if (redis.sismember(request_key, my_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "你已经发送过入群请求，请等待管理员同意";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.sadd(request_key, my_uid);

    // TODO: 通知管理员

    response["data"]["success"] = true;
    response["data"]["reason"] = "入群请求已发送，等待管理员同意";
    sendMsg(epfd, fd, response.dump());
}

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

    // 权限检查
    if (chat_type == "private") {
        if (!redis.sismember(sender_uid, target_uid)) {
            response["data"]["success"] = false;
            response["data"]["reason"] = "对方不是您的好友";
            sendMsg(epfd, fd, response.dump());
            return;
        }
        if (redis.sismember("blocked" + target_uid, sender_uid)) {
            response["data"]["success"] = false;
            response["data"]["reason"] = "您已被对方屏蔽";
            sendMsg(epfd, fd, response.dump());
            return;
        }
    } else if (chat_type == "group") {
        if (!redis.sismember("group_members:" + target_uid, sender_uid)) {
            response["data"]["success"] = false;
            response["data"]["reason"] = "您不是该群成员";
            sendMsg(epfd, fd, response.dump());
            return;
        }
    }

    // 准备接收文件
    lock_guard<mutex> lock(transfer_states_mutex);
    FileTransferState& state = fd_transfer_states[fd];

    // 生成唯一文件名
    size_t dotPos = file_name.rfind('.');
    string name = file_name.substr(0, dotPos);
    string ext = (dotPos != string::npos) ? file_name.substr(dotPos) : "";
    state.file_name = name + "_" + to_string(time(nullptr)) + ext;
    state.file_path = "./fileBuffer_send/" + state.file_name;

    // 确保目录存在
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
    state.receiver_uid = target_uid; // For private chat
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
    string chat_type = data.value("chat_type", "");
    string source_uid = data.value("source_uid", ""); // For group, this is group_uid

    if (file_path.empty() || chat_type.empty() || source_uid.empty()) {
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

    string queue_key;
    if (chat_type == "private") {
        queue_key = "recv" + my_uid;
    } else { // group
        queue_key = "recv_group:" + source_uid + ":" + my_uid;
    }

    if (!redis.sismember(queue_key, file_path)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "文件不存在或已被接收";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "服务器上的文件已丢失";
        sendMsg(epfd, fd, response.dump());
        redis.srem(queue_key, file_path); // Clean up broken entry
        return;
    }

    // Prepare for sending
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
    response["data"]["file_name"] = file_path.substr(file_path.find_last_of('/') + 1);
    response["data"]["file_size"] = state.file_size;
    sendMsg(epfd, fd, response.dump());

    // Register for EPOLLOUT to start sending data
    struct epoll_event temp;
    temp.data.fd = fd;
    temp.events = EPOLLIN | EPOLLET | EPOLLOUT;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &temp);
}

void handleFileData(int epfd, int fd, const char* data, int len) {
    lock_guard<mutex> lock(transfer_states_mutex);
    if (fd_transfer_states.count(fd) == 0) return;

    FileTransferState& state = fd_transfer_states[fd];
    if (state.status != TransferStatus::RECEIVING) return;

    state.file_stream.write(data, len);
    state.transferred_bytes += len;

    if (state.transferred_bytes >= state.file_size) {
        cout << "[文件] fd=" << fd << " 文件接收完成: " << state.file_name << endl;
        state.file_stream.close();

        Redis redis;
        redis.connect();

        // Create file message for history
        Message fileMessage;
        fileMessage.setUsername(getUsernameFromRedis(state.sender_uid));
        fileMessage.setUidFrom(state.sender_uid);
        fileMessage.setContent("[文件]" + state.file_name);

        if (state.chat_type == "private") {
            fileMessage.setUidTo(state.receiver_uid);
            g_mysql.insertPrivateMessage(state.sender_uid, state.receiver_uid, fileMessage.getContent());

            // Add to receiver's pending queue
            redis.sadd("recv" + state.receiver_uid, state.file_path);

            // Notify receiver if online
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

            // Add to each member's pending queue
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

        // Cleanup
        fd_transfer_states.erase(fd);
    }
}


void handleGetManagedGroupsRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取可管理群聊列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_MANAGED_GROUPS_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
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

    // 使用set来自动处理重复的群组ID
    std::set<string> managed_group_uids;

    // 获取创建的群
    string created_key = "created" + my_uid;
    redisReply *created_reply = redis.smembers(created_key);
    if (created_reply) {
        for (size_t i = 0; i < created_reply->elements; ++i) {
            if (created_reply->element[i] == nullptr || created_reply->element[i]->str == nullptr) continue;
            managed_group_uids.insert(created_reply->element[i]->str);
        }
        freeReplyObject(created_reply);
    }

    // 获取管理的群
    string managed_key = "managed" + my_uid;
    redisReply *managed_reply = redis.smembers(managed_key);
    if (managed_reply) {
        for (size_t i = 0; i < managed_reply->elements; ++i) {
            if (managed_reply->element[i] == nullptr || managed_reply->element[i]->str == nullptr) continue;
            managed_group_uids.insert(managed_reply->element[i]->str);
        }
        freeReplyObject(managed_reply);
    }

    nlohmann::json managed_groups = nlohmann::json::array();
    for (const auto& group_uid : managed_group_uids) {
        string group_info_str = redis.hget("group_info", group_uid);
        if (!group_info_str.empty()) {
            try {
                managed_groups.push_back(nlohmann::json::parse(group_info_str));
            } catch (const nlohmann::json::parse_error& e) {
                cerr << "[ERROR] 解析可管理群组信息失败: " << e.what() << endl;
            }
        }
    }

    response["data"]["success"] = true;
    response["data"]["managed_groups"] = managed_groups;
    sendMsg(epfd, fd, response.dump());
}

void handleGetGroupJoinRequests(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取入群申请列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_GROUP_JOIN_REQUESTS_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");

    if (my_uid.empty() || group_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ }

    // TODO: 权限检查，确保 my_uid 是 group_uid 的管理员

    string request_key = "if_add" + group_uid;
    redisReply *replies = redis.smembers(request_key);

    nlohmann::json requesters = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string requester_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", requester_uid);
            if (!user_info_str.empty()) {
                try {
                    requesters.push_back(nlohmann::json::parse(user_info_str));
                } catch (const nlohmann::json::parse_error& e) { /* ... */ }
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["requests"] = requesters;
    sendMsg(epfd, fd, response.dump());
}

void handleRespondToGroupJoinRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的响应入群申请请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_RESPOND_TO_GROUP_JOIN_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");
    string requester_uid = msg["data"].value("requester_uid", "");
    bool accepted = msg["data"].value("accepted", false);

    if (my_uid.empty() || group_uid.empty() || requester_uid.empty()) { /* ... error handling ... */ }

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ }

    // TODO: 权限检查

    string request_key = "if_add" + group_uid;
    if (!redis.sismember(request_key, requester_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该申请不存在或已被处理";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.srem(request_key, requester_uid);

    if (accepted) {
        string group_info_str = redis.hget("group_info", group_uid);
        if (!group_info_str.empty()) {
            Group group;
            group.json_parse(group_info_str);
            redis.sadd(group.getMembers(), requester_uid);
            redis.sadd("joined" + requester_uid, group_uid);
            redis.hset("user_join_time", group_uid + "_" + requester_uid, to_string(time(nullptr)));
            response["data"]["reason"] = "已同意该用户的入群申请";
        } else {
            // 理论上不应发生
            response["data"]["success"] = false;
            response["data"]["reason"] = "群信息不存在";
            sendMsg(epfd, fd, response.dump());
            return;
        }
    } else {
        response["data"]["reason"] = "已拒绝该用户的入群申请";
    }

    response["data"]["success"] = true;
    sendMsg(epfd, fd, response.dump());
}

void handleGetGroupMembersRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取群成员列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_GROUP_MEMBERS_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");

    // ... (权限检查) ...

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) { /* ... error handling ... */ return; }

    Group group;
    group.json_parse(group_info_str);

    redisReply *replies = redis.smembers(group.getMembers());
    nlohmann::json members = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string member_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", member_uid);
            if (!user_info_str.empty()) {
                members.push_back(nlohmann::json::parse(user_info_str));
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["members"] = members;
    sendMsg(epfd, fd, response.dump());
}

void handleKickGroupMemberRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的踢出群成员请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_KICK_GROUP_MEMBER_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");
    string kick_uid = msg["data"].value("kick_uid", "");

    // ... (权限检查) ...

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) { /* ... error handling ... */ return; }

    Group group;
    group.json_parse(group_info_str);

    if (kick_uid == group.getOwnerUid()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "不能踢出群主";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (kick_uid == my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "不能踢出自己";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 执行踢人操作
    redis.srem(group.getMembers(), kick_uid);
    redis.srem("joined" + kick_uid, group_uid);
    redis.srem(group.getAdmins(), kick_uid); // 如果是管理员，也一并移除
    redis.srem("managed" + kick_uid, group_uid);

    // TODO: 通知被踢出的用户

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功踢出该成员";
    sendMsg(epfd, fd, response.dump());
}

void handleAppointAdminRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的设置管理员请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_APPOINT_ADMIN_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");
    string appoint_uid = msg["data"].value("appoint_uid", "");

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) { /* ... error handling ... */ return; }

    Group group;
    group.json_parse(group_info_str);

    // 权限检查: 必须是群主
    if (group.getOwnerUid() != my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "权限不足，只有群主才能设置管理员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 检查是否已经是管理员
    if (redis.sismember(group.getAdmins(), appoint_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该用户已经是管理员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 执行任命操作
    redis.sadd(group.getAdmins(), appoint_uid);
    redis.sadd("managed" + appoint_uid, group_uid);

    // TODO: 通知被任命的用户

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功设置该用户为管理员";
    sendMsg(epfd, fd, response.dump());
}

void handleGetGroupAdminsRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取群管理员列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_GROUP_ADMINS_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) { /* ... error handling ... */ return; }

    Group group;
    group.json_parse(group_info_str);

    // 权限检查: 必须是群主
    if (group.getOwnerUid() != my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "权限不足";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redisReply *replies = redis.smembers(group.getAdmins());
    nlohmann::json admins = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string admin_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", admin_uid);
            if (!user_info_str.empty()) {
                admins.push_back(nlohmann::json::parse(user_info_str));
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["admins"] = admins;
    sendMsg(epfd, fd, response.dump());
}

void handleRevokeAdminRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的撤销管理员请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_REVOKE_ADMIN_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");
    string revoke_uid = msg["data"].value("revoke_uid", "");

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) { /* ... error handling ... */ return; }

    Group group;
    group.json_parse(group_info_str);

    // 权限检查: 必须是群主
    if (group.getOwnerUid() != my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "权限不足，只有群主才能撤销管理员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 逻辑检查: 不能撤销群主自己
    if (revoke_uid == my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "不能撤销群主自己的管理员身份";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 执行撤销操作
    redis.srem(group.getAdmins(), revoke_uid);
    redis.srem("managed" + revoke_uid, group_uid);

    // TODO: 通知被撤销的用户

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功撤销该用户的管理员身份";
    sendMsg(epfd, fd, response.dump());
}

void handleDeleteGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的解散群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_DELETE_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) { /* ... error handling ... */ return; }

    Group group;
    group.json_parse(group_info_str);

    // 权限检查: 必须是群主
    if (group.getOwnerUid() != my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "权限不足，只有群主才能解散群聊";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 通知所有成员
    redisReply *members_reply = redis.smembers(group.getMembers());
    if (members_reply) {
        for (size_t i = 0; i < members_reply->elements; ++i) {
            if (members_reply->element[i] == nullptr || members_reply->element[i]->str == nullptr) continue;
            string member_uid = members_reply->element[i]->str;
            if (member_uid == my_uid) continue;
            // TODO: 升级为JSON通知
            if (redis.hexists("unified_receiver", member_uid)) {
                int member_fd = stoi(redis.hget("unified_receiver", member_uid));
                sendMsg(member_fd, "DELETE:" + group.getGroupName());
            }
            // 清理每个成员的群组关联
            redis.srem("joined" + member_uid, group_uid);
            redis.srem("managed" + member_uid, group_uid);
        }
        freeReplyObject(members_reply);
    }

    // 清理群主自己的关联
    redis.srem("joined" + my_uid, group_uid);
    redis.srem("managed" + my_uid, group_uid);
    redis.srem("created" + my_uid, group_uid);

    // 删除群组核心数据
    redis.del(group.getMembers());
    redis.del(group.getAdmins());
    redis.del(group.getGroupUid() + "history"); // 旧版历史记录，以防万一
    redis.srem("group_Name", group.getGroupName());
    redis.hdel("group_info", group_uid);

    // 删除MySQL中的群聊消息
    g_mysql.deleteGroupMessages(group_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "群聊已成功解散";
    sendMsg(epfd, fd, response.dump());
}

void handleGetJoinedGroupsRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取已加入群聊列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_JOINED_GROUPS_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string joined_key = "joined" + my_uid;
    redisReply *replies = redis.smembers(joined_key);
    nlohmann::json joined_groups = nlohmann::json::array();
    if (replies != nullptr) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string group_uid = replies->element[i]->str;
            string group_info_str = redis.hget("group_info", group_uid);
            if (!group_info_str.empty()) {
                try {
                    joined_groups.push_back(nlohmann::json::parse(group_info_str));
                } catch (const nlohmann::json::parse_error& e) {
                    cerr << "[ERROR] 解析已加入群组信息失败: " << e.what() << endl;
                }
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["joined_groups"] = joined_groups;
    sendMsg(epfd, fd, response.dump());
}

void handleQuitGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的退出群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_QUIT_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) { /* ... error handling ... */ return; }

    Group group;
    group.json_parse(group_info_str);

    // 权限检查: 群主不能退出群聊
    if (group.getOwnerUid() == my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群主不能退出自己创建的群聊，请先解散群聊";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 执行退群操作
    redis.srem(group.getMembers(), my_uid);
    redis.srem(group.getAdmins(), my_uid); // 如果是管理员，也一并移除
    redis.srem("joined" + my_uid, group_uid);
    redis.srem("managed" + my_uid, group_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功退出该群聊";
    sendMsg(epfd, fd, response.dump());
}

void handleInviteToGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的邀请好友入群请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_INVITE_TO_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);
    string group_uid = msg["data"].value("group_uid", "");
    string friend_username = msg["data"].value("friend_username", "");

    Redis redis;
    if (!redis.connect()) { /* ... error handling ... */ return; }

    string friend_uid = redis.hget("username_to_uid", friend_username);

    if (friend_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该用户不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) { /* ... error handling ... */ return; }

    Group group;
    group.json_parse(group_info_str);

    if (friend_uid == my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "不能邀请自己";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    if (!redis.sismember(my_uid, friend_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "TA不是你的好友";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    if (redis.sismember("deactivated_users", friend_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该好友已注销";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    if (redis.sismember(group.getMembers(), friend_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该好友已在群聊中";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // 直接将好友加入群聊
    redis.sadd(group.getMembers(), friend_uid);
    redis.sadd("joined" + friend_uid, group_uid);
    redis.hset("user_join_time", group_uid + "_" + friend_uid, to_string(time(nullptr)));

    // TODO: 通知被邀请的用户

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功邀请好友加入群聊";
    sendMsg(epfd, fd, response.dump());
}

void handleGetHistoryRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取历史记录请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_HISTORY_RESPONSE;
    string my_uid = getUidByFd(fd);
    string target_uid = msg["data"].value("target_uid", "");
    string chat_type = msg["data"].value("chat_type", "");

    if (my_uid.empty() || target_uid.empty() || chat_type.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求参数";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    vector<string> history;
    if (chat_type == "private") {
                history = g_mysql.getPrivateHistoryAfterTime(my_uid, target_uid, 0, 50);
    } else if (chat_type == "group") {
        Redis redis;
        if (redis.connect()) {
            string join_time_str = redis.hget("user_join_time", target_uid + "_" + my_uid);
            time_t join_time = 0;
            if (!join_time_str.empty() && join_time_str != "(nil)") {
                join_time = stoll(join_time_str);
            }
            history = g_mysql.getGroupHistoryAfterTime(target_uid, join_time, 50);
        }
    }

    response["data"]["success"] = true;
    // 直接将字符串向量赋值给json，它会自动转换成JSON数组
    response["data"]["history"] = history;
    sendMsg(epfd, fd, response.dump());
}











void handleLegacyCommand(int epfd, int fd, const std::string& msg, ThreadPool& pool) {
    cout << "[\u517c容\u6a21\u5f0f] \u6536\u5230\u65e7协\u8bae\u547d令: " << msg << endl;


    if (msg == UNIFIED_RECEIVER) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        pool.addTask([=](){ handleUnifiedReceiver(epfd, fd); });
    } else if (msg == SENDFILE_F) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        pool.addTask([=](){ sendFile_Friend(epfd, fd); });
    } else if (msg == RECVFILE_F) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        pool.addTask([=](){ recvFile_Friend(epfd, fd); });
    } else if (msg == SENDFILE_G) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        pool.addTask([=](){ sendFile_Group(epfd, fd); });
    } else if (msg == RECVFILE_G) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        pool.addTask([=](){ recvFile_Group(epfd, fd); });
    } else if (msg == REQUEST_CODE) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        pool.addTask([=](){ handleRequestCode(epfd, fd); });
    } else if (msg == REGISTER_WITH_CODE) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        pool.addTask([=](){ serverRegisterWithCode(epfd, fd); });
    } else if (msg == REQUEST_RESET_CODE) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        pool.addTask([=](){ handleResetCode(epfd, fd); });
    } else {
        cout << "[\u517c容\u6a21\u5f0f] \u6536\u5230\u672a\u77e5\u7684\u65e7协\u8bae\u547d令: '" << msg << "'" << endl;

    }
}

void sendMsg(int epfd, int fd, std::string msg) {
    if (fd < 0 || msg.empty()) {
        return;
    }

    SendBuffer& buf = sendBuffers[fd];
    bool was_empty = buf.data.empty();

    // Append new message to buffer
    if (was_empty) {
        int len_net = htonl((int)msg.size());
        buf.data.insert(buf.data.end(), (char*)&len_net, (char*)&len_net + 4);
        buf.data.insert(buf.data.end(), msg.begin(), msg.end());
    } else {

        int len_net = htonl((int)msg.size());
        buf.data.insert(buf.data.end(), (char*)&len_net, (char*)&len_net + 4);
        buf.data.insert(buf.data.end(), msg.begin(), msg.end());
    }


    if (was_empty) {
        while (buf.sent_bytes < buf.data.size()) {
            ssize_t n = send(fd, buf.data.data() + buf.sent_bytes, buf.data.size() - buf.sent_bytes, 0);
            if (n > 0) {
                buf.sent_bytes += n;
            } else {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // 内核缓冲区满了. 注册EPOLLOUT事件.
                    cout << "[DEBUG] fd=" << fd << " 的内核发送缓冲区已满. 注册EPOLLOUT事件." << endl;
                    struct epoll_event temp;
                    temp.data.fd = fd;
                    temp.events = EPOLLIN | EPOLLET | EPOLLOUT;
                    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &temp);
                    return; // 退出并等待handleWriteEvent被调用
                }
                // 发生了其他错误
                cerr << "[ERROR] fd=" << fd << " 上发生send错误, errno=" << errno << endl;
                handleCloseEvent(epfd, fd);
                return;
            }
        }
    }

    // 如果代码执行到这里, 说明所有数据都立即发送成功了.
    if (buf.sent_bytes == buf.data.size()) {
        buf.data.clear();
        buf.sent_bytes = 0;
    }
}

