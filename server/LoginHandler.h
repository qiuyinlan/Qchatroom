#ifndef LOGINHANDLER_H
#define LOGINHANDLER_H

#include <string>
#include "../utils/User.h"

// 处理统一接收线程连接的函数
void handleRequestOfflineNotifications(int epfd, int fd, const nlohmann::json& msg);

// 发送离线通知
void notify(int fd, const std::string &UID);



#endif // LOGINHANDLER_H

