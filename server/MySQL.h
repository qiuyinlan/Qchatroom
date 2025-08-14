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
    
    // 获取私聊历史消息
    std::vector<std::string> getPrivateHistory(const std::string& user1, const std::string& user2, int limit = 50);
};

#endif
