#ifndef CHATROOM_MYSQL_H
#define CHATROOM_MYSQL_H

#include <mysql/mysql.h>
#include <string>
#include <vector>

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

    // 获取群聊历史消息
    std::vector<std::string> getGroupHistory(const std::string& group_uid, int limit = 50);

    // 获取群聊历史消息（指定时间之后的）
    std::vector<std::string> getGroupHistoryAfterTime(const std::string& group_uid, time_t after_time, int limit = 50);

    // 获取私聊历史消息
    std::vector<std::string> getPrivateHistory(const std::string& user1, const std::string& user2, int limit = 50);

    // 获取私聊历史消息（指定时间之后的）
    std::vector<std::string> getPrivateHistoryAfterTime(const std::string& user1_uid, const std::string& user2_uid, time_t after_time, int limit = 50);

    // ========== 新增：好友管理和拉黑功能 ==========

    // 检查是否为好友关系
    bool isFriend(const std::string& user1, const std::string& user2);

    // 检查是否被屏蔽
    bool isBlocked(const std::string& user, const std::string& blocked_user);

    // 添加好友关系
    bool addFriend(const std::string& user1, const std::string& user2);

    // 删除好友关系
    bool deleteFriend(const std::string& user1, const std::string& user2);

    // 屏蔽用户
    bool blockUser(const std::string& user, const std::string& blocked_user);

    // 解除屏蔽
    bool unblockUser(const std::string& user, const std::string& blocked_user);

    // ========== 删除好友时的消息处理 ==========

    // 完全删除消息（双方都删除时）
    bool deleteMessagesCompletely(const std::string& user1_uid, const std::string& user2_uid);

    // 删除群聊的所有消息（解散群聊时）
    bool deleteGroupMessages(const std::string& group_uid);
};

#endif
