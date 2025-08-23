#include "FileHandlers.h"
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

