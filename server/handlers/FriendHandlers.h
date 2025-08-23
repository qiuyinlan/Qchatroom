#ifndef FRIENDHANDLERS_H
#define FRIENDHANDLERS_H

#include "nlohmann/json.hpp"

// Function declarations for friend-related business logic
void handleAddFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetFriendRequests(int epfd, int fd, const nlohmann::json& msg);
void handleRespondToFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleDeleteFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleBlockFriendRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetBlockedListRequest(int epfd, int fd, const nlohmann::json& msg);
void handleUnblockFriendRequest(int epfd, int fd, const nlohmann::json& msg);

#endif // FRIENDHANDLERS_H

