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
    response["flag"] = S2C_CHAT_LISTS_RESPONSE;
    string uid = getUidByFd(fd);

    if (uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "User not logged in or session expired";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Redis redis;
    if (!redis.connect()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "Server internal error (Redis)";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    // Fetch friends
    nlohmann::json friends_list = nlohmann::json::array();
    string friends_key = uid; // The key for a user's friend list is their UID
    redisReply *replies = redis.smembers(friends_key);
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string friend_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", friend_uid);
            if (!user_info_str.empty()) {
                try {
                    nlohmann::json friend_json = nlohmann::json::parse(user_info_str);
                    friend_json["is_online"] = redis.hexists("is_online", friend_uid);
                    friends_list.push_back(friend_json);
                } catch (const nlohmann::json::parse_error& e) {}
            }
        }
        freeReplyObject(replies);
    }

    // Fetch groups
    nlohmann::json groups_list = nlohmann::json::array();
    string joined_groups_key = "joined" + uid;
    replies = redis.smembers(joined_groups_key);
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string group_uid = replies->element[i]->str;
            string group_info_str = redis.hget("group_info", group_uid);
            if (!group_info_str.empty()) {
                try {
                    groups_list.push_back(nlohmann::json::parse(group_info_str));
                } catch (const nlohmann::json::parse_error& e) {}
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["friends"] = friends_list;
    response["data"]["groups"] = groups_list;
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

