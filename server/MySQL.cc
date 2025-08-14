#include "MySQL.h"
#include "Redis.h"
#include <iostream>
#include <cstring>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

// 辅助函数：从Redis获取用户名
string getUsernameFromRedis(const string& uid) {
    Redis redis;
    if (!redis.connect()) {
        return uid; // 如果Redis连接失败，返回UID
    }

    // 尝试从不同的Redis key获取用户信息
    string user_info;

    // 1. 尝试从hash表"users"中获取用户信息
    if (redis.hexists("users", uid)) {
        user_info = redis.hget("users", uid);
    }
    // 2. 尝试从hash表"user_info"中获取用户信息
    else if (redis.hexists("user_info", uid)) {
        user_info = redis.hget("user_info", uid);
    }
    // 3. 尝试从"username"中获取
    else {
        user_info = redis.hget("username", uid);
    }

    // 如果获取到的是JSON格式，解析出username字段
    if (!user_info.empty() && user_info != "(nil)") {
        try {
            json user_json = json::parse(user_info);
            if (user_json.contains("username")) {
                string username = user_json["username"].get<string>();
                if (!username.empty()) {
                    return username;
                }
            }
        } catch (const exception& e) {
            // 如果不是JSON格式，直接返回原字符串（可能就是用户名）
            return user_info;
        }
    }

    // 如果都获取不到，返回UID
    return uid;
}

MySQL::MySQL() : conn(nullptr) {}

MySQL::~MySQL() {
    if (conn) {
        mysql_close(conn);
        conn = nullptr;
    }
}

bool MySQL::connect() {
    conn = mysql_init(nullptr);
    if (!conn) {
        cout << "[MySQL ERROR] mysql_init failed" << endl;
        return false;
    }
    
    // 连接数据库
    if (!mysql_real_connect(conn,
                           "localhost",    
                           "root",          
                           "123",             
                           "chatroom",     
                           3306,          
                           nullptr,
                           0)) {
        cout << "[MySQL ERROR] Connection failed: " << mysql_error(conn) << endl;
        mysql_close(conn);
        conn = nullptr;
        return false;
    }
    
    // 设置自动提交模式
    if (mysql_autocommit(conn, 1)) {
        cout << "[MySQL ERROR] Failed to set autocommit: " << mysql_error(conn) << endl;
        mysql_close(conn);
        conn = nullptr;
        return false;
    }

    cout << "[MySQL] Connected successfully with autocommit enabled" << endl;
    return true;
}

bool MySQL::insertGroupMessage(const string& group_uid, const string& sender_uid, const string& content) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }
    
    // 准备SQL语句
    const char* sql = "INSERT INTO group_messages (group_uid, sender_uid, content) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    
    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }
    
    // 绑定参数
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    // group_uid
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)group_uid.c_str();
    bind[0].buffer_length = group_uid.length();
    
    // sender_uid
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)sender_uid.c_str();
    bind[1].buffer_length = sender_uid.length();
    
    // content
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)content.c_str();
    bind[2].buffer_length = content.length();
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }
    
    // 执行语句
    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }
    
    mysql_stmt_close(stmt);
    cout << "[MySQL] Group message inserted successfully" << endl;
    return true;
}

bool MySQL::insertPrivateMessage(const string& sender_uid, const string& receiver_uid, const string& content) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    // 存储消息内容，sender_uid和receiver_uid用于查询索引
    const char* sql = "INSERT INTO private_messages (sender_uid, receiver_uid, content) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    
    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)sender_uid.c_str();
    bind[0].buffer_length = sender_uid.length();
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)receiver_uid.c_str();
    bind[1].buffer_length = receiver_uid.length();
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)content.c_str();
    bind[2].buffer_length = content.length();
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }
    
    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 显式提交事务
    if (mysql_commit(conn)) {
        cout << "[MySQL ERROR] mysql_commit failed: " << mysql_error(conn) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Private message inserted and committed successfully" << endl;
    return true;
}

vector<string> MySQL::getGroupHistory(const string& group_uid, int limit) {
    vector<string> messages;

    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return messages;
    }

    const char* sql = "SELECT sender_uid, content, sent_at FROM group_messages WHERE group_uid = ? ORDER BY sent_at DESC LIMIT ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return messages;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 绑定输入参数
    MYSQL_BIND bind_param[2];
    memset(bind_param, 0, sizeof(bind_param));

    bind_param[0].buffer_type = MYSQL_TYPE_STRING;
    bind_param[0].buffer = (char*)group_uid.c_str();
    bind_param[0].buffer_length = group_uid.length();

    bind_param[1].buffer_type = MYSQL_TYPE_LONG;
    bind_param[1].buffer = &limit;

    if (mysql_stmt_bind_param(stmt, bind_param)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 执行查询
    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 绑定结果
    char sender_uid_buf[65];
    char content_buf[1001];
    char sent_at_buf[20];
    unsigned long sender_uid_len, content_len, sent_at_len;

    MYSQL_BIND bind_result[3];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = sender_uid_buf;
    bind_result[0].buffer_length = sizeof(sender_uid_buf);
    bind_result[0].length = &sender_uid_len;

    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = content_buf;
    bind_result[1].buffer_length = sizeof(content_buf);
    bind_result[1].length = &content_len;

    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = sent_at_buf;
    bind_result[2].buffer_length = sizeof(sent_at_buf);
    bind_result[2].length = &sent_at_len;

    if (mysql_stmt_bind_result(stmt, bind_result)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 获取结果
    while (mysql_stmt_fetch(stmt) == 0) {
        string sender_uid_str(sender_uid_buf, sender_uid_len);
        string content_str(content_buf, content_len);
        string sent_at_str(sent_at_buf, sent_at_len);

        // 直接返回存储的消息内容（和Redis格式保持一致）
        // MySQL中存储的就是原始消息内容，不需要重新构建JSON
        messages.push_back(content_str);
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Retrieved " << messages.size() << " group messages" << endl;
    return messages;
}

vector<string> MySQL::getGroupHistoryAfterTime(const string& group_uid, time_t after_time, int limit) {
    vector<string> messages;

    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return messages;
    }

    cout << "[DEBUG] getGroupHistoryAfterTime: 查询群聊 '" << group_uid << "' 在时间 " << after_time << " 之后的消息" << endl;

    // 查询指定时间之后的群聊消息
    const char* sql = "SELECT sender_uid, content, sent_at FROM group_messages "
                     "WHERE group_uid = ? AND UNIX_TIMESTAMP(sent_at) > ? "
                     "ORDER BY sent_at DESC LIMIT ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return messages;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)group_uid.c_str();
    bind[0].buffer_length = group_uid.length();

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = &after_time;

    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = &limit;

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 绑定结果
    char sender_uid_buf[256], content_buf[4096];
    unsigned long sender_uid_len, content_len;
    MYSQL_TIME sent_at_time;

    MYSQL_BIND bind_result[3];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = sender_uid_buf;
    bind_result[0].buffer_length = sizeof(sender_uid_buf);
    bind_result[0].length = &sender_uid_len;

    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = content_buf;
    bind_result[1].buffer_length = sizeof(content_buf);
    bind_result[1].length = &content_len;

    bind_result[2].buffer_type = MYSQL_TYPE_TIMESTAMP;
    bind_result[2].buffer = &sent_at_time;

    if (mysql_stmt_bind_result(stmt, bind_result)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        string sender_uid_str(sender_uid_buf, sender_uid_len);
        string content_str(content_buf, content_len);

        // 格式化时间戳
        char time_str[64];
        snprintf(time_str, sizeof(time_str), "%04d-%d-%d-%02d:%02d:%02d",
                sent_at_time.year, sent_at_time.month, sent_at_time.day,
                sent_at_time.hour, sent_at_time.minute, sent_at_time.second);

        // 直接返回存储的消息内容（和Redis格式保持一致）
        messages.push_back(content_str);
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Retrieved " << messages.size() << " group messages after time " << after_time << endl;
    return messages;
}

vector<string> MySQL::getPrivateHistory(const string& user1, const string& user2, int limit) {
    vector<string> messages;

    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return messages;
    }

    cout << "[DEBUG] getPrivateHistory: 查询用户 '" << user1 << "' 和 '" << user2 << "' 之间的消息" << endl;

    const char* sql;
    bool query_all = (user1.empty() && user2.empty());

    if (query_all) {
        sql = "SELECT sender_uid, receiver_uid, content, sent_at FROM private_messages ORDER BY sent_at DESC LIMIT ?";
        cout << "[DEBUG] 查询所有消息" << endl;
    } else {
        sql = "SELECT sender_uid, receiver_uid, content, sent_at FROM private_messages "
              "WHERE (sender_uid = ? AND receiver_uid = ?) OR (sender_uid = ? AND receiver_uid = ?) "
              "ORDER BY sent_at DESC LIMIT ?";
        cout << "[DEBUG] 查询特定用户间的消息" << endl;
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return messages;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 绑定参数
    if (query_all) {
        // 查询所有消息，只需要绑定limit参数
        MYSQL_BIND bind_param[1];
        memset(bind_param, 0, sizeof(bind_param));

        bind_param[0].buffer_type = MYSQL_TYPE_LONG;
        bind_param[0].buffer = &limit;

        if (mysql_stmt_bind_param(stmt, bind_param)) {
            cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
            mysql_stmt_close(stmt);
            return messages;
        }
    } else {
        // 查询特定用户间的消息
        MYSQL_BIND bind_param[5];
        memset(bind_param, 0, sizeof(bind_param));

        bind_param[0].buffer_type = MYSQL_TYPE_STRING;
        bind_param[0].buffer = (char*)user1.c_str();
        bind_param[0].buffer_length = user1.length();

        bind_param[1].buffer_type = MYSQL_TYPE_STRING;
        bind_param[1].buffer = (char*)user2.c_str();
        bind_param[1].buffer_length = user2.length();

        bind_param[2].buffer_type = MYSQL_TYPE_STRING;
        bind_param[2].buffer = (char*)user2.c_str();
        bind_param[2].buffer_length = user2.length();

        bind_param[3].buffer_type = MYSQL_TYPE_STRING;
        bind_param[3].buffer = (char*)user1.c_str();
        bind_param[3].buffer_length = user1.length();

        bind_param[4].buffer_type = MYSQL_TYPE_LONG;
        bind_param[4].buffer = &limit;

        if (mysql_stmt_bind_param(stmt, bind_param)) {
            cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
            mysql_stmt_close(stmt);
            return messages;
        }
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 绑定结果
    char sender_uid_buf[65];
    char receiver_uid_buf[65];
    char content_buf[1001];
    MYSQL_TIME sent_at_time;
    unsigned long sender_uid_len, receiver_uid_len, content_len;

    MYSQL_BIND bind_result[4];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = sender_uid_buf;
    bind_result[0].buffer_length = sizeof(sender_uid_buf);
    bind_result[0].length = &sender_uid_len;

    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = receiver_uid_buf;
    bind_result[1].buffer_length = sizeof(receiver_uid_buf);
    bind_result[1].length = &receiver_uid_len;

    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = content_buf;
    bind_result[2].buffer_length = sizeof(content_buf);
    bind_result[2].length = &content_len;

    bind_result[3].buffer_type = MYSQL_TYPE_TIMESTAMP;
    bind_result[3].buffer = &sent_at_time;
    bind_result[3].buffer_length = sizeof(sent_at_time);

    if (mysql_stmt_bind_result(stmt, bind_result)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        string sender_uid_str(sender_uid_buf, sender_uid_len);
        string receiver_uid_str(receiver_uid_buf, receiver_uid_len);
        string content_str(content_buf, content_len);

        // 格式化时间戳，和Redis版本保持一致：2025-8-14-17:19:11
        char time_str[64];
        snprintf(time_str, sizeof(time_str), "%04d-%d-%d-%02d:%02d:%02d",
                sent_at_time.year, sent_at_time.month, sent_at_time.day,
                sent_at_time.hour, sent_at_time.minute, sent_at_time.second);

        // 构建JSON消息，和Redis格式保持一致
        json message_json;
        message_json["timeStamp"] = string(time_str);
        message_json["username"] = getUsernameFromRedis(sender_uid_str);  // 从Redis获取真实用户名
        message_json["UID_from"] = sender_uid_str;
        message_json["UID_to"] = receiver_uid_str;
        message_json["content"] = content_str;
        message_json["group_name"] = "1";  // 私聊消息的group_name设为"1"

        messages.push_back(message_json.dump());
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Retrieved " << messages.size() << " private messages" << endl;
    return messages;
}

vector<string> MySQL::getPrivateHistoryAfterTime(const string& user1_uid, const string& user2_uid, time_t after_time, int limit) {
    vector<string> messages;

    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return messages;
    }

    cout << "[DEBUG] getPrivateHistoryAfterTime: 查询用户 '" << user1_uid << "' 和 '" << user2_uid << "' 在时间 " << after_time << " 之后的消息" << endl;

    // 查询指定时间之后的消息
    const char* sql = "SELECT sender_uid, receiver_uid, content, sent_at "
                     "FROM private_messages "
                     "WHERE ((sender_uid = ? AND receiver_uid = ?) OR (sender_uid = ? AND receiver_uid = ?)) "
                     "  AND UNIX_TIMESTAMP(sent_at) > ? "
                     "ORDER BY sent_at DESC LIMIT ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return messages;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    MYSQL_BIND bind[6];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user1_uid.c_str();
    bind[0].buffer_length = user1_uid.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)user2_uid.c_str();
    bind[1].buffer_length = user2_uid.length();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)user2_uid.c_str();
    bind[2].buffer_length = user2_uid.length();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)user1_uid.c_str();
    bind[3].buffer_length = user1_uid.length();

    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = &after_time;

    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = &limit;

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 绑定结果
    char sender_uid_buf[256], receiver_uid_buf[256], content_buf[4096];
    unsigned long sender_uid_len, receiver_uid_len, content_len;
    MYSQL_TIME sent_at_time;

    MYSQL_BIND bind_result[4];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = sender_uid_buf;
    bind_result[0].buffer_length = sizeof(sender_uid_buf);
    bind_result[0].length = &sender_uid_len;

    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = receiver_uid_buf;
    bind_result[1].buffer_length = sizeof(receiver_uid_buf);
    bind_result[1].length = &receiver_uid_len;

    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = content_buf;
    bind_result[2].buffer_length = sizeof(content_buf);
    bind_result[2].length = &content_len;

    bind_result[3].buffer_type = MYSQL_TYPE_TIMESTAMP;
    bind_result[3].buffer = &sent_at_time;

    if (mysql_stmt_bind_result(stmt, bind_result)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        string sender_uid_str(sender_uid_buf, sender_uid_len);
        string receiver_uid_str(receiver_uid_buf, receiver_uid_len);
        string content_str(content_buf, content_len);

        // 格式化时间戳
        char time_str[64];
        snprintf(time_str, sizeof(time_str), "%04d-%d-%d-%02d:%02d:%02d",
                sent_at_time.year, sent_at_time.month, sent_at_time.day,
                sent_at_time.hour, sent_at_time.minute, sent_at_time.second);

        // 构建JSON格式
        json message_json;
        message_json["timeStamp"] = string(time_str);
        message_json["username"] = getUsernameFromRedis(sender_uid_str);
        message_json["UID_from"] = sender_uid_str;
        message_json["UID_to"] = receiver_uid_str;
        message_json["content"] = content_str;
        message_json["group_name"] = "1";

        messages.push_back(message_json.dump());
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Retrieved " << messages.size() << " private messages after time " << after_time << endl;
    return messages;
}

// ========== 新增：好友管理和拉黑功能实现 ==========

bool MySQL::isFriend(const string& user1, const string& user2) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    const char* sql = "SELECT COUNT(*) as cnt FROM friends WHERE (user1 = ? AND user2 = ?) OR (user1 = ? AND user2 = ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user1.c_str();
    bind[0].buffer_length = user1.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)user2.c_str();
    bind[1].buffer_length = user2.length();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)user2.c_str();
    bind[2].buffer_length = user2.length();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)user1.c_str();
    bind[3].buffer_length = user1.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 绑定结果
    int count = 0;
    MYSQL_BIND result_bind;
    memset(&result_bind, 0, sizeof(result_bind));
    result_bind.buffer_type = MYSQL_TYPE_LONG;
    result_bind.buffer = &count;

    if (mysql_stmt_bind_result(stmt, &result_bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    bool is_friend = false;
    if (mysql_stmt_fetch(stmt) == 0) {
        is_friend = (count > 0);
    }

    mysql_stmt_close(stmt);
    return is_friend;
}

bool MySQL::isBlocked(const string& user, const string& blocked_user) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    const char* sql = "SELECT COUNT(*) as cnt FROM blocks WHERE (user = ? AND blocked_user = ?) OR (user = ? AND blocked_user = ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user.c_str();
    bind[0].buffer_length = user.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)blocked_user.c_str();
    bind[1].buffer_length = blocked_user.length();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)blocked_user.c_str();
    bind[2].buffer_length = blocked_user.length();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)user.c_str();
    bind[3].buffer_length = user.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 绑定结果
    int count = 0;
    MYSQL_BIND result_bind;
    memset(&result_bind, 0, sizeof(result_bind));
    result_bind.buffer_type = MYSQL_TYPE_LONG;
    result_bind.buffer = &count;

    if (mysql_stmt_bind_result(stmt, &result_bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    bool is_blocked = false;
    if (mysql_stmt_fetch(stmt) == 0) {
        is_blocked = (count > 0);
    }

    mysql_stmt_close(stmt);
    return is_blocked;
}

bool MySQL::addFriend(const string& user1, const string& user2) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    // 添加双向好友关系
    const char* sql = "INSERT INTO friends (user1, user2) VALUES (?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 先添加 user1 -> user2
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user1.c_str();
    bind[0].buffer_length = user1.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)user2.c_str();
    bind[1].buffer_length = user2.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 重置并添加 user2 -> user1
    mysql_stmt_reset(stmt);

    bind[0].buffer = (char*)user2.c_str();
    bind[0].buffer_length = user2.length();
    bind[1].buffer = (char*)user1.c_str();
    bind[1].buffer_length = user1.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Friend relationship added: " << user1 << " <-> " << user2 << endl;
    return true;
}

bool MySQL::deleteFriend(const string& user1, const string& user2) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    const char* sql = "DELETE FROM friends WHERE (user1 = ? AND user2 = ?) OR (user1 = ? AND user2 = ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user1.c_str();
    bind[0].buffer_length = user1.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)user2.c_str();
    bind[1].buffer_length = user2.length();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)user2.c_str();
    bind[2].buffer_length = user2.length();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)user1.c_str();
    bind[3].buffer_length = user1.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);

    // 同时删除相关的屏蔽关系
    const char* block_sql = "DELETE FROM blocks WHERE (user = ? AND blocked_user = ?) OR (user = ? AND blocked_user = ?)";
    MYSQL_STMT* block_stmt = mysql_stmt_init(conn);

    if (block_stmt && mysql_stmt_prepare(block_stmt, block_sql, strlen(block_sql)) == 0) {
        if (mysql_stmt_bind_param(block_stmt, bind) == 0) {
            mysql_stmt_execute(block_stmt);
        }
        mysql_stmt_close(block_stmt);
    }

    cout << "[MySQL] Friend relationship deleted: " << user1 << " <-> " << user2 << endl;
    return true;
}

bool MySQL::blockUser(const string& user, const string& blocked_user) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    const char* sql = "INSERT INTO blocks (user, blocked_user) VALUES (?, ?) ON DUPLICATE KEY UPDATE created_at = CURRENT_TIMESTAMP";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user.c_str();
    bind[0].buffer_length = user.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)blocked_user.c_str();
    bind[1].buffer_length = blocked_user.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] User blocked: " << user << " blocked " << blocked_user << endl;
    return true;
}

bool MySQL::unblockUser(const string& user, const string& blocked_user) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    const char* sql = "DELETE FROM blocks WHERE (user = ? AND blocked_user = ?) OR (user = ? AND blocked_user = ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user.c_str();
    bind[0].buffer_length = user.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)blocked_user.c_str();
    bind[1].buffer_length = blocked_user.length();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)blocked_user.c_str();
    bind[2].buffer_length = blocked_user.length();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)user.c_str();
    bind[3].buffer_length = user.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] User unblocked: " << user << " unblocked " << blocked_user << endl;
    return true;
}

// ========== 删除好友时的消息处理方法 ==========

bool MySQL::deleteMessagesCompletely(const string& user1_uid, const string& user2_uid) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    // 完全删除两个用户之间的所有消息
    const char* sql = "DELETE FROM private_messages "
                     "WHERE (sender_uid = ? AND receiver_uid = ?) "
                     "   OR (sender_uid = ? AND receiver_uid = ?)";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user1_uid.c_str();
    bind[0].buffer_length = user1_uid.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)user2_uid.c_str();
    bind[1].buffer_length = user2_uid.length();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)user2_uid.c_str();
    bind[2].buffer_length = user2_uid.length();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)user1_uid.c_str();
    bind[3].buffer_length = user1_uid.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    bool success = (mysql_stmt_execute(stmt) == 0);
    if (success) {
        mysql_commit(conn);
        cout << "[MySQL] 完全删除用户间消息成功" << endl;
    } else {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
    }

    mysql_stmt_close(stmt);
    return success;
}

bool MySQL::deleteGroupMessages(const string& group_uid) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    // 删除群聊的所有消息
    const char* sql = "DELETE FROM group_messages WHERE group_uid = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        cout << "[MySQL ERROR] mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)group_uid.c_str();
    bind[0].buffer_length = group_uid.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    bool success = (mysql_stmt_execute(stmt) == 0);
    if (success) {
        mysql_commit(conn);
        cout << "[MySQL] 删除群聊消息成功: " << group_uid << endl;
    } else {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
    }

    mysql_stmt_close(stmt);
    return success;
}



