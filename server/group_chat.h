

#ifndef CHATROOM_GROUP_CHAT_H
#define CHATROOM_GROUP_CHAT_H

#include <utility>
#include <string>

#include "User.h"
#include "Group.h"
#include "Redis.h"

class GroupChat {
public:
    GroupChat() = default;

    GroupChat(int fd, const User &user);

    void group(int fd, User &user);
    
    void synchronizeGL(int fd, User &user) ;

    void sync();

    void startChat();

    void createGroup();

    void joinGroup();


    void managedGroup() const;

    void approve(Group &group) const;

    void remove(Group &group) const;


    void appointAdmin(Group &createdGroup) const;

    void revokeAdmin(Group &createdGroup) const;

    static void deleteGroup(Group &createdGroup);

    void showMembers() const;

    void quit();

    void getperson();
private:
    int fd;
    User user;
    string joined;
    string managed;
    string created;

    // 辅助函数：通过群名查找群UID
    string findGroupUidByName(Redis& redis, const string& groupName);
};

#endif //CHATROOM_GROUP_CHAT_H
