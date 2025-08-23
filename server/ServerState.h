#ifndef SERVERSTATE_H
#define SERVERSTATE_H

#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <mutex>
#include <ctime>
#include "MySQL.h"
#include "FileTransferState.h"
#include "IO.h"

// Global MySQL connection object
extern MySQL g_mysql;

// File Transfer State Management
extern std::map<int, FileTransferState> fd_transfer_states;
extern std::mutex transfer_states_mutex;

// Heartbeat and user activity
extern std::unordered_map<std::string, time_t> last_activity;
extern std::unordered_map<int, std::string> fd_to_uid;
extern std::mutex activity_mutex;

// Function declarations for shared utility functions
void checkHeartbeatTimeout();
void updateUserActivity(const std::string& uid);
void addFdToUid(int fd, const std::string& uid);
void removeFdFromUid(int fd);
std::string getUidByFd(int fd);
void removeUserActivity(const std::string& uid);
void removeFdMapping(int fd);
void sendMsg(int epfd, int fd, std::string msg);

#endif // SERVERSTATE_H

