#ifndef CHATROOM_SERVER_IO_H
#define CHATROOM_SERVER_IO_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstring> // For memset

// Structure to manage receiving data for each connection
// Handles message framing (length prefix + data)
struct RecvBuffer {
    char header_buf[4];
    int header_read = 0;
    int msg_len = 0;
    std::vector<char> msg_buf;
    int msg_read = 0;

    void reset() {
        memset(header_buf, 0, 4);
        header_read = 0;
        msg_len = 0;
        msg_buf.clear();
        msg_read = 0;
    }
};

// Structure to manage sending data for each connection
struct SendBuffer {
    std::vector<char> data;
    size_t sent_bytes = 0;
};

// Global maps to store buffers for each file descriptor
// Declared here, defined in IO.cc
extern std::unordered_map<int, RecvBuffer> recvBuffers;
extern std::unordered_map<int, SendBuffer> sendBuffers;

// Function Prototypes
void return_last();
int write_n(int fd, const char *msg, int n);
int sendMsg(int fd, std::string msg);
int read_n(int fd, char *msg, int n);
int recvMsg(int fd, std::string &msg);
void clearBuffers(int fd);


void clearBuffers(int fd);

#endif // CHATROOM_SERVER_IO_H
