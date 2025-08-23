#include "FriendHandlers.h"
#include "../utils/proto.h"
#include "../utils/IO.h"
#include "../Redis.h"
#include <iostream>
#include <string>

using namespace std;
using json = nlohmann::json;

#include "../ServerState.h"

void handleAddFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的添加好友请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_ADD_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期，请重新登录";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_username = msg["data"].value("friend_username", "");
    if (friend_username.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "好友用户名不能为空";
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

    if (!redis.hexists("username_to_uid", friend_username)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = redis.hget("username_to_uid", friend_username);
    if (my_uid == friend_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "不能添加自己为好友";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (redis.sismember(my_uid, friend_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "对方已经是您的好友";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string request_key = friend_uid + "add_friend";
    if (redis.sismember(request_key, my_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "已发送过请求，请勿重复发送";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.sadd(request_key, my_uid);

    string notify_key = friend_uid + "add_f_notify";
    redis.sadd(notify_key, my_uid);

    if (redis.hexists("unified_receiver", friend_uid)) {
        string receiver_fd_str = redis.hget("unified_receiver", friend_uid);
        int receiver_fd = stoi(receiver_fd_str);
        sendMsg(receiver_fd, REQUEST_NOTIFICATION);
        redis.srem(notify_key, my_uid);
    }

    response["data"]["success"] = true;
    response["data"]["reason"] = "好友请求发送成功";
    sendMsg(epfd, fd, response.dump());
}


void handleGetFriendRequests(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取好友请求列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_FRIEND_REQUESTS_RESPONSE;
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

    string request_key = my_uid + "add_friend";
    redisReply *replies = redis.smembers(request_key);

    nlohmann::json requesters = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string requester_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", requester_uid);
            if (!user_info_str.empty()) {
                try {
                    requesters.push_back(nlohmann::json::parse(user_info_str));
                } catch (const nlohmann::json::parse_error& e) {
                    cerr << "[ERROR] 解析好友请求者信息失败: " << e.what() << endl;
                }
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["requests"] = requesters;
    sendMsg(epfd, fd, response.dump());
}

void handleRespondToFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的响应好友请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_RESPOND_TO_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string requester_uid = msg["data"].value("requester_uid", "");
    bool accepted = msg["data"].value("accepted", false);

    Redis redis;
    if (!redis.connect()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "服务器内部错误 (Redis)";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string request_key = my_uid + "add_friend";
    if (!redis.sismember(request_key, requester_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该好友请求不存在或已处理";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.srem(request_key, requester_uid);

    if (accepted) {
        redis.sadd(my_uid, requester_uid);
        redis.sadd(requester_uid, my_uid);

        time_t now = time(nullptr);
        redis.hset("friend_join_time", my_uid + "_" + requester_uid, to_string(now));
        redis.hset("friend_join_time", requester_uid + "_" + my_uid, to_string(now));

        response["data"]["reason"] = "已成功添加好友";
    } else {
        response["data"]["reason"] = "已拒绝好友请求";
    }

    response["data"]["success"] = true;
    sendMsg(epfd, fd, response.dump());
}

void handleDeleteFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的删除好友请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_DELETE_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = msg["data"].value("friend_uid", "");
    if (friend_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的好友ID";
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

    redis.srem(my_uid, friend_uid);
    redis.hdel("friend_join_time", my_uid + "_" + friend_uid);
    redis.hdel("friend_join_time", friend_uid + "_" + my_uid);
    redis.srem("blocked" + my_uid, friend_uid);
    g_mysql.deleteMessagesCompletely(my_uid, friend_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "好友已成功删除";
    sendMsg(epfd, fd, response.dump());
}

void handleBlockFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的屏蔽好友请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_BLOCK_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = msg["data"].value("friend_uid", "");
    if (friend_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的好友ID";
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

    string block_key = "blocked" + my_uid;
    redis.sadd(block_key, friend_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功屏蔽该好友";
    sendMsg(epfd, fd, response.dump());
}

void handleGetBlockedListRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取屏蔽列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_BLOCKED_LIST_RESPONSE;
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

    string block_key = "blocked" + my_uid;
    redisReply *replies = redis.smembers(block_key);

    nlohmann::json blocked_users = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string blocked_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", blocked_uid);
            if (!user_info_str.empty()) {
                try {
                    blocked_users.push_back(nlohmann::json::parse(user_info_str));
                } catch (const nlohmann::json::parse_error& e) {
                    cerr << "[ERROR] 解析被屏蔽用户信息失败: " << e.what() << endl;
                }
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["blocked_list"] = blocked_users;
    sendMsg(epfd, fd, response.dump());
}

void handleUnblockFriendRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的解除屏蔽请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_UNBLOCK_FRIEND_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = msg["data"].value("friend_uid", "");
    if (friend_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的好友ID";
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

    string block_key = "blocked" + my_uid;
    redis.srem(block_key, friend_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功解除屏蔽";
    sendMsg(epfd, fd, response.dump());
}
