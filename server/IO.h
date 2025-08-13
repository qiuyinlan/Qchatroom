#ifndef CHATROOM_SERVER_IO_H
#define CHATROOM_SERVER_IO_H

#include <string>

void return_last();
int write_n(int fd, const char *msg, int n);

int sendMsgET(int fd, std::string msg);

int read_n(int fd, char *msg, int n);

int recvMsgET(int fd, std::string &msg);

void clearBuffers(int fd);

#endif // CHATROOM_SERVER_IO_H
