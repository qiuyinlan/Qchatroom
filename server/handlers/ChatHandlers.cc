#include "ChatHandlers.h"
#include "../utils/proto.h"
#include "../utils/IO.h"
#include "../Redis.h"
#include "../MySQL.h"
#include "../utils/User.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;
using json = nlohmann::json;

#include "../ServerState.h"



void handleGetChatLists(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取聊天列表请求" << endl;
    nlohmann::json response;
    string uid = getUidByFd(fd);

    if (uid.empty()) {
        response["flag"] = S2C_CHAT_LISTS_RESPONSE;
        response["data"]["reason"] = "用户未登录或会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Redis redis;
    if (!redis.connect()) {
        response["flag"] = S2C_CHAT_LISTS_RESPONSE;
        response["data"]["reason"] = "服务器内部错误 (Redis)";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    response["flag"] = S2C_CHAT_LISTS_RESPONSE;
    response["data"]["friends"] = nlohmann::json::array();
    response["data"]["groups"] = nlohmann::json::array();

    redisReply *friend_replies = redis.smembers(uid);
    if (friend_replies) {
        for (size_t i = 0; i < friend_replies->elements; ++i) {
            if (friend_replies->element[i] == nullptr || friend_replies->element[i]->str == nullptr) continue;
            string friend_id = friend_replies->element[i]->str;
            string user_info_str = redis.hget("user_info", friend_id);
            if (!user_info_str.empty()) {
                try {
                    nlohmann::json friend_json = nlohmann::json::parse(user_info_str);
                    friend_json["is_online"] = redis.hexists("is_online", friend_id);
                    response["data"]["friends"].push_back(friend_json);
                } catch (const nlohmann::json::parse_error& e) {
                    cerr << "[ERROR] Failed to parse user_info_str for friend " << friend_id << ": " << e.what() << endl;
                }
            }
        }
        freeReplyObject(friend_replies);
    }

    string group_list_key = "user_groups:" + uid;
    redisReply *group_reply = redis.smembers(group_list_key);
    if (group_reply) {
        for (size_t i = 0; i < group_reply->elements; ++i) {
            if (group_reply->element[i] == nullptr || group_reply->element[i]->str == nullptr) continue;
            string group_id = group_reply->element[i]->str;
            string group_info_str = redis.hget("group_info", group_id);
            if (!group_info_str.empty()) {
                response["data"]["groups"].push_back(nlohmann::json::parse(group_info_str));
            }
        }
        freeReplyObject(group_reply);
    }

    sendMsg(epfd, fd, response.dump());
}

void handleStartChatRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的开始聊天请求" << endl;
    nlohmann::json response;
    string uid = getUidByFd(fd);

    if (uid.empty()) {
        response["flag"] = S2C_START_CHAT_RESPONSE;
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户未登录或会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (!msg.contains("data") || !msg["data"].is_object() || !msg["data"].contains("friend_uid")) {
        response["flag"] = S2C_START_CHAT_RESPONSE;
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求格式";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    string friend_uid = msg["data"]["friend_uid"].get<string>();

    vector<string> history = g_mysql.getPrivateHistoryAfterTime(uid, friend_uid, 0, 50);

    response["flag"] = S2C_START_CHAT_RESPONSE;
    response["data"]["success"] = true;
    response["data"]["history"] = history;

    sendMsg(epfd, fd, response.dump());
}

void handlePrivateMessage(int epfd, int fd, const nlohmann::json& msg) {
    string sender_uid = getUidByFd(fd);
    if (sender_uid.empty()) return;

    auto data = msg["data"];
    string receiver_uid = data.value("receiver_uid", "");
    string content = data.value("content", "");

    g_mysql.insertPrivateMessage(sender_uid, receiver_uid, content);

    Redis redis;
    if (redis.connect() && redis.hexists("is_online", receiver_uid)) {
        int receiver_fd = stoi(redis.hget("is_online", receiver_uid));
        nlohmann::json forward_msg;
        forward_msg["flag"] = S2C_PRIVATE_MESSAGE;
        forward_msg["data"]["content"] = content;
        forward_msg["data"]["sender_uid"] = sender_uid;
        forward_msg["data"]["username"] = getUsernameFromRedis(sender_uid);
        sendMsg(epfd, receiver_fd, forward_msg.dump());
    }
}


void handleExitChatRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的退出聊天请求" << endl;
}

void handleGetHistoryRequest(int epfd, int fd, const nlohmann::json& msg) {
    // Implementation will be based on existing logic for fetching history
}

