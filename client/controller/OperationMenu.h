#ifndef CHATROOM_OPERATIONMENU_H
#define CHATROOM_OPERATIONMENU_H

#include <vector>
#include <string>
#include <utility>
#include "User.h"

void operationMenu();

void clientOperation(int fd, User &user);
void syncFriends(int fd, std::string my_uid, std::vector<std::pair<std::string, User>> &my_friends);

void deactivateAccount(int fd, User &user);

#endif //CHATROOM_OPERATIONMENU_H
