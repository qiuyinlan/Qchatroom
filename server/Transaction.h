#ifndef CHATROOM_TRANSACTION_H
#define CHATROOM_TRANSACTION_H

#include "../utils/User.h"

//业务处理函数，对应实现的所有功能
 
void start_chat_mysql(int fd, User &user);  
void add_friend(int fd, User &user);
void del_friend(int fd, User &user);
void blockedLists(int fd, User &user);
void unblocked(int fd, User &user);

void G_history(int fd, User &user);
void F_history(int fd, User &user); 

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
