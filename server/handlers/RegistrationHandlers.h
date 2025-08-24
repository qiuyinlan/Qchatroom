#ifndef REGISTRATIONHANDLERS_H
#define REGISTRATIONHANDLERS_H

// These functions will handle multi-step, synchronous-style interactions
// for registration and password reset. They will temporarily take control
// of the file descriptor from the main epoll loop.

#include "nlohmann/json.hpp"

void handleRequestCode(int epfd, int fd, const nlohmann::json& msg);
void serverRegisterWithCode(int epfd, int fd, const nlohmann::json& msg);
void handleResetCode(int epfd, int fd, const nlohmann::json& msg);

#endif // REGISTRATIONHANDLERS_H

