#include "../utils/TCP.h"
#include "IO.h"
#include "../utils/IO.h"
#include "LoginHandler.h"
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>
#include <csignal>
#include "ThreadPool.hpp"
#include "proto.h"
#include "Redis.h"
#include "Transaction.h"
#include <curl/curl.h>
#include <string>
#include <User.h>
#include <thread>
#include <unordered_set>
#include <mutex>
#include <random>
#include <unordered_map>
#include <ctime>
#include "../utils/IO.h"

using namespace std;

extern int sendMsg(int fd, std::string msg);
extern int recvMsg(int fd, std::string &msg);

// ET，记录等待JSON数据的fd
unordered_set<int> waiting_for_json;
mutex waiting_mutex;

// 心跳，因为fd值会被复用，所以一定要引入uid,用户唯一标识
unordered_map<string, time_t> last_activity;  // 记最后活跃
unordered_map<int, string> fd_to_uid;         // fd到uid的映射
mutex activity_mutex;  


// 更新uid活跃时间
void updateUserActivity(const string& uid) {
    lock_guard<mutex> lock(activity_mutex);
    last_activity[uid] = time(nullptr);
}

// 添加fd到uid的映射，写成单独函数，可以更好地控制锁
void addFdToUid(int fd, const string& uid) {
    lock_guard<mutex> lock(activity_mutex);
    fd_to_uid[fd] = uid;
    last_activity[uid] = time(nullptr);//unix时间戳
    cout << "[心跳]  fd=" << fd << " -> uid=" << uid << endl;
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
            //如果不是读事件，继续循环
            if (!(ep[i].events & EPOLLIN))
                continue;

            int fd = ep[i].data.fd;
            
            //listenfd变成可读状态，代表有新客户端连接上来了
            if (fd == listenfd) {
                //循环一定要有退出的判断EAGAIN
                while (true) {
                    struct sockaddr_in cli_addr;
                    memset(&cli_addr, 0, sizeof(cli_addr));
                    socklen_t cli_len = sizeof(cli_addr);

                    //accept
                    int connfd = Accept(listenfd, (struct sockaddr *) &cli_addr, &cli_len);
                    if (connfd < 0){
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        } else if (errno == EINTR) {
                            continue;
                        } else {
                            perror("accept");
                            break;
                        }
                    }
                    cout << "received from " << inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, str, sizeof(str))
                        << " at port " << ntohs(cli_addr.sin_port) << endl;
                    cout << "connectfd " << connfd << "-----client " << ++num << endl;

                    int flag = fcntl(connfd, F_GETFL);
                    flag |= O_NONBLOCK;
                    fcntl(connfd, F_SETFL, flag);

                    temp.events = EPOLLIN | EPOLLET;
                    temp.data.fd = connfd;
                
                    //TCP keepalive 
                    int keep_alive = 1;
                    int keep_idle = 30;
                    int keep_interval = 30;
                    int keep_count = 15;
                    setsockopt(connfd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive));
                    setsockopt(connfd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
                    setsockopt(connfd, SOL_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval));
                    setsockopt(connfd, SOL_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count));
                
                    //将connfd加入监听红黑树
                    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &temp);
                    if (ret < 0) {
                        sys_err("epoll_ctl error");
                    }

                }
                
            }
            //数据连接
              else {
                msg.clear();
                while (true) {
                    int recv_ret = recvMsgET(fd, msg);
                   // cout << "[DEBUG] recvMsg返回值: " << recv_ret << ", 消息内容: '" << msg << "'" << endl;

                    if (recv_ret == -2) {
                        // 说明数据还没读完，ET模式下需要等待下次EPOLLIN事件
                        // 不要继续循环，直接退出等待更多数据
                       // cout << "[DEBUG] 数据不完整，等待下次事件" << endl;
                        break;
                    }
                    else if (recv_ret == 0) {
                        // 连接关闭，退出循环，清理资源
                        cout << "[INFO] 客户端 " << fd << " 断开连接" << endl;

                        // 获取对应的uid并清理
                        string uid = getUidByFd(fd);
                        if (!uid.empty()) {
                            redis.hdel("is_online", uid);
                            redis.hdel("unified_receiver", uid);
                            removeUserActivity(uid);
                        }

                        removeFdMapping(fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        break;
                    }
                    else if (recv_ret > 0) {
                        //cout << "[DEBUG] 收到完整消息: " << msg << ", 长度: " << recv_ret << endl;

                        // 首先检查是否是统一接收连接的心跳消息
                        if (msg == "HEARTBEAT") {
                            string uid_for_heartbeat = getUidByFd(fd);

                            cout << "[心跳] 收到心跳消息，fd=" << fd << ", uid=" << uid_for_heartbeat << endl;
                           // sendMsg(fd, "HEARTBEAT_ACK");

                            // 更新活跃时间
                            if (!uid_for_heartbeat.empty()) {
                                updateUserActivity(uid_for_heartbeat);
                                //notify(fd, uid_for_heartbeat);
                            }
                            continue;
                        }

                        //切换成阻塞模式
                        // 首先检查JSON登录数据
                        bool is_waiting_for_json = false;
                        {
                            lock_guard<mutex> lock(waiting_mutex);
                            is_waiting_for_json = waiting_for_json.count(fd) > 0;
                        }

                        if (is_waiting_for_json) {
                            // 这应该是登录的JSON数据
                            cout << "[DEBUG] 接收到等待中的登录JSON数据: " << msg << endl;
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            clearBuffers(fd);
                            {
                                lock_guard<mutex> lock(waiting_mutex);
                                waiting_for_json.erase(fd);
                            }
                            pool.addTask([=](){ serverLoginWithData(epfd, fd, msg); });
                            break;
                        }
                        else if (msg == LOGIN) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            clearBuffers(fd);

                            // 进入处理函数前将 socket 切换为阻塞模式（ET 事件循环中保持非阻塞）
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }

                            pool.addTask([=](){
                                serverLogin(epfd, fd);
                            });
                            break;
                        }
                        //额外的连接

                        //1.通知和心跳
                        else if (msg == UNIFIED_RECEIVER) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            pool.addTask([=](){ handleUnifiedReceiver(epfd, fd); });
                            break;
                        } else if (msg == SENDFILE_F) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 阻塞IO
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ sendFile_Friend(epfd, fd); });
                            break;
                        } else if (msg == RECVFILE_F) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
        
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ recvFile_Friend(epfd, fd); });
                            break;
                        } else if (msg == SENDFILE_G) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ sendFile_Group(epfd, fd); });
                            break;
                        } else if (msg == RECVFILE_G) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 文件传输处理使用阻塞IO
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ recvFile_Group(epfd, fd); });
                            break;
                        }
                        //正常请求
                        else if (msg == REQUEST_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);//发验证码，发前检查邮箱
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ handleRequestCode(epfd, fd); });
                            break;
                        } else if (msg == REGISTER_WITH_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ serverRegisterWithCode(epfd, fd); });//注册
                            break;
                        } else if (msg == REQUEST_RESET_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ handleResetCode(epfd, fd); });
                            break;
                        } else if (msg == RESET_PASSWORD_WITH_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ resetPasswordWithCode(epfd, fd); });
                            break;
                        } else if (msg == FIND_PASSWORD_WITH_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ findPasswordWithCode(epfd, fd); });
                            break;
                        }
                        else{
                            cout << "[DEBUG] 未知协议: '" << msg << "' (长度: " << msg.length() << ")" << endl;
                        }
                        continue;
                    }
                    else {
                        // 出错（recv_ret < 0 且不是 -2）
                        cerr << "[ERROR] recvMsgET error fd=" << fd << ", ret=" << recv_ret << endl;
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        break;
                    }
                }
            }
        }
    }
}
                
