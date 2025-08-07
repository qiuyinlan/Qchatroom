#include <random>
#include <iostream>
#include <nlohmann/json.hpp>
#include "User.h"

using namespace std;
using json = nlohmann::json;


string User::get_time() {
    time_t raw_time;
    struct tm *ptm;
    time(&raw_time);
    ptm = localtime(&raw_time);
    //char* data=new char[60];
    unique_ptr<char[]> data(new char[60]);
    sprintf(data.get(), "%d-%d-%d-%02d:%02d:%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour,
            ptm->tm_min, ptm->tm_sec);
    //string result(data);
    //delete[] data;
    string result(data.get());
    return result;
}


User::User() : is_online(false) {
    // 取当前时间戳的后两位
    time_t timer;
    time(&timer);
    string timeStamp = to_string(timer).substr(to_string(timer).size() - 2, 2);
    // 生成2位随机数
    std::random_device rd;
    std::mt19937 eng(rd());
    std::uniform_int_distribution<> dist(10, 99);
    int random_num = dist(eng);
    // 拼接成4位UID
    UID = timeStamp + to_string(random_num);
    my_time = get_time();
    password = "";
    username = "";
    email = "";
}
//设置邮箱等等，是一个操作不需要返回值
//但如果要拿东西，这个东西是必须要拿到的

void User::setUID(string uid) {
    UID = std::move(uid);
}

[[nodiscard]] string User::getUID() const {
    return UID;
}

void User::setEmail(string email) {
    this->email = std::move(email);
}

[[nodiscard]] string User::getEmail() const {
    return email;
}

void User::setPassword(string password) {
    this->password = std::move(password);
}

void User::setUsername(string name) {
    username = std::move(name);
}

string User::getMyTime() const {
    return my_time;
}

[[nodiscard]] string User::getUsername() const {
    return username;
}

[[nodiscard]] string User::getPassword() const {
    return password;
}

void User::setIsOnline(bool online) {
    is_online = online;
}

[[nodiscard]] bool User::getIsOnline() const {
    return is_online;
}

string User::to_json() {
    json root;
    root["my_time"] = my_time;
    root["UID"] = UID;
    root["email"] = email;
    root["username"] = username;
    root["password"] = password;
    return root.dump();
}

void User::json_parse(const string &json_str) {
    try {
        json root = json::parse(json_str);

        // 安全地获取字段
        if (root.contains("my_time") && root["my_time"].is_string()) {
            my_time = root["my_time"].get<string>();
        }
        if (root.contains("UID") && root["UID"].is_string()) {
            UID = root["UID"].get<string>();
        }
        if (root.contains("email") && root["email"].is_string()) {
            email = root["email"].get<string>();
        }
        if (root.contains("username") && root["username"].is_string()) {
            username = root["username"].get<string>();
        }
        if (root.contains("password") && root["password"].is_string()) {
            password = root["password"].get<string>();
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] User JSON解析失败: " << e.what() << std::endl;
        std::cerr << "[ERROR] JSON内容: " << json_str << std::endl;
        // 设置默认值
        my_time = "";
        UID = "";
        email = "";
        username = "未知用户";
        password = "";
    }
}
