#include "MySQL.h"
#include "Redis.h"
#include <iostream>
#include <cstring>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

// 从Redis获取用户名
string getUsernameFromRedis(const string& uid) {
    Redis redis;
    if (!redis.connect()) {
        return uid; 
    }

    // 直接从uid_to_username哈希表获取用户名
    string username = redis.hget("uid_to_username", uid);

    if (!username.empty() && username != "(nil)") {
        return username;
    }

    cout << "[DEBUG] uid_to_username not found, trying reverse lookup for UID: " << uid << endl;

    return uid;
}

// 从Redis获取群聊名称
string getGroupNameFromRedis(const string& group_uid) {
    Redis redis;
    if (!redis.connect()) {
        return group_uid;
    }

    string group_info = redis.hget("group_info", group_uid);
    if (group_info.empty() || group_info == "(nil)") {
        return group_uid;
    }

    try {
        json group_json = json::parse(group_info);
        if (group_json.contains("groupName")) {
            return group_json["groupName"].get<string>();
        }
    } catch (const json::exception& e) {
        cout << "[DEBUG] 解析群聊信息JSON失败: " << e.what() << endl;
    }

    return group_uid;
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
    
    // 连接
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
    
    // 自动提交模式
    if (mysql_autocommit(conn, 1)) {
        cout << "[MySQL ERROR] Failed to set autocommit: " << mysql_error(conn) << endl;
        mysql_close(conn);
        conn = nullptr;
        return false;
    }

    cout << "[MySQL] Connected successfully with autocommit enabled" << endl;
    return true;
}

//保存群聊消息
bool MySQL::insertGroupMessage(const string& group_uid, const string& sender_uid, const string& content) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    // 解析JSON获取纯文本内容（如果传入的是JSON格式）
    string pure_content = content;
    try {
        json msg_json = json::parse(content);
        if (msg_json.contains("content")) {
            pure_content = msg_json["content"].get<string>();
            cout << "[DEBUG] 从JSON中提取纯文本内容: " << pure_content << endl;
        }
    } catch (const json::exception& e) {
        // 如果不是JSON格式，直接使用原内容
        cout << "[DEBUG] 非JSON格式，直接存储: " << content << endl;
        pure_content = content;
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

    // 存储纯文本内容
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)pure_content.c_str();
    bind[2].buffer_length = pure_content.length();

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
    cout << "[MySQL] Group message inserted successfully (pure content)" << endl;
    return true;
}

//已经是建立在之前已经connect过的前提下了，conn已经填入信息成功了
bool MySQL::insertPrivateMessage(const string& sender_uid, const string& receiver_uid, const string& content) {
    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return false;
    }

    // 存储消息内容，sender_uid和receiver_uid用于查询索引
    const char* sql = "INSERT INTO private_messages (sender_uid, receiver_uid, content) VALUES (?, ?, ?)";
   //菜单与对象绑定
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    
    if (!stmt) {
        cout << "[MySQL ERROR] mysql_stmt_init failed" << endl;
        return false;
    }
    //预处理
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

    //绑定参数，？对应的东西
    if (mysql_stmt_bind_param(stmt, bind)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }
    //执行
    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Private message inserted successfully" << endl;
    return true;
}



vector<string> MySQL::getGroupHistoryAfterTime(const string& group_uid, time_t after_time, int limit) {
    vector<string> messages;

    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return messages;
    }

    cout << "[DEBUG] getGroupHistoryAfterTime: 查询群聊 '" << group_uid << "' 在时间 " << after_time << " 之后的消息" << endl;

    // 查询指定时间之后的群聊消息——————————————命令内容
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
        string content_str(content_buf, content_len);  // 现在是纯文本内容

        // 格式化时间戳
        char time_str[64];
        snprintf(time_str, sizeof(time_str), "%04d-%d-%d-%02d:%02d:%02d",
                sent_at_time.year, sent_at_time.month, sent_at_time.day,
                sent_at_time.hour, sent_at_time.minute, sent_at_time.second);

        // 重新构建JSON格式（与Redis格式保持一致）
        json message_json;
        message_json["timeStamp"] = string(time_str);
        message_json["username"] = getUsernameFromRedis(sender_uid_str);  // 从Redis获取最新用户名
        message_json["UID_from"] = sender_uid_str;
        message_json["UID_to"] = group_uid;
        message_json["content"] = content_str;  // 纯文本内容
        message_json["group_name"] = getGroupNameFromRedis(group_uid);  // 从Redis获取群名

        messages.push_back(message_json.dump());
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Retrieved " << messages.size() << " group messages after time " << after_time << endl;
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
        cout << "[MySQL] 删除群聊消息成功: " << group_uid << endl;
    } else {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
    }

    mysql_stmt_close(stmt);
    return success;
}



