#include "LoginHandler.h"
#include "../utils/IO.h"
#include "../utils/proto.h"
#include "Redis.h"
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
using namespace std;

// Your correct notify function, adapted for the new redisReply structure
void notify(int fd, const string &UID) {
    bool msgnum = false;
    Redis redis;
    if (!redis.connect()) return;

    auto process_string_set = [&](const string& key, const string& msg_prefix) {
        redisReply *reply = redis.smembers(key);
        if (reply && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                if (reply->element[i] && reply->element[i]->str) {
                    sendMsg(fd, msg_prefix + reply->element[i]->str);
                    msgnum = true;
                    redis.srem(key, reply->element[i]->str);
                }
            }
        }
        freeReplyObject(reply);
    };

    // Friend requests
    redisReply* reply = redis.smembers(UID + "add_f_notify");
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        if (reply->elements > 0) {
            sendMsg(fd, REQUEST_NOTIFICATION);
            msgnum = true;
            for (size_t i = 0; i < reply->elements; ++i) {
                 if (reply->element[i] && reply->element[i]->str) {
                    redis.srem(UID + "add_f_notify", reply->element[i]->str);
                 }
            }
        }
    }
    freeReplyObject(reply);

    // Group notifications
    process_string_set(UID + "deleteAC_notify", "deleteAC_notify:");
    process_string_set("appoint_admin" + UID, "ADMIN_ADD:");
    process_string_set("REMOVE" + UID, "REMOVE:");
    process_string_set("DELETE" + UID, "DELETE:");
    process_string_set(UID + "file_notify", "FILE:");
    process_string_set("revoke_admin" + UID, "ADMIN_REMOVE:");

    // Offline messages
    int offlineMsgNum = redis.llen("off_msg" + UID);
    if (offlineMsgNum > 0) {
        redisReply **offlineMsgArr = redis.lrange("off_msg" + UID, "0", "-1");
        map<string, int> senderCount;
        if (offlineMsgArr) {
            for (int i = 0; i < offlineMsgNum; i++) {
                if (offlineMsgArr[i] && offlineMsgArr[i]->str) {
                    senderCount[string(offlineMsgArr[i]->str)]++;
                }
                freeReplyObject(offlineMsgArr[i]);
            }
            free(offlineMsgArr);

            for (const auto& pair : senderCount) {
                string sender = pair.first;
                int count = pair.second;
                if (count == 1) {
                    sendMsg(fd, "MESSAGE:" + sender);
                } else {
                    sendMsg(fd, "MESSAGE:" + sender + "(" + to_string(count) + "条)");
                }
                msgnum = true;
            }
            redis.del("off_msg" + UID);
        }
    }

    if (!msgnum) {
        sendMsg(fd, "nomsg");
    }
}

void handleRequestOfflineNotifications(int epfd, int fd, const nlohmann::json& msg) {
    string uid = msg["data"].value("uid", "");
    if (uid.empty()) {
        cout << "[警告] 收到一个没有UID的离线通知请求，已忽略。fd=" << fd << endl;
        return;
    }

    cout << "[业务] 处理用户 " << uid << " (fd=" << fd << ") 的离线通知请求" << endl;

    Redis redis;
    if (!redis.connect()) {
        cerr << "[ERROR] 处理离线通知请求时无法连接到Redis" << endl;
        return;
    }

    // 1. Register the notification FD for real-time messages
    redis.hset("notification_fds", uid, to_string(fd));

    // 2. Fetch and send all offline notifications
    notify(fd, uid);
}

