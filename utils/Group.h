

#ifndef CHATROOM_GROUP_H
#define CHATROOM_GROUP_H

#include <string>

class Group {
public:
    //=default 让编译器自动生成构造函数
    Group() ;

    Group(std::string groupName, std::string UID);

    std::string to_json();

    [[nodiscard]] const std::string &getGroupName() const;

    [[nodiscard]] const std::string &getOwnerUid() const;

    [[nodiscard]] const std::string &getGroupUid() const;

    [[nodiscard]] const std::string &getMembers() const;

    [[nodiscard]] const std::string &getAdmins() const;

    void json_parse(const std::string &json);

private:
    std::string groupName;
    std::string UID;
    std::string groupUID;
    std::string members;
    std::string admins;
};


#endif //CHATROOM_GROUP_H
