#ifndef CHAT_H
#define CHAT_H
#include <vector>
#include <string>
#include "../utils/User.h"
#include "../utils/Group.h"

using namespace std;

class ChatSession {
public:
    ChatSession(int fd, User user);
    void startChat(vector<pair<string, User>> &my_friends,vector<Group> &joinedGroup);
   
private:
   
    int fd;
    void startGroupChat(int groupIndex, const vector<Group>& joinedGroups);
    User user;
};

// 全局函数声明
void groupChatReceived(int fd, const string& groupUid);

#endif //CHAT_H