#include "MySQL.h"
#include <iostream>
#include <cstring>

using namespace std;

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
                           "localhost",     // 主机
                           "root",          // 用户名
                           "password",      // 密码，你需要改成你的MySQL密码
                           "chatroom",      // 数据库名
                           3306,            // 端口
                           nullptr, 
                           0)) {
        cout << "[MySQL ERROR] Connection failed: " << mysql_error(conn) << endl;
        mysql_close(conn);
        conn = nullptr;
        return false;
    }
    
    cout << "[MySQL] Connected successfully" << endl;
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
    
    mysql_stmt_close(stmt);
    cout << "[MySQL] Private message inserted successfully" << endl;
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

        // 格式化消息：发送者|内容|时间
        string formatted_msg = sender_uid_str + "|" + content_str + "|" + sent_at_str;
        messages.push_back(formatted_msg);
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Retrieved " << messages.size() << " group messages" << endl;
    return messages;
}

vector<string> MySQL::getPrivateHistory(const string& user1, const string& user2, int limit) {
    vector<string> messages;

    if (!conn) {
        cout << "[MySQL ERROR] Not connected" << endl;
        return messages;
    }

    const char* sql = "SELECT sender_uid, receiver_uid, content, sent_at FROM private_messages "
                     "WHERE (sender_uid = ? AND receiver_uid = ?) OR (sender_uid = ? AND receiver_uid = ?) "
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

    // 绑定参数
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

    if (mysql_stmt_execute(stmt)) {
        cout << "[MySQL ERROR] mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    // 绑定结果
    char sender_uid_buf[65];
    char receiver_uid_buf[65];
    char content_buf[1001];
    char sent_at_buf[20];
    unsigned long sender_uid_len, receiver_uid_len, content_len, sent_at_len;

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

    bind_result[3].buffer_type = MYSQL_TYPE_STRING;
    bind_result[3].buffer = sent_at_buf;
    bind_result[3].buffer_length = sizeof(sent_at_buf);
    bind_result[3].length = &sent_at_len;

    if (mysql_stmt_bind_result(stmt, bind_result)) {
        cout << "[MySQL ERROR] mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return messages;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        string sender_uid_str(sender_uid_buf, sender_uid_len);
        string receiver_uid_str(receiver_uid_buf, receiver_uid_len);
        string content_str(content_buf, content_len);
        string sent_at_str(sent_at_buf, sent_at_len);

        // 格式化消息：发送者->接收者|内容|时间
        string formatted_msg = sender_uid_str + "->" + receiver_uid_str + "|" + content_str + "|" + sent_at_str;
        messages.push_back(formatted_msg);
    }

    mysql_stmt_close(stmt);
    cout << "[MySQL] Retrieved " << messages.size() << " private messages" << endl;
    return messages;
}
