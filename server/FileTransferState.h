#ifndef FILETRANSFERSTATE_H
#define FILETRANSFERSTATE_H

#include <string>
#include <fstream>

// 定义文件传输的状态
enum class TransferStatus {
    NONE,
    RECEIVING, // 服务器正在接收文件
    SENDING    // 服务器正在发送文件
};

struct FileTransferState {
    TransferStatus status = TransferStatus::NONE;
    std::string file_name;
    std::string file_path;
    long long file_size = 0;
    long long transferred_bytes = 0;
    std::string sender_uid;
    std::string receiver_uid;
    std::string chat_type; // "private" or "group"
    std::string group_uid; // if chat_type is "group"
    std::ofstream file_stream; // 用于接收文件
    std::ifstream read_stream; // 用于发送文件
    int target_fd = -1; // 发送文件时，目标客户端的fd
};

#endif // FILETRANSFERSTATE_H

