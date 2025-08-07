
#ifndef CHATROOM_LOGINHANDLER_H
#define CHATROOM_LOGINHANDLER_H

#include "User.h"


//以下是登陆后的业务派发

void serverLogin(int epfd, int fd);

void serverOperation(int fd, User &user);

void notify(int fd, const std::string &UID);

void handleUnifiedReceiver(int epfd, int fd);

//以下是注册登陆函数

void handleRequestCode(int epfd, int fd);   //检查重复，发送验证码

void serverRegisterWithCode(int epfd, int fd);  //检查后，带着验证码注册

void handleResetCode(int epfd, int fd);

void resetPasswordWithCode(int epfd, int fd);

bool sendMail(const std::string& to_email, const std::string& code, bool is_find);

void findPasswordWithCode(int epfd, int fd);

#endif  
