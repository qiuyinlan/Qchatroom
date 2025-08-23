#include "FileTransfer.h"
#include <sys/socket.h>
#include "../utils/proto.h"
#include "../utils/IO.h"
#include "nlohmann/json.hpp"
#include <iostream>
#include <filesystem>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <thread>

using namespace std;

FileTransfer::FileTransfer() = default;

void FileTransfer::sendFile_Friend(int fd, const User& myUser, const User& targetUser) const {
    string filePath;
    struct stat fileStat;

    while (true) {
        cout << "请输入要发送文件的绝对路径 (输入'0'取消): ";
        getline(cin, filePath);
        if (cin.eof() || filePath == "0") {
            cout << "已取消发送。" << endl;
            return;
        }

        if (stat(filePath.c_str(), &fileStat) != 0 || !S_ISREG(fileStat.st_mode)) {
            cerr << "[错误] 文件无效或不是一个常规文件，请重新输入。" << endl;
        } else {
            break;
        }
    }

    string fileName = filesystem::path(filePath).filename().string();

    nlohmann::json req;
    req["flag"] = C2S_SEND_FILE_REQUEST;
    req["data"]["chat_type"] = "private";
    req["data"]["target_uid"] = targetUser.getUID();
    req["data"]["file_name"] = fileName;
    req["data"]["file_size"] = fileStat.st_size;

    sendMsg(fd, req.dump());

    // 启动一个新线程来处理服务器响应和文件数据发送
    thread([=]() {
        string response_str;
        if (recvMsg(fd, response_str) <= 0) {
            cerr << "[错误] 等待服务器响应超时或连接断开。" << endl;
            return;
        }

        try {
            auto res = nlohmann::json::parse(response_str);
            if (res["data"]["success"].get<bool>()) {
                cout << "[文件] 服务器准备就绪，开始发送 " << fileName << "..." << endl;
                ifstream file_stream(filePath, ios::binary);
                char buffer[4096];
                while (file_stream.read(buffer, sizeof(buffer)) || file_stream.gcount() > 0) {
                    if (send(fd, buffer, file_stream.gcount(), 0) < 0) {
                        perror("send");
                        break;
                    }
                }
                cout << "[文件] " << fileName << " 发送完成。" << endl;
            } else {
                cerr << "[错误] 服务器拒绝接收文件: " << res["data"].value("reason", "未知原因") << endl;
            }
        } catch (const nlohmann::json::parse_error& e) {
            cerr << "[错误] 解析服务器响应失败: " << e.what() << endl;
        }
    }).detach();
}

void FileTransfer::recvFile_Friend(int fd, const User& myUser) const {
    // 新逻辑下，接收文件的提示由主消息循环处理。
    // 此函数仅用于用户确认接收后，向服务器请求文件列表。
    cout << "正在查询可接收的文件列表..." << endl;
    // 实际的文件请求和接收逻辑需要与主循环和通知系统集成
    // 此处暂时留空，因为它的触发方式已经改变
}

void FileTransfer::sendFile_Group(int fd, const User& myUser, const Group& targetGroup) const {
    string filePath;
    struct stat fileStat;

    while (true) {
        cout << "请输入要发送到群聊的文件绝对路径 (输入'0'取消): ";
        getline(cin, filePath);
        if (cin.eof() || filePath == "0") {
            cout << "已取消发送。" << endl;
            return;
        }

        if (stat(filePath.c_str(), &fileStat) != 0 || !S_ISREG(fileStat.st_mode)) {
            cerr << "[错误] 文件无效或不是一个常规文件，请重新输入。" << endl;
        } else {
            break;
        }
    }

    string fileName = filesystem::path(filePath).filename().string();

    nlohmann::json req;
    req["flag"] = C2S_SEND_FILE_REQUEST;
    req["data"]["chat_type"] = "group";
    req["data"]["target_uid"] = targetGroup.getGroupUid();
    req["data"]["file_name"] = fileName;
    req["data"]["file_size"] = fileStat.st_size;

    sendMsg(fd, req.dump());

    thread([=]() {
        string response_str;
        if (recvMsg(fd, response_str) <= 0) {
            cerr << "[错误] 等待服务器响应超时或连接断开。" << endl;
            return;
        }

        try {
            auto res = nlohmann::json::parse(response_str);
            if (res["data"]["success"].get<bool>()) {
                cout << "[文件] 服务器准备就绪，开始发送到群聊 " << targetGroup.getGroupName() << "..." << endl;
                ifstream file_stream(filePath, ios::binary);
                char buffer[4096];
                while (file_stream.read(buffer, sizeof(buffer)) || file_stream.gcount() > 0) {
                    if (send(fd, buffer, file_stream.gcount(), 0) < 0) {
                        perror("send");
                        break;
                    }
                }
                cout << "[文件] " << fileName << " 发送完成。" << endl;
            } else {
                cerr << "[错误] 服务器拒绝接收文件: " << res["data"].value("reason", "未知原因") << endl;
            }
        } catch (const nlohmann::json::parse_error& e) {
            cerr << "[错误] 解析服务器响应失败: " << e.what() << endl;
        }
    }).detach();
}

void FileTransfer::recvFile_Group(int fd, const User& myUser, const std::string& G_uid) const {
    // 同样，此函数的逻辑也需要与新的通知和主循环集成
    cout << "正在查询群聊可接收的文件列表..." << endl;
}