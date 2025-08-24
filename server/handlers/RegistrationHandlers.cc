#include "RegistrationHandlers.h"
#include "../Redis.h"
#include "../utils/IO.h"
#include "../utils/User.h"
#include "../utils/proto.h"
#include <iostream>
#include <string>
#include <random>
#include <ctime>
#include <curl/curl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

// Function to generate a verification code
std::string generateCode() {
    time_t timer;
    time(&timer);
    std::string timeStamp = std::to_string(timer).substr(std::to_string(timer).size() - 2, 2);
    std::random_device rd;
    std::mt19937 eng(rd());
    std::uniform_int_distribution<> dist(10, 99);
    int random_num = dist(eng);
    return timeStamp + std::to_string(random_num);
}

// Struct for curl email upload
struct upload_status {
    size_t bytes_read;
    const std::string* payload;
};

// Curl callback for reading email payload
static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp) {
    struct upload_status *upload_ctx = (struct upload_status *)userp;
    const std::string& payload = *(upload_ctx->payload);
    size_t left = payload.size() - upload_ctx->bytes_read;
    size_t len = size * nmemb;
    if (len > left) {
        len = left;
    }
    if (len) {
        memcpy(ptr, payload.data() + upload_ctx->bytes_read, len);
        upload_ctx->bytes_read += len;
    }
    return len;
}

// Function to send email using curl
bool sendMail(const std::string& to_email, const std::string& code) {
    CURL *curl;
    CURLcode res = CURLE_OK;
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.163.com:465");
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_USERNAME, "17750601137@163.com");
        curl_easy_setopt(curl, CURLOPT_PASSWORD, "GAWKe3AaL7bEV64e");
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, "17750601137@163.com");
        struct curl_slist *recipients = nullptr;
        recipients = curl_slist_append(recipients, to_email.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        std::string from = "From: 17750601137@163.com\r\n";
        std::string to = "To: " + to_email + "\r\n";
        std::string subject = "Subject: Verification Code\r\n";
        std::string mimeVersion = "MIME-Version: 1.0\r\n";
        std::string contentType = "Content-Type: text/plain; charset=utf-8\r\n";
        std::string body = "Hello,\r\n\r\nYour verification code is: " + code + "\r\nPlease use this code within 5 minutes to complete your verification.\r\n\r\nThank you!";
        std::string full_mail_payload = from + to + subject + mimeVersion + contentType + "\r\n" + body;

        upload_status up_status = { 0, &full_mail_payload };
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &up_status);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE, (long)full_mail_payload.length());

        res = curl_easy_perform(curl);
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);

        return res == CURLE_OK;
    }
    return false;
}

// Handles the multi-step process of requesting a registration code
void handleRequestCode(int epfd, int fd, const nlohmann::json& msg) {
    Redis redis;
    if (!redis.connect()) {
        sendMsg(fd, "服务器内部错误 (Redis)");
        return;
    }

    string email = msg["data"].value("email", "");
    string username = msg["data"].value("username", "");

    if (redis.hexists("email_to_uid", email)) {
        sendMsg(fd, "邮箱已存在");
        return;
    }

    if (redis.hexists("username_to_uid", username)) {
        sendMsg(fd, "用户名已存在");
        return;
    }

    string code = generateCode();
    redis.hset("verify_code", email, code);

    if (sendMail(email, code)) {
        sendMsg(fd, "验证码已发送，请查收邮箱");
    } else {
        sendMsg(fd, "验证码发送失败，请稍后重试");
    }
}

// Handles the final step of registration with a code
void serverRegisterWithCode(int epfd, int fd, const nlohmann::json& msg) {
    Redis redis;
    if (!redis.connect()) {
        sendMsg(fd, "服务器内部错误");
        return;
    }

    try {
        auto data = msg["data"];
        string email = data.value("email", "");
        string code = data.value("code", "");
        string username = data.value("username", "");
        string password = data.value("password", "");

        string real_code = redis.hget("verify_code", email);
        if (real_code.empty() || real_code != code) {
            sendMsg(fd, "验证码错误");
            return;
        }

        User user;
        user.setEmail(email);
        user.setUsername(username);
        user.setPassword(password);

        redis.hset("user_info", user.getUID(), user.to_json());
        redis.hset("email_to_uid", email, user.getUID());
        redis.hset("username_to_uid", username, user.getUID());
        redis.sadd("all_uid", user.getUID());
        redis.hdel("verify_code", email);

        sendMsg(fd, "注册成功");
    } catch (const json::parse_error& e) {
        sendMsg(fd, "无效的注册信息格式");
    }
}

// Placeholder for password reset logic
void handleResetCode(int epfd, int fd) {
    // This function can be implemented similarly to handleRequestCode
    // but for password reset purposes.
    sendMsg(fd, "功能待实现");
    struct epoll_event temp;
    temp.data.fd = fd;
    temp.events = EPOLLIN | EPOLLET;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
}

