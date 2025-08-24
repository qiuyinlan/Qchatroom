#include "StartMenu.h"
#include <iostream>
#include <cstdint>
#include "../utils/IO.h"
#include "../utils/proto.h"
#include "OperationMenu.h"
#include "User.h"
#include <termios.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include "../service/Notifications.h"

using namespace std;
using json = nlohmann::json;

bool isNumber(const string &input) {
    for (char c: input) {
        if (!isdigit(c)) {
            return false;
        }
    }
    return true;
}

char getch() {
    char ch;
    struct termios tm, tm_old;
    tcgetattr(STDIN_FILENO, &tm);
    tm_old = tm;
    tm.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &tm);
    while ((ch = getchar()) == EOF) {
        clearerr(stdin);
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &tm_old);
    return ch;
}


void get_password(const string& prompt, string &password) {
    char ch;
    password.clear();
    cout << prompt;
    while ((ch = getch()) != '\n') {
        if (ch == '\b' || ch == 127) {
            if (!password.empty()) {
                cout << "\b \b";
                password.pop_back();
            }
        } else {
            password.push_back(ch);
            cout << '*';
        }
    }
    cout << endl;
}

int login(int fd, User &user) {
    string email;
    string passwd;

    // 1. 在客户端本地一次性收集完所有信息
    while (true) {
        std::cout << "请输入你的邮箱(输入0返回)：" << std::endl;
        getline(cin, email);
        if (email == "0") return 0;
        if (email.empty()) {
            cout << "邮箱不能为空，请重新输入。" << endl;
            continue;
        }
        if (email.find(' ') != string::npos) {
            cout << "邮箱不能包含空格，请重新输入。" << endl;
            continue;
        }
        break;
    }

    get_password("请输入你的密码: ", passwd);
    if (passwd.empty()) {
        cout << "密码不能为空，操作取消。" << endl;
        return 0;
    }

    // 2. 构建符合新协议的JSON请求
    json request_json;
    request_json["flag"] = C2S_LOGIN_REQUEST;
    request_json["data"]["email"] = email;
    request_json["data"]["password"] = passwd;

    // 3. 一次性发送完整的请求
    sendMsg(fd, request_json.dump());

    // 4. 只接收一次最终的服务器响应
    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "与服务器断开连接..." << endl;
        return 0;
    }

    // 5. 解析服务器的JSON响应
    try {
        json response_json = json::parse(response_str);
        int flag = response_json.value("flag", 0);

        if (flag == S2C_LOGIN_SUCCESS) {
            cout << "登录成功!" << endl;
            // 从响应的data字段中解析用户信息
            user.json_parse(response_json["data"].dump());
            cout << "欢迎您：【" << user.getUsername() << "】" << endl;
            return 1; // 返回1表示登录成功
        } else {
            // 登录失败，打印出服务器返回的原因
            string reason = response_json["data"].value("reason", "未知错误");
            cout << "登录失败: " << reason << endl;
            return 0; // 返回0表示登录失败
        }
    } catch (const json::parse_error& e) {
        cerr << "[ERROR] 解析服务器响应失败: " << e.what() << "\n响应原文: " << response_str << endl;
        return 0;
    }
}

int email_register(int fd) {
    string email, code, username, password, password2, server_reply;
    string prompt = "请输入你的邮箱: ";
    while(true){
        cout << prompt << endl;
        std::getline(std::cin, email);
        if (email.empty()) {
            std::cout << "邮箱不能为空，请重新输入！" << std::endl;
            continue;
        } else if (email == "0") {
            sendMsg(fd, "0");
            return 0;
        }else {
            //不为空，就让服务器检查一下redis
            sendMsg(fd, REQUEST_CODE);
            sendMsg(fd, email);
            //看邮箱是否重复

            recvMsg(fd, server_reply);
            if(server_reply == "邮箱已存在"){
                prompt = "该邮箱已注册，请重新输入邮箱（输入0返回）：";
                continue;
            }
        }
        break;

    }
    prompt = "请输入你的用户名: ";
    while(true){
        cout << prompt << endl;
        getline(cin, username);
        if (username.empty()) {
            cout << "用户名不能为空！" << endl;
            continue;
        }else if (username == "0") {
            sendMsg(fd, "0");
            return 0;
        }else {
            sendMsg(fd, username);
            //看是否重复

            recvMsg(fd, server_reply);
            if(server_reply == "已存在"){
                prompt = "该用户名已注册，请重新输入（输入0返回）：";
                continue;
            }
        }
        break;

    }

    // 邮箱不重复，验证码发
    cout << "按回车获取验证码..." << endl;
    cin.ignore(INT32_MAX, '\n');

    recvMsg(fd, server_reply);
    cout << server_reply << endl;//验证码发送是否成功的消息
    if (server_reply.find("失败") != string::npos) return 0;


    while (true) {
        cout << "请输入收到的验证码: ";
        std::getline(std::cin, code);
        if (code.empty()) {
            cout << "验证码不能为空！" << endl;
        }else {
            break;
        }
    }


    get_password("请输入你的密码: ", password);
    while (true) {

        get_password("请再次输入你的密码: ", password2);
        if (password != password2) {
            cout << "两次密码不一致！请重新输入。" << endl;

            get_password("请输入你的密码: ", password);
            continue;
        }
        break;
    }
    // 组装JSON
    json root;
    root["email"] = email;
    root["code"] = code;
    root["username"] = username;
    root["password"] = password;
    string json_str = root.dump();
    sendMsg(fd, REGISTER_WITH_CODE);

    sendMsg(fd, json_str);

    recvMsg(fd, server_reply);
    cout << server_reply << endl;
    return server_reply == "注册成功";
}

// int email_reset_password(int fd) {
//     string email, code, password, password2, server_reply;
//     cout << "请输入你的邮箱: ";
//     getline(cin, email);
//     if (email.empty()) {
//         cout << "邮箱不能为空！" << endl;
//         return 0;
//     }
//     // 获取验证码
//     cout << "按回车获取验证码..." << endl;
//     cin.ignore(INT32_MAX, '\n');
//     sendMsg(fd, REQUEST_RESET_CODE);
//     sendMsg(fd, email);
//     recvMsg(fd, server_reply);
//     cout << server_reply << endl;
//     if (server_reply.find("失败") != string::npos) return 0;
//     // 优化：循环输入验证码，不能为空
//     while (true) {
//         cout << "请输入收到的验证码: ";
//         getline(cin, code);
//         if (code.empty()) {
//             cout << "验证码不能为空！" << endl;
//             continue;
//         }
//         break;
//     }

//     get_password("请输入新密码: ", password);
//     get_password("请再次输入新密码: ", password2);
//     if (password != password2) {
//         cout << "两次密码不一致！" << endl;
//         return 0;
//     }
//     // 组装JSON
//     json root;
//     root["email"] = email;
//     root["code"] = code;
//     root["password"] = password;
//     string json_str = root.dump();
//     sendMsg(fd, RESET_PASSWORD_WITH_CODE);
//     sendMsg(fd, json_str);
//     recvMsg(fd, server_reply);
//     cout << server_reply << endl;
//     return server_reply == "密码重置成功";
// }

// int email_find_password(int fd) {
//     string email, code, server_reply;


//     while (true) {
//         cout << "请输入你的邮箱: ";
//         getline(cin, email);
//         if (email.empty()) {
//             cout << "邮箱不能为空！" << endl;
//             continue;
//         }
//         break;
//     }
//     // 获取验证码
//     cout << "按回车获取验证码..." << endl;
//     cin.ignore(INT32_MAX, '\n');
//     sendMsg(fd, REQUEST_RESET_CODE); // 复用请求验证码的协议
//     sendMsg(fd, email);
//     recvMsg(fd, server_reply);
//     cout << server_reply << endl;
//     if (server_reply.find("失败") != string::npos) return 0;
//     // 循环输入验证码，不能为空


// }
void clientRegisterWithCode(int fd) {
    string email, username, password, code, response;

    cout << "请输入邮箱: ";
    getline(cin, email);
    if (email.empty()) {
        cout << "邮箱不能为空。" << endl;
        return;
    }

    cout << "请输入用户名: ";
    getline(cin, username);
    if (username.empty()) {
        cout << "用户名不能为空。" << endl;
        return;
    }

    // 1. 发送邮箱和用户名到服务器，请求验证码
    nlohmann::json req_code;
    req_code["flag"] = C2S_REQUEST_CODE;
    req_code["data"]["email"] = email;
    req_code["data"]["username"] = username;
    sendMsg(fd, req_code.dump());

    // 2. 接收服务器的响应
    if (recvMsg(fd, response) <= 0) {
        cout << "与服务器通信失败。" << endl;
        return;
    }

    cout << "服务器响应: " << response << endl;
    if (response != "验证码已发送，请查收邮箱") {
        return; // 如果服务器返回任何错误信息，则终止注册流程
    }

    // 3. 输入验证码和密码
     while (true) {
        cout << "请输入收到的验证码: ";
        getline(cin, code);
        if (code.empty()) {
            cout << "验证码不能为空！" << endl;
            continue;
        }
        break;
    }
    string password2;
    while (true) {

        get_password("请输入你的密码: ", password2);
        get_password("请再次输入你的密码: ", password);
         if (password != password2) {
            cout << "两次密码不一致！请重新输入。" << endl;
            continue;
         }
        
        break;
    }

    // 4. 发送所有信息到服务器进行最终注册
    nlohmann::json req_register;
    req_register["flag"] = C2S_REGISTER_WITH_CODE;
    req_register["data"]["email"] = email;
    req_register["data"]["username"] = username;
    req_register["data"]["password"] = password2;
    req_register["data"]["code"] = code;
    sendMsg(fd, req_register.dump());

    // 5. 接收最终的注册结果
    if (recvMsg(fd, response) <= 0) {
        cout << "与服务器通信失败。" << endl;
        return;
    }

    cout << "服务器响应: " << response << endl;
}

