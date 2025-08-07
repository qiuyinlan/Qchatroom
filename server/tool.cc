#include "tools.h"
#include <iostream>
#include <unistd.h>

bool c_break(int ret, int fd, User &user) {
    Redis redis;
    redis.connect();
    if (ret <= 0) {
        std::cout << "[INFO] tool客户端 " << fd << " 断开连接" << std::endl;
        redis.hdel("is_online", user.getUID());
        redis.hdel("unified_receiver", user.getUID());
        close(fd);
        return false;
    }
    return true;
}