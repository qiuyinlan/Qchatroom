
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


#define MAX_MSG_LEN 10000

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



// 维护每个fd的发送缓冲区状态

// 新增：添加互斥锁保护缓冲区访问
#include <mutex>
std::mutex buffer_mutex;


// Definition of the global buffer maps. They are declared as extern in IO.h
unordered_map<int, RecvBuffer> recvBuffers;
unordered_map<int, SendBuffer> sendBuffers;

// 清理fd对应的缓冲区
void clearBuffers(int fd) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    recvBuffers.erase(fd);
    sendBuffers.erase(fd);
}

int recvMsg(int fd, string &msg) {
    // 加锁保护缓冲区访问
    std::lock_guard<std::mutex> lock(buffer_mutex);

    RecvBuffer& buf = recvBuffers[fd];
    // 每次调用都确保缓冲区初始化（关键修复）
    if (buf.header_read == 0 && buf.msg_len == 0) {
        buf.reset();
    }

    bool is_complete = false;
    bool has_new_data = false;  // 标记本次调用是否读取到新数据

    while (true) {
        if (buf.header_read < 4) {
            // 读取消息头
            ssize_t n = recv(fd, buf.header_buf + buf.header_read, 4 - buf.header_read, 0);
            if (n > 0) {
                has_new_data = true;
                buf.header_read += n;
                if (buf.header_read == 4) {
                    // 解析长度
                    buf.msg_len = ntohl(*((int*)buf.header_buf));
                    cout << "[DEBUG] 消息头解析完成，预期长度=" << buf.msg_len << endl;

                    if (buf.msg_len <= 0 || buf.msg_len > MAX_MSG_LEN) {
                        cerr << "[ERROR] 消息长度异常" << endl;
                        buf.reset();  // 重置缓冲区
                        return -1;
                    }
                    buf.msg_buf.resize(buf.msg_len);
                    buf.msg_read = 0;
                }
            } else if (n == 0) {
                buf.reset();  // 重置缓冲区
                return 0; // 连接关闭
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                buf.reset();  // 出错时重置
                return -1; // 错误
            }
        } else {
            // 读取消息体
            ssize_t n = recv(fd, buf.msg_buf.data() + buf.msg_read, buf.msg_len - buf.msg_read, 0);
            if (n > 0) {
                has_new_data = true;
                buf.msg_read += n;
                cout << "[DEBUG] 已读取消息体字节数=" << buf.msg_read
                     << ", 预期=" << buf.msg_len << endl;

                if (buf.msg_read == buf.msg_len) {
                    // 消息完整
                    msg.assign(buf.msg_buf.data(), buf.msg_len);
                    is_complete = true;
                    buf.reset();  // 重置缓冲区
                    break;
                }
            } else if (n == 0) {
                buf.reset();
                return 0; // 连接关闭
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                buf.reset();
                return -1; // 错误
            }
        }
    }

    if (is_complete) {
        cout << "[DEBUG] 消息完整，返回长度=" << msg.size() << endl;
        return (int)msg.size();
    } else {
        // 如果本次调用读取到了新数据，但消息还不完整，返回-2表示需要继续等待
        // 如果本次调用没有读取到任何新数据，说明需要等待更多数据，也返回-2
        if (has_new_data) {
            cout << "[DEBUG] 消息不完整但有新数据，返回-2" << endl;
        } else {
            cout << "[DEBUG] 无新数据可读，返回-2" << endl;
        }
        return -2;  // 使用-2表示数据不完整，避免与消息长度1冲突
    }
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



