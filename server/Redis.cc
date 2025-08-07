#include "Redis.h"
#include <iostream>

using namespace std;

Redis::Redis() : context(nullptr), reply(nullptr) {}
Redis::~Redis() {
    redisFree(context);
    context = nullptr;
    reply = nullptr;
}

bool Redis::connect() {
    context = redisConnect("127.0.0.1", 6379); // 尝试连接 Redis 服务器

    if (context == nullptr || context->err) { // 检查连接是否成功
        if (context->err) {
            cout << "Redis connection error: " << context->errstr << endl;
        } else {
            cout << "Redis connection error: can't allocate redis context (null context)" << endl;
        }
        // 如果连接失败，打印错误信息后，立即返回 false
        return false; // <--- 关键的修改点，确保返回 false
    } else {
        
        return true; // <--- 连接成功时返回 true
    }
}

bool Redis::del(const std::string &key) {
    std::string command = "del " + key;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    bool success = reply != nullptr && reply->type != REDIS_REPLY_ERROR;
    freeReplyObject(reply);
    return success;
}

bool Redis::sadd(const std::string &key, const std::string &value) {
    std::string command = "SADD " + key + " " + value;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    bool success = reply != nullptr && reply->type != REDIS_REPLY_ERROR;
    freeReplyObject(reply);
    return success;
}

bool Redis::sismember(const string &key, const string &value) {
    string command = "SISMEMBER " + key + " " + value;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    bool is_member = reply->integer > 0;
    freeReplyObject(reply);
    return is_member;
}

int Redis::scard(const string &key) {
    string command = "SCARD " + key;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    int integer = reply->integer;
    freeReplyObject(reply);
    return integer;
}

redisReply **Redis::smembers(const string &key) {
    string command = "SMEMBERS " + key;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        cout << "[REDIS ERROR] SMEMBERS command failed or returned wrong type" << endl;
        return nullptr;
    }
    return reply->element;
}

void Redis::srem(const string &key, const string &value) {
    string command = "SREM " + key + " " + value;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    freeReplyObject(reply);
}

string Redis::hget(const string &key, const string &field) {
    string command = "HGET " + key + " " + field;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
  
    string get_info;
    if (reply != nullptr && reply->str != nullptr) {
        get_info = reply->str;
    } else {
        get_info = ""; // 返回空字符串而不是nullptr
    }
    freeReplyObject(reply);
    return get_info;
}

bool Redis::hset(const string &key, const string &field, const string &value) {
    // 使用参数化命令，避免字符串拼接导致的解析问题
    reply = static_cast<redisReply *>(redisCommand(context, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str()));
    if (reply == nullptr) {
        cout << "[REDIS ERROR] HSET command failed" << endl;
        return false;
    }
    bool success = reply->type != REDIS_REPLY_ERROR;
    if (!success) {
        cout << "[REDIS ERROR] HSET error: " << reply->str << endl;
    }
    freeReplyObject(reply);
    return success;
}

bool Redis::hdel(const string &key, const string &field) {
    string command = "HDEL " + key + " " + field;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    bool success = reply != nullptr && reply->type != REDIS_REPLY_ERROR;
    freeReplyObject(reply);
    return success;
}

bool Redis::hexists(const string &key, const string &field) {
    string command = "HEXISTS " + key + " " + field;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    bool exist = reply->integer > 0;
    freeReplyObject(reply);
    return exist;
}

int Redis::llen(const string &key) {
    string command = "LLEN " + key;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    int integer = reply->integer;
    freeReplyObject(reply);
    return integer;
}

redisReply **Redis::lrange(const string &key, const string &start, const string &stop) {
    string command = "LRANGE " + key + " " + start + " " + stop;
    reply = static_cast<redisReply *>(redisCommand(context, command.data()));
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        cout << "[REDIS ERROR] LRANGE command failed or returned wrong type" << endl;
        return nullptr;
    }
    return reply->element;
}

redisReply **Redis::lrange(const string &key) {
    string command = "LRANGE " + key + " 0" + " -1";
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        cout << "[REDIS ERROR] LRANGE command failed or returned wrong type" << endl;
        return nullptr;
    }
    return reply->element;
}

void Redis::lpush(const string &key, const string &value) {
    // 使用参数化命令，避免字符串拼接导致的解析问题
    reply = static_cast<redisReply *>(redisCommand(context, "LPUSH %s %s", key.c_str(), value.c_str()));
    if (reply == nullptr) {
        cout << "[REDIS ERROR] LPUSH command failed" << endl;
        return;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        cout << "[REDIS ERROR] LPUSH error: " << reply->str << endl;
    }
    freeReplyObject(reply);
}

void Redis::ltrim(const string &key) {
    string command = "LTRIM " + key + " 1 0";
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    freeReplyObject(reply);
}

redisReply **Redis::hgetall(const string &key) {
    string command = "HGETALL " + key;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    return reply->element;
}

int Redis::hlen(const string &key) {
    string command = "HLEN " + key;
    reply = static_cast<redisReply *>(redisCommand(context, command.c_str()));
    int integer = reply->integer;
    freeReplyObject(reply);
    return integer;
}
