
#include "IO.h"
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>
#include "User.h"

using namespace std;

//socket编程中，返回零，表示对端断开，关闭连接

//通用返回按钮
void return_last(){
    std::cout << "\033[90m输入【0】返回\033[0m" << std::endl;
}



int write_n (int fd, const char *msg, int n) {
    int n_written;
    int n_left = n;
    const char *ptr = msg;
    while (n_left > 0) {
        if ((n_written = send(fd, ptr, n_left, 0)) < 0) {
            if (n_written < 0 && errno == EINTR)
                continue;
            else
                return -1;
        } else if (n_written == 0) {
            continue;
        }
        ptr += n_written;
        n_left -= n_written;
    }
    return n;
}

int sendMsg(int fd, string msg) {
    if (fd < 0) { // fd 也可以是负数，表示无效文件描述符
        cout << "[ERROR] sendMsg: 无效文件描述符 fd=" << fd << endl;
        return -1;
    }
    if (msg.empty()) {
        cout << "[WARNING] sendMsg: 正在发送空消息" << endl;
        return 1;
    }



    // 验证消息长度
    if (msg.size() > 10000) {
        cout << "警告：消息过长 " << msg.size() << " 字节" << endl;
    }

    int ret;
    char *data = (char *) malloc(sizeof(char) * (msg.size() + 4));
    if (!data) {
        cout << "sendMsg: 内存分配失败" << endl;
        return -1;
    }

    int len = htonl(msg.size());
    memcpy(data, &len, 4);
    memcpy(data + 4, msg.c_str(), msg.size());
    ret = write_n(fd, data, msg.size() + 4);
    free(data);

    if (ret < 0) {
        perror("sendMsg error");
        close(fd);
        return -1;
    }

    if (ret != (int)(msg.size() + 4)) {
        cout << "发送字节数不匹配：期望 " << (msg.size() + 4) << "，实际 " << ret << endl;
    }
    return ret;
}

int read_n(int fd, char *msg, int n) {
    int n_left = n;
    int n_read;
    char *ptr = msg;
    while (n_left > 0) {
        if ((n_read = recv(fd, ptr, n_left, 0)) < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK) {
                n_read = 0;  // 继续尝试
            } else {
                return -1;  // 真正的错误
            }
        } else if (n_read == 0) {
            // 对端关闭连接，但我们还没读完所需的数据
            if (n_left == n) {
                // 一开始就没读到数据，返回0表示连接关闭
                return 0;
            } else {
                // 读到了部分数据但连接关闭了，这是错误情况
                cout << "连接意外关闭，期望读取 " << n << " 字节，实际读取 " << (n - n_left) << " 字节" << endl;
                return -1;
            }
        }
        ptr += n_read;
        n_left -= n_read;
    }
    return n - n_left;  // 应该等于 n
}

int recvMsg(int fd, string &msg) {
    int len = 0;
    // 使用read_n确保完整接收4字节长度字段
    int header_ret = read_n(fd, (char *) &len, 4);
    if (header_ret != 4) {
        if (header_ret <= 0) {
            return 0;  // 连接断开
        }
        return -1;  // 接收错误
    }

    // 网络字节序转换
    len = ntohl(len);
    if (len == 0) {
        msg.clear();  // 如果长度为0，返回空字符串
        return 1;    // 表示成功接收了一个空消息
    }
    // 检查消息长度是否合理
    if (len < 0 || len > 10000) {
        return -1;  // 异常长度
    }
    
    char *buffer = new char[len + 1];
    memset(buffer, 0, len + 1);

    // 使用read_n确保完整接收消息内容
    int content_ret = read_n(fd, buffer, len);
    if (content_ret != len) {
        delete[] buffer;
        return -1;
    }

    msg = string(buffer, len);  // 指定长度，避免字符串截断
    delete[] buffer;
    return len;
}

