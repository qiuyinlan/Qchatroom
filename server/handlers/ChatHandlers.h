#ifndef CHATHANDLERS_H
#define CHATHANDLERS_H

#include "nlohmann/json.hpp"

// Function declarations for chat-related business logic
void handleGetChatLists(int epfd, int fd, const nlohmann::json& msg);
void handleStartChatRequest(int epfd, int fd, const nlohmann::json& msg);
void handlePrivateMessage(int epfd, int fd, const nlohmann::json& msg);
void handleGroupMessage(int epfd, int fd, const nlohmann::json& msg);
void handleExitChatRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetHistoryRequest(int epfd, int fd, const nlohmann::json& msg);

#endif // CHATHANDLERS_H

