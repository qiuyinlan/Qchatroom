
#ifndef CHATROOM_REDIS_H
#define CHATROOM_REDIS_H

#include <hiredis/hiredis.h>
#include <string>

using std::string;



//使用前先启动redis-server
class Redis {
private:
    redisContext *context;
    redisReply *reply;
public:
    Redis();
    ~Redis();
    bool connect();
    bool del(const std::string &key);
    bool sadd(const std::string &key, const std::string &value);
    bool sismember(const std::string &key, const std::string &value);
    int scard(const std::string &key);
    redisReply **smembers(const std::string &key);
    void srem(const std::string &key, const std::string &value);
    std::string hget(const std::string &key, const std::string &field);
    // 确保声明了 hset 方法
    bool hset(const std::string &key, const std::string &field, const std::string &value);
    // 确保声明了 hdel 方法
    bool hdel(const std::string &key, const std::string &field);
    // 确保声明了 hexists 方法
    bool hexists(const std::string &key, const std::string &field);
    int llen(const std::string &key);
    redisReply **lrange(const std::string &key, const std::string &start, const std::string &stop);
    redisReply **lrange(const std::string &key);
    void lpush(const std::string &key, const std::string &value);
    void ltrim(const std::string &key);
    redisReply **hgetall(const std::string &key);
    int hlen(const std::string &key);
    redisContext* getContext() { return context; }  // 添加获取context的方法
};

#endif  // CHATROOM_REDIS_H
