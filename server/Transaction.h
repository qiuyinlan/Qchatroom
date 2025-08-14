#ifndef CHATROOM_TRANSACTION_H
#define CHATROOM_TRANSACTION_H

#include "../utils/User.h"

//业务处理函数，对应实现的所有功能

// ========== MySQL版本（仅消息存储和历史记录） ==========
void F_history_mysql(int fd, User &user);  // 历史消息获取用MySQL
void start_chat_mysql(int fd, User &user);  // 聊天消息存储用MySQL

// ========== Redis版本（好友管理、屏蔽等保持原样） ==========
void F_history(int fd, User &user);
void start_chat(int fd, User &user);
void add_friend(int fd, User &user);
void del_friend(int fd, User &user);
void blockedLists(int fd, User &user);
void unblocked(int fd, User &user);
void G_history(int fd, User &user);
void sendFile_Friend(int epfd, int fd);
void recvFile_Friend(int epfd, int fd);
void sendFile_Group(int epfd, int fd);
void recvFile_Group(int epfd, int fd);
void synchronize(int fd, User &user);
void list_friend(int fd, User &user);
void findRequest(int fd, User &user);



// void synchronizeGL(int fd, User &user);



void deactivateAccount(int fd, User &user);

#endif //CHATROOM_TRANSACTION_H
