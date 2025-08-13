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
#include "../utils/IO.h"

using namespace std;

// 显式声明，，避免链接问题
extern int sendMsg(int fd, std::string msg);
extern int recvMsg(int fd, std::string &msg);

// ET，记录等待JSON数据的fd
unordered_set<int> waiting_for_json;
mutex waiting_mutex;




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

//超时，映射

void hearbeat(int epfd, int fd) {
    Redis redis;
    redis.connect();

    // 将心跳连接切换为阻塞，才能正确依赖 SO_RCVTIMEO
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    // 设置阻塞读超时 40s
    struct timeval timeout;
    timeout.tv_sec = 20;
    timeout.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
    }

    // 先接收客户端UID（阻塞版）
    string uid;
    if (recvMsg(fd, uid) <= 0) {
        cout << "[ERROR] 接收心跳UID失败，关闭连接" << endl;
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        return;
    }

    while (true) {
        string buf;
        int len = recvMsg(fd, buf); // 阻塞读取，受 SO_RCVTIMEO 影响
        if (len <= 0) {
            // 超时或连接断开，进行清理
            cout << "[INFO] 心跳连接异常/超时，uid=" << uid << "，关闭连接" << endl;

            // 关闭对应的统一接收连接（若存在且合法）
            string data_fd_str = redis.hget("unified_receiver", uid);
            bool ok_num = !data_fd_str.empty() && all_of(data_fd_str.begin(), data_fd_str.end(), ::isdigit);
            if (ok_num) {
                int data_fd = stoi(data_fd_str);
                
                cout << "[清理] 关闭统一接收连接 fd=" << data_fd << endl;
                epoll_ctl(epfd, EPOLL_CTL_DEL, data_fd, nullptr);
                close(data_fd);
                redis.hdel("unified_receiver", uid);
            } else {
                cout << "[WARN] 未找到统一接收连接或值非法，uid=" << uid << endl;
            }

            redis.hdel("is_online", uid);
            break;
        }

        if (buf == "HEARTBEAT") {
            // 发送心跳响应（阻塞）
            if (sendMsg(fd, "HEARTBEAT_ACK") < 0) {
                cout << "[ERROR] 心跳响应发送失败，uid=" << uid << endl;

                // 同样清理统一接收连接
                string data_fd_str = redis.hget("unified_receiver", uid);
                bool ok_num = !data_fd_str.empty() && all_of(data_fd_str.begin(), data_fd_str.end(), ::isdigit);
                if (ok_num) {
                    int data_fd = stoi(data_fd_str);
                    cout << "[清理] 关闭统一接收连接 fd=" << data_fd << endl;
                    epoll_ctl(epfd, EPOLL_CTL_DEL, data_fd, nullptr);
                    close(data_fd);
                    redis.hdel("unified_receiver", uid);
                }
                redis.hdel("is_online", uid);
                break;
            }
        }
    }

    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
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
    

    while (true) {
        ret = epoll_wait(epfd, ep, 1024, -1);
        for (int i = 0; i < ret; i++) {
            //如果不是读事件，继续循环
            if (!(ep[i].events & EPOLLIN))
                continue;

            int fd = ep[i].data.fd;

            //listenfd变成可读状态，代表有新客户端连接上来了
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
                    cout << "[DEBUG] recvMsg返回值: " << recv_ret << ", 消息内容: '" << msg << "'" << endl;

                    if (recv_ret == -2) {
                        // 说明数据还没读完，ET模式下需要等待下次EPOLLIN事件
                        // 不要继续循环，直接退出等待更多数据
                        cout << "[DEBUG] 数据不完整，等待下次事件" << endl;
                        break;
                    }
                    else if (recv_ret == 0) {
                        // 连接关闭，退出循环，清理资源
                        cout << "[INFO] 客户端 " << fd << " 断开连接" << endl;
                        redis.hdel("is_online", to_string(fd));
                        redis.hdel("unified_receiver", to_string(fd));
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        break;
                    }
                    else if (recv_ret > 0) {
                        cout << "[DEBUG] 收到完整消息: " << msg << ", 长度: " << recv_ret << endl;
                        
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
                            cout << "[DEBUG] 收到LOGIN协议，移交给线程池处理" << endl;
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
                        else if (msg == "HEARTBEAT") {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            pool.addTask([=](){
                                hearbeat(epfd, fd);
                            });
                            break;
                        } else if (msg == UNIFIED_RECEIVER) {
                            // 保持非阻塞模式，继续走ET读写
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            pool.addTask([=](){ handleUnifiedReceiver(epfd, fd); });
                            break;
                        } else if (msg == SENDFILE_F) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 文件传输处理使用阻塞IO
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ sendFile_Friend(epfd, fd); });
                            break;
                        } else if (msg == RECVFILE_F) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 文件传输处理使用阻塞IO
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ recvFile_Friend(epfd, fd); });
                            break;
                        } else if (msg == SENDFILE_G) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 文件传输处理使用阻塞IO
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
                            // 此分支内部使用阻塞IO
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ handleRequestCode(epfd, fd); });
                            break;
                        } else if (msg == REGISTER_WITH_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 此分支内部使用阻塞IO
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ serverRegisterWithCode(epfd, fd); });//注册
                            break;
                        } else if (msg == REQUEST_RESET_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 此分支内部使用阻塞IO
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ handleResetCode(epfd, fd); });
                            break;
                        } else if (msg == RESET_PASSWORD_WITH_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 此分支内部使用阻塞IO
                            int _flags = fcntl(fd, F_GETFL, 0);
                            if (_flags != -1) {
                                fcntl(fd, F_SETFL, _flags & ~O_NONBLOCK);
                            }
                            pool.addTask([=](){ resetPasswordWithCode(epfd, fd); });
                            break;
                        } else if (msg == FIND_PASSWORD_WITH_CODE) {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                            // 此分支内部使用阻塞IO
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
                
