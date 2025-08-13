#include "../utils/TCP.h"
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



using namespace std;

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
    timeout.tv_sec = 40;
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
    
    int epfd = epoll_create(1024);

    struct epoll_event temp, ep[1024];
    
    
    temp.data.fd = listenfd;
    temp.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &temp);

    while (true) {
        ret = epoll_wait(epfd, ep, 1024, -1);
        for (int i = 0; i < ret; i++) {
            //如果不是读事件，继续循环
            if (!(ep[i].events & EPOLLIN))
                continue;

            int fd = ep[i].data.fd;

            //listenfd变成可读状态，代表有新客户端连接上来了
            if (ep[i].data.fd == listenfd) {
                struct sockaddr_in cli_addr;
                memset(&cli_addr, 0, sizeof(cli_addr));
                socklen_t cli_len = sizeof(cli_addr);

                //accept
                int connfd = Accept(listenfd, (struct sockaddr *) &cli_addr, &cli_len);
                


                cout << "received from " << inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, str, sizeof(str))
                     << " at port " << ntohs(cli_addr.sin_port) << endl;
                cout << "cfd " << connfd << "-----client " << ++num << endl;

                int flag = fcntl(connfd, F_GETFL);
                flag |= O_NONBLOCK;
                fcntl(connfd, F_SETFL, flag);

                temp.events = EPOLLIN;
                temp.data.fd = connfd;
               
        
                //启用 TCP keepalive 
                int keep_alive = 1;
                //表示在连接空闲n秒后开始发送 TCP keepalive 探测包。
                int keep_idle = 30;

                //表示在发送第一个 TCP keepalive 探测包后，每隔n秒发送一个探测包。
                int keep_interval = 30;
                //表示如果在发送n个 TCP keepalive 探测包后仍然没有收到响应，连接将被关闭。
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
            //数据连接
              else {
                int recv_ret = recvMsg(fd, msg);

                if (recv_ret <= 0 || msg.empty()) {
                    // 连接断开，从epoll中移除
                    cout << "[INFO] 客户端 " << fd << " 断开连接" << endl;
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    continue;
                }

                if (msg == LOGIN) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ serverLogin(epfd, fd); });
                } 
                //额外的连接
                else if (msg == "HEARTBEAT") {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ hearbeat(epfd, fd); });
                }
                else if (msg == UNIFIED_RECEIVER) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ handleUnifiedReceiver(epfd, fd); });
                } else if (msg == SENDFILE_F) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ sendFile_Friend(epfd, fd); });
                } else if (msg == RECVFILE_F) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ recvFile_Friend(epfd, fd); });
                } else if (msg == SENDFILE_G) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ sendFile_Group(epfd, fd); });
                } else if (msg == RECVFILE_G) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ recvFile_Group(epfd, fd); });
                }
                //正常请求
                else if (msg == REQUEST_CODE) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);//发验证码，发前检查邮箱
                    pool.addTask([=](){ handleRequestCode(epfd, fd); });
                } else if (msg == REGISTER_WITH_CODE) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ serverRegisterWithCode(epfd, fd); });//注册
                } else if (msg == REQUEST_RESET_CODE) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ handleResetCode(epfd, fd); });
                } else if (msg == RESET_PASSWORD_WITH_CODE) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ resetPasswordWithCode(epfd, fd); });
                } else if (msg == FIND_PASSWORD_WITH_CODE) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep[i].data.fd, nullptr);
                    pool.addTask([=](){ findPasswordWithCode(epfd, fd); });
                }
                else{
                    cout << "[DEBUG] 未知协议: '" << msg << "' (长度: " << msg.length() << ")" << endl;
                }
            }
        }
    }
}

