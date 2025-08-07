#ifndef CHATROOM_NOTIFICATIONS_H
#define CHATROOM_NOTIFICATIONS_H

#include "User.h"

#pragma once
#include <atomic>
#include <vector>
#include <mutex>
#include <string>

extern std::atomic<bool> stopNotify;




// 新的统一消息处理
void unifiedMessageReceiver(string UID);
void processUnifiedMessage(const string& msg);
void heartbeat(string UID);
// 全局状态管理类完整定义
class ClientState {
public:
    static bool inChat;
    static std::string currentChatUID;
    static std::string myUID;
   
    static std::mutex messageMutex;

    static void enterChat(const std::string& targetUID);
    static void exitChat();

};



#endif //CHATROOM_NOTIFICATIONS_H
