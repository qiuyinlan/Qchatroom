#ifndef FILEHANDLERS_H
#define FILEHANDLERS_H

#include "nlohmann/json.hpp"

// Function declarations for file transfer-related business logic
void handleSendFileRequest(int epfd, int fd, const nlohmann::json& msg);
void handleRecvFileRequest(int epfd, int fd, const nlohmann::json& msg);

// Function declarations for low-level file data handling
void handleFileData(int epfd, int fd, const char* data, int len);
void handleFileWriteEvent(int epfd, int fd);
void cleanupFileTransfer(int fd);

#endif // FILEHANDLERS_H

