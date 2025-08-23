#ifndef FRIEND_MANAGER_H
#define FRIEND_MANAGER_H

#include <vector>
#include "User.h"
#include "Group.h"

class FriendManager {
public:
    FriendManager(int fd, User user);

    void listFriends(std::vector<std::pair<std::string, User>> &, std::vector<Group> &);
    void addFriend(std::vector<std::pair<std::string, User>> &) const;
    void findRequest(std::vector<std::pair<std::string, User>> &my_friends) const;
    void delFriend(std::vector<std::pair<std::string, User>> &);
    void blockedLists(std::vector<std::pair<std::string, User>> &my_friends) const;
    void unblocked(std::vector<std::pair<std::string, User>> &my_friends) const;

private:
    int fd;
    User user;
};

#endif //FRIEND_MANAGER_H