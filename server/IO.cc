
#include "IO.h"
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
using namespace std;

//socket编程中，返回零，表示对端断开，关闭连接，两边的阻塞/非阻塞是不一样的


using namespace std;

#define MAX_MSG_LEN 10000

// 维护每个fd的接收缓冲区状态
struct RecvBuffer {
    char header_buf[4];
    int header_read = 0;
    int msg_len = 0;
    vector<char> msg_buf;
    int msg_read = 0;
};

// 维护每个fd的发送缓冲区状态
struct SendBuffer {
    vector<char> data;
    size_t sent_bytes = 0;
};

// 模拟外部存储，每个fd对应一个接收和发送缓冲区
unordered_map<int, RecvBuffer> recvBuffers;
unordered_map<int, SendBuffer> sendBuffers;

// 你的消息处理函数，收到完整消息后调用
void process_message(int fd, const string& msg) {
    cout << "[INFO] 收到完整消息，fd=" << fd << " 内容长度=" << msg.size() << endl;
    // 这里根据你的业务写处理逻辑
}

// ET模式下非阻塞读，调用多次直到返回EAGAIN
int recvMsg(int fd, string &msg) {
    RecvBuffer& buf = recvBuffers[fd];
    while (true) {
        if (buf.header_read < 4) {
            ssize_t n = recv(fd, buf.header_buf + buf.header_read, 4 - buf.header_read, 0);
            if (n > 0) {
                buf.header_read += n;
                if (buf.header_read == 4) {
                    buf.msg_len = ntohl(*((int*)buf.header_buf));
                    if (buf.msg_len <= 0 || buf.msg_len > MAX_MSG_LEN) {
                        cerr << "[ERROR] 消息长度异常: " << buf.msg_len << endl;
                        return -1;
                    }
                    buf.msg_buf.resize(buf.msg_len);
                    buf.msg_read = 0;
                }
            } else if (n == 0) {
                // 对端关闭
                return 0;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                perror("recv error");
                return -1;
            }
        } else {
            // 读消息体
            ssize_t n = recv(fd, buf.msg_buf.data() + buf.msg_read, buf.msg_len - buf.msg_read, 0);
            if (n > 0) {
                buf.msg_read += n;
                if (buf.msg_read == buf.msg_len) {
                    // 完整消息接收完成
                    msg.assign(buf.msg_buf.data(), buf.msg_len);
                    // 重置状态准备下一条消息
                    buf.header_read = 0;
                    buf.msg_len = 0;
                    buf.msg_buf.clear();
                    buf.msg_read = 0;
                    return (int)msg.size();
                }
            } else if (n == 0) {
                return 0;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                perror("recv error");
                return -1;
            }
        }
    }
    // 本次没收到完整消息，返回1表示继续等待
    return 1;
}

// ET模式下非阻塞写，调用多次直到写完或返回EAGAIN
int sendMsg(int fd, string msg) {
    if (fd < 0) {
        cerr << "[ERROR] sendMsg: 无效fd=" << fd << endl;
        return -1;
    }
    if (msg.empty()) {
        cout << "[WARNING] sendMsg: 空消息" << endl;
        return 1;
    }

    SendBuffer& buf = sendBuffers[fd];

    if (buf.data.empty()) {
        // 构造发送缓冲区（长度 + 数据）
        int len_net = htonl((int)msg.size());
        buf.data.resize(4 + msg.size());
        memcpy(buf.data.data(), &len_net, 4);
        memcpy(buf.data.data() + 4, msg.data(), msg.size());
        buf.sent_bytes = 0;
    }

    while (buf.sent_bytes < buf.data.size()) {
        ssize_t n = send(fd, buf.data.data() + buf.sent_bytes, buf.data.size() - buf.sent_bytes, 0);
        if (n > 0) {
            buf.sent_bytes += n;
        } else if (n == 0) {
            continue;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 缓冲区满，下次写事件再继续发送
                break;
            }
            if (errno == EINTR) continue;
            perror("send error");
            return -1;
        }
    }

    if (buf.sent_bytes == buf.data.size()) {
        // 发送完成，清空缓冲
        buf.data.clear();
        buf.sent_bytes = 0;
        return (int)msg.size() + 4;
    }
    // 发送未完成，等待下次写事件继续
    return 1;
}




int read_n(int fd, char *msg, int n) {
    int n_left = n;
    int n_read;
    char *ptr = msg;
    //循环保证接收完整
    while (n_left > 0) {
         n_read = recv(fd, ptr, n_left, 0);
         //成功
        if (n_read > 0){
            ptr += n_read;
            n_left -= n_read;
        } else if (n_read == 0) {
            // 对端关闭连接
            return 0;
        } else {
            if (errno == EINTR) {
                // 中断信号，继续读
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没数据了，正常退出循环
                break;
            } else {
                // 其他错误
                return -1;
            }
        }
    }

    //正常退出
    return n - n_left;  // 应等于 n
}
int write_n (int fd, const char *msg, int n) {
    int n_written;
    int n_left = n;
    const char *ptr = msg;
    while (n_left > 0) {      
         ssize_t n_written = send(fd, ptr, n_left, 0);
        if (n_written > 0) {
            ptr += n_written;
            n_left -= n_written;
        } else if (n_written == 0) {
            // send返回0一般不会出现
            continue;
        } else {
            if (errno == EINTR) {
                continue; // 信号中断，重试
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                return -1; // 其他错误
            }
        }
    }
    return n - n_left; // 返回实际写入字节数
}