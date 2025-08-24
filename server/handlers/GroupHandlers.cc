#include "GroupHandlers.h"
#include "../utils/proto.h"
#include "../utils/IO.h"
#include "../utils/User.h"
#include "../utils/Group.h"
#include "../MySQL.h"
#include "../Redis.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;
using json = nlohmann::json;

#include "../ServerState.h"



void handleStartGroupChatRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的开始群聊请求" << endl;
    nlohmann::json response;
    string uid = getUidByFd(fd);

    if (uid.empty()) {
        response["flag"] = S2C_START_GROUP_CHAT_RESPONSE;
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户未登录或会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (!msg.contains("data") || !msg["data"].is_object() || !msg["data"].contains("group_uid")) {
        response["flag"] = S2C_START_GROUP_CHAT_RESPONSE;
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求格式";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    string group_uid = msg["data"]["group_uid"].get<string>();

    vector<string> history = g_mysql.getGroupHistoryAfterTime(group_uid, 0, 50);

    response["flag"] = S2C_START_GROUP_CHAT_RESPONSE;
    response["data"]["success"] = true;
    response["data"]["history"] = history;

    sendMsg(epfd, fd, response.dump());
}



void handleGroupMessage(int epfd, int fd, const nlohmann::json& msg) {
    string sender_uid = getUidByFd(fd);
    if (sender_uid.empty()) return;

    auto data = msg["data"];
    string group_uid = data.value("group_uid", "");
    string content = data.value("content", "");

    g_mysql.insertGroupMessage(group_uid, sender_uid, content);

    Redis redis;
    if (redis.connect()) {
        Message message(getUsernameFromRedis(sender_uid), sender_uid, group_uid, getGroupNameFromRedis(group_uid));
        message.setContent(content);
        string message_json = message.to_json();

        redisReply *replies = redis.smembers("group_members:" + group_uid);
        if (replies) {
            for (size_t i = 0; i < replies->elements; ++i) {
                if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
                string member_uid = replies->element[i]->str;
                if (member_uid == sender_uid) continue; // Don't send to self

                if (redis.hexists("notification_fds", member_uid)) {
                    int member_fd = stoi(redis.hget("notification_fds", member_uid));
                    cout << "[实时消息] 转发群聊消息给在线成员 " << member_uid << " (fd: " << member_fd << ")" << endl;
                    sendMsg(epfd, member_fd, message_json);
                } else {
                    cout << "[实时消息] 群成员 " << member_uid << " 不在线, 存储为离线消息" << endl;
                    // Storing the full message JSON for group notifications might be too much.
                    // Sticking to the original design of notifying that a group has new messages.
                    redis.lpush("off_msg:" + member_uid, getGroupNameFromRedis(group_uid));
                }
            }
            freeReplyObject(replies);
        }
    }
}


void handleCreateGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的创建群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_CREATE_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_name = msg["data"].value("group_name", "");
    if (group_name.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群聊名称不能为空";
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

    if (redis.sismember("group_Name", group_name)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群聊名称已存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    Group group(group_name, my_uid);
    redis.hset("group_info", group.getGroupUid(), group.to_json());
    redis.sadd("group_Name", group_name);
    redis.sadd("created" + my_uid, group.getGroupUid());
    redis.sadd("managed" + my_uid, group.getGroupUid());
    redis.sadd("joined" + my_uid, group.getGroupUid());
    redis.sadd(group.getMembers(), my_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "群聊创建成功";
    sendMsg(epfd, fd, response.dump());
}

void handleJoinGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的加入群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_JOIN_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_name = msg["data"].value("group_name", "");
    if (group_name.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群聊名称不能为空";
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

    if (!redis.sismember("group_Name", group_name)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群聊不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = redis.hget("group_uid_by_name", group_name);
    if (redis.sismember("group_members:" + group_uid, my_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "您已是该群成员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string request_key = "group_join_requests:" + group_uid;
    redis.sadd(request_key, my_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "入群申请已发送";
    sendMsg(epfd, fd, response.dump());
}
void handleGetManagedGroupsRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取可管理群组列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_MANAGED_GROUPS_RESPONSE;
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

    string managed_key = "managed" + my_uid;
    redisReply *replies = redis.smembers(managed_key);

    nlohmann::json managed_groups = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string group_uid = replies->element[i]->str;
            string group_info_str = redis.hget("group_info", group_uid);
            if (!group_info_str.empty()) {
                managed_groups.push_back(nlohmann::json::parse(group_info_str));
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["managed_groups"] = managed_groups;
    sendMsg(epfd, fd, response.dump());
}

void handleGetGroupMembersRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取群成员列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_GROUP_MEMBERS_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = msg["data"].value("group_uid", "");
    if (group_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的群组ID";
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

    string members_key = "group_members:" + group_uid;
    redisReply *replies = redis.smembers(members_key);

    nlohmann::json members = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string member_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", member_uid);
            if (!user_info_str.empty()) {
                members.push_back(nlohmann::json::parse(user_info_str));
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["members"] = members;
    sendMsg(epfd, fd, response.dump());
}

void handleKickGroupMemberRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的踢出群成员请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_KICK_GROUP_MEMBER_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = msg["data"].value("group_uid", "");
    string kick_uid = msg["data"].value("kick_uid", "");

    if (group_uid.empty() || kick_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求参数";
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

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群组不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    Group group;
    group.json_parse(group_info_str);

    bool is_owner = (group.getOwnerUid() == my_uid);
    bool is_admin = redis.sismember(group.getAdmins(), my_uid);

    if (!is_owner && !is_admin) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "没有权限执行此操作";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (kick_uid == group.getOwnerUid()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "不能踢出群主";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (!is_owner && redis.sismember(group.getAdmins(), kick_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "管理员不能踢出其他管理员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.srem(group.getMembers(), kick_uid);
    redis.srem("joined" + kick_uid, group_uid);
    redis.srem("managed" + kick_uid, group_uid);
    redis.srem(group.getAdmins(), kick_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "成员已被踢出";
    sendMsg(epfd, fd, response.dump());
}
void handleAppointAdminRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的任命管理员请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_APPOINT_ADMIN_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = msg["data"].value("group_uid", "");
    string appoint_uid = msg["data"].value("appoint_uid", "");

    if (group_uid.empty() || appoint_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求参数";
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

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群组不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    Group group;
    group.json_parse(group_info_str);

    if (group.getOwnerUid() != my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "只有群主才能任命管理员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (!redis.sismember(group.getMembers(), appoint_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "该用户不是群成员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.sadd(group.getAdmins(), appoint_uid);
    redis.sadd("managed" + appoint_uid, group_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "管理员任命成功";
    sendMsg(epfd, fd, response.dump());
}

void handleGetGroupAdminsRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取群管理员列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_GROUP_ADMINS_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = msg["data"].value("group_uid", "");
    if (group_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的群组ID";
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

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群组不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    Group group;
    group.json_parse(group_info_str);

    redisReply *replies = redis.smembers(group.getAdmins());
    nlohmann::json admins = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string admin_uid = replies->element[i]->str;
            string user_info_str = redis.hget("user_info", admin_uid);
            if (!user_info_str.empty()) {
                admins.push_back(nlohmann::json::parse(user_info_str));
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["admins"] = admins;
    sendMsg(epfd, fd, response.dump());
}

void handleRevokeAdminRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的撤销管理员请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_REVOKE_ADMIN_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = msg["data"].value("group_uid", "");
    string revoke_uid = msg["data"].value("revoke_uid", "");

    if (group_uid.empty() || revoke_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求参数";
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

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群组不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    Group group;
    group.json_parse(group_info_str);

    if (group.getOwnerUid() != my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "只有群主才能撤销管理员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    if (revoke_uid == my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "不能撤销自己的群主身份";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.srem(group.getAdmins(), revoke_uid);
    redis.srem("managed" + revoke_uid, group_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "管理员已撤销";
    sendMsg(epfd, fd, response.dump());
}

void handleDeleteGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的解散群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_DELETE_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = msg["data"].value("group_uid", "");
    if (group_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的群组ID";
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

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群组不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    Group group;
    group.json_parse(group_info_str);

    if (group.getOwnerUid() != my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "只有群主才能解散群聊";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redisReply *members_reply = redis.smembers(group.getMembers());
    if (members_reply) {
        for (size_t i = 0; i < members_reply->elements; ++i) {
            if (members_reply->element[i] == nullptr || members_reply->element[i]->str == nullptr) continue;
            string member_uid = members_reply->element[i]->str;
            redis.srem("joined" + member_uid, group_uid);
            redis.srem("managed" + member_uid, group_uid);
        }
        freeReplyObject(members_reply);
    }

    redis.del(group.getMembers());
    redis.del(group.getAdmins());
    redis.del("group_join_requests:" + group_uid);
    redis.srem("group_Name", group.getGroupName());
    redis.hdel("group_info", group_uid);
    g_mysql.deleteGroupMessages(group_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "群聊已成功解散";
    sendMsg(epfd, fd, response.dump());
}

void handleGetJoinedGroupsRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的获取已加入群聊列表请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_JOINED_GROUPS_RESPONSE;
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

    string joined_key = "joined" + my_uid;
    redisReply *replies = redis.smembers(joined_key);

    nlohmann::json joined_groups = nlohmann::json::array();
    if (replies) {
        for (size_t i = 0; i < replies->elements; ++i) {
            if (replies->element[i] == nullptr || replies->element[i]->str == nullptr) continue;
            string group_uid = replies->element[i]->str;
            string group_info_str = redis.hget("group_info", group_uid);
            if (!group_info_str.empty()) {
                joined_groups.push_back(nlohmann::json::parse(group_info_str));
            }
        }
        freeReplyObject(replies);
    }

    response["data"]["success"] = true;
    response["data"]["joined_groups"] = joined_groups;
    sendMsg(epfd, fd, response.dump());
}

void handleQuitGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的退出群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_QUIT_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = msg["data"].value("group_uid", "");
    if (group_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的群组ID";
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

    string group_info_str = redis.hget("group_info", group_uid);
    if (group_info_str.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群组不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }
    Group group;
    group.json_parse(group_info_str);

    if (group.getOwnerUid() == my_uid) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "群主不能退出群聊，只能解散";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.srem(group.getMembers(), my_uid);
    redis.srem("joined" + my_uid, group_uid);
    redis.srem(group.getAdmins(), my_uid); // Also remove from admins if they were one
    redis.srem("managed" + my_uid, group_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "已成功退出群聊";
    sendMsg(epfd, fd, response.dump());
}

void handleInviteToGroupRequest(int epfd, int fd, const nlohmann::json& msg) {
    cout << "[业务] 处理fd=" << fd << " 的邀请好友加入群聊请求" << endl;
    nlohmann::json response;
    response["flag"] = S2C_INVITE_TO_GROUP_RESPONSE;
    string my_uid = getUidByFd(fd);

    if (my_uid.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "用户会话已过期";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string group_uid = msg["data"].value("group_uid", "");
    string friend_username = msg["data"].value("friend_username", "");

    if (group_uid.empty() || friend_username.empty()) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "无效的请求参数";
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
        response["data"]["reason"] = "好友不存在";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    string friend_uid = redis.hget("username_to_uid", friend_username);
    if (redis.sismember("group_members:" + group_uid, friend_uid)) {
        response["data"]["success"] = false;
        response["data"]["reason"] = "好友已是群成员";
        sendMsg(epfd, fd, response.dump());
        return;
    }

    redis.sadd("group_members:" + group_uid, friend_uid);
    redis.sadd("joined" + friend_uid, group_uid);

    response["data"]["success"] = true;
    response["data"]["reason"] = "好友邀请成功";
    sendMsg(epfd, fd, response.dump());
}




