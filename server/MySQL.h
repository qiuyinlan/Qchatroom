#ifndef CHATROOM_MYSQL_H
#define CHATROOM_MYSQL_H

#include <mysql/mysql.h>
#include <string>
#include <vector>

//跨系统更强，依赖更少，更底层

class MySQL {
private:
    MYSQL* conn;

public:
    MySQL();
    ~MySQL();

    // 连接数据库
    bool connect();

    // 插入群聊消息
    bool insertGroupMessage(const std::string& group_uid, const std::string& sender_uid, const std::string& content);

    // 插入私聊消息
    bool insertPrivateMessage(const std::string& sender_uid, const std::string& receiver_uid, const std::string& content);

    // 获取群聊历史消息（指定时间之后的，时间戳为0时显示所有消息）
    std::vector<std::string> getGroupHistoryAfterTime(const std::string& group_uid, time_t after_time, int limit = 50);

    // 获取私聊历史消息（指定时间之后的，时间戳为0时显示所有消息）
    std::vector<std::string> getPrivateHistoryAfterTime(const std::string& user1_uid, const std::string& user2_uid, time_t after_time, int limit = 50);



    // ========== 删除好友时的消息处理 ==========

    // 完全删除消息（双方都删除时）
    bool deleteMessagesCompletely(const std::string& user1_uid, const std::string& user2_uid);

    // 删除群聊的所有消息（解散群聊时）
    bool deleteGroupMessages(const std::string& group_uid);
};

// 辅助函数声明
std::string getUsernameFromRedis(const std::string& uid);
std::string getGroupNameFromRedis(const std::string& group_uid);

#endif
