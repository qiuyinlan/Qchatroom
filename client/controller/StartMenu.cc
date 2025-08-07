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
    sendMsg(fd, LOGIN);
    string email;
    string passwd;
    while (true) {
        std::cout << "请输入你的邮箱：" << std::endl;
        getline(cin, email);
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
    while (true) {
        get_password("请输入你的密码: ", passwd);
        if (passwd.empty()) {
            cout << "密码不能为空，请重新输入。" << endl;
            continue;
        }
        if (passwd.find(' ') != string::npos) {
            cout << "密码不能包含空格，请重新输入。" << endl;
            continue;
        }
        break;
    }
    //发
    LoginRequest loginRequest(email, passwd);
    sendMsg(fd, loginRequest.to_json());
    string buf;
    recvMsg(fd, buf);
    if (buf == "-1") {
        cout << "账号不存在" << endl;
        return 0;
    } else if (buf == "-2") {
        cout << "密码错误" << endl;
        int choice;
        while (true) {
            cout << "请选择：[1]重试登录  [0]返回首页" << endl;
            if (!(cin >> choice)) {
                cout << "输入格式错误" << endl;
                cin.clear();
                cin.ignore(INT32_MAX, '\n');
                continue;
            }
            cin.ignore(INT32_MAX, '\n');
            if (choice == 1) {
                return login(fd, user); // 递归重试
            } else if (choice == 0) {
                return 0;
            } else {
                cout << "无效选项，请重新输入。" << endl;
            }
        }
    } else if (buf == "-3") {
        cout << "该用户已经登录" << endl;
        return 0;
    }  else if (buf == "1") {
        cout << "登录成功!" << endl;
        string user_info;
        recvMsg(fd, user_info);
        user.json_parse(user_info);
        cout << "用户：【" <<user.getUsername()<<"】" <<  endl;

        return 1;
    }
    return 0;
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

int email_reset_password(int fd) {
    string email, code, password, password2, server_reply;
    cout << "请输入你的邮箱: ";
    getline(cin, email);
    if (email.empty()) {
        cout << "邮箱不能为空！" << endl;
        return 0;
    }
    // 获取验证码
    cout << "按回车获取验证码..." << endl;
    cin.ignore(INT32_MAX, '\n');
    sendMsg(fd, REQUEST_RESET_CODE);
    sendMsg(fd, email);
    recvMsg(fd, server_reply);
    cout << server_reply << endl;
    if (server_reply.find("失败") != string::npos) return 0;
    // 优化：循环输入验证码，不能为空
    while (true) {
        cout << "请输入收到的验证码: ";
        getline(cin, code);
        if (code.empty()) {
            cout << "验证码不能为空！" << endl;
            continue;
        }
        break;
    }

    get_password("请输入新密码: ", password);
    get_password("请再次输入新密码: ", password2);
    if (password != password2) {
        cout << "两次密码不一致！" << endl;
        return 0;
    }
    // 组装JSON
    json root;
    root["email"] = email;
    root["code"] = code;
    root["password"] = password;
    string json_str = root.dump();
    sendMsg(fd, RESET_PASSWORD_WITH_CODE);
    sendMsg(fd, json_str);
    recvMsg(fd, server_reply);
    cout << server_reply << endl;
    return server_reply == "密码重置成功";
}

int email_find_password(int fd) {
    string email, code, server_reply;
    
   
    while (true) {
        cout << "请输入你的邮箱: ";
        getline(cin, email);
        if (email.empty()) {
            cout << "邮箱不能为空！" << endl;
            continue;
        }
        break;
    }
    // 获取验证码
    cout << "按回车获取验证码..." << endl;
    cin.ignore(INT32_MAX, '\n');
    sendMsg(fd, REQUEST_RESET_CODE); // 复用请求验证码的协议
    sendMsg(fd, email);
    recvMsg(fd, server_reply);
    cout << server_reply << endl;
    if (server_reply.find("失败") != string::npos) return 0;
    // 循环输入验证码，不能为空
    while (true) {
        cout << "请输入收到的验证码: ";
        getline(cin, code);
        if (code.empty()) {
            cout << "验证码不能为空！" << endl;
            continue;
        }
        break;
    }
    // 组装JSON
    json root;
    root["email"] = email;
    root["code"] = code;
    string json_str = root.dump();
    sendMsg(fd, FIND_PASSWORD_WITH_CODE); // 需要在协议中定义 FIND_PASSWORD_WITH_CODE
    sendMsg(fd, json_str);
    recvMsg(fd, server_reply);
    cout << server_reply << endl;
    return server_reply == "找回密码成功";
}

