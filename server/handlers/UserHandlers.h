#ifndef USERHANDLERS_H
#define USERHANDLERS_H

#include "nlohmann/json.hpp"

// Function declarations for user account-related business logic
void handleLoginRequest(int epfd, int fd, const nlohmann::json& msg);
void handleLogoutRequest(int epfd, int fd, const nlohmann::json& msg);
void handleDeactivateAccountRequest(int epfd, int fd, const nlohmann::json& msg);
void handleHeartbeat(int epfd, int fd, const nlohmann::json& msg);

#endif // USERHANDLERS_H


