#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <string>
#include "User.h"
#include "Group.h"

class FileTransfer {
public:
    FileTransfer();

    // The 'fd' parameter is the main connection fd.
    void sendFile_Friend(int fd, const User& myUser, const User& targetUser) const;
    void recvFile_Friend(int fd, const User& myUser) const;

    void sendFile_Group(int fd, const User& myUser, const Group& targetGroup) const;
    void recvFile_Group(int fd, const User& myUser, const std::string& G_uid) const;
};

#endif //FILE_TRANSFER_H