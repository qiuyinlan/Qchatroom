#include "UserHandlers.h"
#include "../utils/proto.h"
#include "../utils/IO.h"
#include "../User.h"
#include "../Redis.h"
#include <iostream>
#include <string>

using namespace std;
using json = nlohmann::json;

#include "../ServerState.h"

void handleLoginRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的登录请求" << endl;
    nlohmann::json response;

    if (!msg.contains("data") || !msg["data"].is_object()) {
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "无效的请求格式.";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    auto data = msg["data"];
    string email = data.value("email", "");
    string password = data.value("password", "");

    if (email.empty() || password.empty()) {
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "邮箱或密码不能为空.";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Redis redis;
    if (!redis.connect()) {
        cerr << "[ERROR] fd=" << fd << " 的登录流程中Redis连接失败" << endl;
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "服务器内部错误 (Redis连接).";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (!redis.hexists("email_to_uid", email)) {
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "账户不存在.";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string UID = redis.hget("email_to_uid", email);
    string user_info_str = redis.hget("user_info", UID);

    User user;
    try {
        user.json_parse(user_info_str);
    } catch (const std::exception& e) {
        cerr << "[ERROR] UID: " << UID << " 的用户信息JSON解析失败, error: " << e.what() << endl;
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "服务器内部错误 (用户数据损坏).";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (password != user.getPassword()) {
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "密码错误.";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (redis.hexists("is_online", UID)) {
        cout << "[INFO] 用户 " << UID << " 已在线. 拒绝重复登录." << endl;
        response["flag"] = S2C_LOGIN_FAILURE;
        response["data"]["reason"] = "用户已在其他设备登录.";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    cout << "[INFO] 用户 " << UID << " 登录成功, fd=" << fd << endl;
    redis.hset("is_online", UID, to_string(fd));
    addFdToUid(fd, UID);

    response["flag"] = S2C_LOGIN_SUCCESS;
    response["data"] = nlohmann::json::parse(user_info_str);
    sendMsg(epfd, fd, response.dump());
}

void handleLogoutRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的登出请求" << endl;
    string uid = getUidByFd(fd);
    if (!uid.empty()) {
        Redis redis;
        if (redis.connect()) {
            redis.hdel("is_online", uid);
        }
        removeFdFromUid(fd);
    }
}

void handleDeactivateAccountRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的注销账户请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_DEACTIVATE_ACCOUNT_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Redis redis;
    if (!redis.connect()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "服务器内部错误 (Redis)";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.sadd("deactivated_users", my_uid);

    string user_info_str = redis.hget("user_info", my_uid);
    if (!user_info_str.empty()) {
        User user;
        user.json_parse(user_info_str);
        redis.hdel("email_to_uid", user.getEmail());
    }
    redis.hdel("user_info", my_uid);

    redisReply *friends_reply = redis.smembers(my_uid);
    if (friends_reply) {
        for (size_t i = 0; i < friends_reply->elements; ++i) {
            if (friends_reply->element[i] == nullptr || friends_reply->element[i]->str == nullptr) continue;
            string friend_uid = friends_reply->element[i]->str;
            redis.srem(friend_uid, my_uid);
        }
        freeReplyObject(friends_reply);
    }
    redis.del(my_uid);

    redis.hdel("is_online", my_uid);
    redis.hdel("unified_receiver", my_uid);
    removeUserActivity(my_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "账户已成功注销";
    sendMsg(epfd, fd, response.dump());
}

void handleHeartbeat(int epfd, int fd, const nlohmann::json& msg) {
    if (msg.contains("data") && msg["data"].contains("uid")) {
        string uid = msg["data"]["uid"].get<string>();
        addFdToUid(fd, uid);
        cout << "[心跳] 收到心跳，fd=" << fd << ", uid=" << uid << endl;
        updateUserActivity(uid);
    } else {
        cout << "[警告] 收到一个不规范的心跳包，已忽略。fd=" << fd << endl;
    }
}

