#include <functional>
#include <iostream>
#include <map>
#include <cstdint>
#include <csignal>
#include <atomic>
#include <mutex>
#include "client.h"
#include "../utils/TCP.h"
#include "StartMenu.h"
#include "../utils/proto.h"
#include "OperationMenu.h"
#include "../service/Notifications.h"
#include <thread>

using namespace std;



    //全局变量——当前聊天对象
    
void signalHandler(int signum) {
    cout << "\n接收到信号 " << signum << "，客户端正在退出..." << endl;
    cout << "再见！" << endl;
    // 注意：这里无法向服务器发送断开通知，因为没有fd
    // 服务器会通过连接断开自动检测到客户端退出
    exit(signum);
}

// void start_UI() {
//     cout << "[1]登录    [2]邮箱注册    [3]重置密码    [4]找回密码    [5]退出" << endl;
//     cout << "请输入你的选择" << endl;
// }
void start_UI() {
    cout << "[1]登录    [2]邮箱注册    [0]退出" << endl;
    cout << "请输入你的选择" << endl;
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        IP = "10.30.0.146";
        PORT = 8000;
    } else if (argc == 3) {
        IP = argv[1];
        PORT = stoi(argv[2]);
    } else {
       cout << "请输入./client.out" << endl;
        return 1;
    }
    signal(SIGINT, signalHandler);
    User user;


    while (true) {
        start_UI();
        string option;
        getline(cin, option);
        if (option.empty()) {
            cout << "输入为空！" << endl;
            continue;
        }
        // if (option != "1" && option != "2" && option != "3" && option != "4" && option != "5") {
        //     cout << "没有这个选项！" << endl;
        //     continue;
        // }
         if (option != "1" && option != "2" &&  option != "0") {
            cout << "没有这个选项！" << endl;
            continue;
        }
        // if (option == "5") {
        //     cout << "退出成功" << endl;
        //     break;
        // }
        if (option == "0") {
            cout << "退出成功" << endl;
            break;
        }
        char *end_ptr;
        int opt = (int) strtol(option.c_str(), &end_ptr, 10);
        if (opt == 0 || option.find(' ') != std::string::npos) {
            std::cout << "输入格式错误 请重新输入" << std::endl;
            continue;
        }

        // 每次操作都重新建立连接
        int fd = Socket();
        Connect(fd, IP, PORT);
        if (opt == 1) {
            if (login(fd, user)) {
                clientOperation(fd, user);
            }
            close(fd);  // 登录完成后关闭连接
            continue;
        }
        if (opt == 2) {
            email_register(fd);
            close(fd);  // 注册完成后关闭连接
            continue;
        }
        if (opt == 3) {
            email_reset_password(fd);
            close(fd);  // 重置密码完成后关闭连接
            continue;
        }
        if (opt == 4) {
            email_find_password(fd);
            close(fd);  // 找回密码完成后关闭连接
            continue;
        }
    }
}
