#include "proto.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <utility>
//数据传输操作上方空一行以突出逻辑
using namespace std;
using json = nlohmann::json;

LoginRequest::LoginRequest() = default;

LoginRequest::LoginRequest(std::string email, std::string password) : email(std::move(email)), password(std::move(password)) {}

[[nodiscard]] const string &LoginRequest::getEmail() const {
    return email;
}

void LoginRequest::setEmail(const std::string &email) {
    this->email = email;
}

[[nodiscard]] const string &LoginRequest::getPassword() const {
    return password;
}

void LoginRequest::setPassword(const std::string &password) {
    this->password = password;
}

string LoginRequest::to_json() {
    json root;
    root["email"] = email;
    root["password"] = password;
    return root.dump();
}

void LoginRequest::json_parse(const string &json_str) {
    // json root = json::parse(json_str);
    // email = root["email"].get<string>();
    // password = root["password"].get<string>();
    try {
        json root = json::parse(json_str);
        // 使用 value() 方法，如果键不存在或为null，返回第二个参数的默认值（空字符串）
        email = root.value("email", "");
        password = root.value("password", "");
    } catch (const json::exception& e) {
        // 处理JSON解析本身的错误，例如字符串格式不正确
        cerr << "[ERROR] LoginRequest JSON解析失败: " << e.what() << endl;
        cerr << "[ERROR] JSON内容: " << json_str << endl;
        // 可以选择抛出，或者设置成员为默认空值
        email = "";
        password = "";
      
    }
}


Message::Message() = default;

Message::Message(string username, string UID_from, string UID_to, string groupName) : username(std::move(username)),
                                                                                      UID_from(std::move(UID_from)),
                                                                                      UID_to(std::move(UID_to)),
                                                                                      group_name(
                                                                                              std::move(groupName)) {}

[[nodiscard]] string Message::getUsername() const {
    return username;
}

void Message::setUsername(const string &name) {
    username = name;
}

[[nodiscard]] const string &Message::getUidFrom() const {
    return UID_from;
}

void Message::setUidFrom(const string &uidFrom) {
    UID_from = uidFrom;
}

[[nodiscard]] const string &Message::getUidTo() const {
    return UID_to;
}

void Message::setUidTo(const string &uidTo) {
    UID_to = uidTo;
}

[[nodiscard]] string Message::getContent() const {
    return content;
}

void Message::setContent(const string &msg) {
    content = msg;
}

[[nodiscard]] const string &Message::getGroupName() const {
    return group_name;
}

void Message::setGroupName(const string &groupName) {
    group_name = groupName;
}



[[nodiscard]] string Message::getTime() const {
    return timeStamp;
}

void Message::setTime(const string &t) {
    timeStamp = t;
}

string Message::get_time() {
    time_t raw_time;
    struct tm *ptm;
    time(&raw_time);
    ptm = localtime(&raw_time);
    //char* data=new char[60];
    unique_ptr<char[]> data(new char[60]);
    sprintf(data.get(), "%d-%d-%d-%02d:%02d:%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour,
            ptm->tm_min, ptm->tm_sec);
    
    string result(data.get());
    return result;
}

string Message::to_json() {
    timeStamp = get_time();
    json root;
    root["timeStamp"] = timeStamp;
    root["username"] = username;
    root["UID_from"] = UID_from;
    root["UID_to"] = UID_to;
    root["content"] = content;
    root["group_name"] = group_name;
    return root.dump();
}

void Message::json_parse(const string &json_str) {
    json root = json::parse(json_str);
    timeStamp = root["timeStamp"].get<string>();
    username = root["username"].get<string>();
    UID_from = root["UID_from"].get<string>();
    UID_to = root["UID_to"].get<string>();
    content = root["content"].get<string>();
    group_name = root["group_name"].get<string>();
}
