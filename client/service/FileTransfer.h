#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <vector>
#include "User.h"
#include "Group.h"

class FileTransfer {
public:
    FileTransfer() ;  // 添加默认构造函数
    FileTransfer(int fd, User user);

    // 私聊文件传输
    void sendFile_Friend(const User& targetUser, const User& myUser) const;
    void recvFile_Friend( User& myUser) const;

    // 群聊文件传输
    void sendFile_Group(const Group& targetGroup, const User& myUser) const ;
    void recvFile_Group(const User& myUser,string G_uid) const;

    //线程
    void  sendFileThread(int fd, const User& targetUser, const User& myUser, int inputFile, off_t fileSize, off_t offset,string filePath) const;
    void  recvFileThread(int fd, string fileName,  off_t size, string filePath)const;

    void  G_sendFileThread(int fd, const Group& targetUser, const User& myUser, int inputFile, off_t fileSize, off_t offset,string filePath) const;
    void  G_recvFileThread(int fd, string fileName,  off_t size, string filePath)const;



   
    private:
    int fd;
    User user;
};

#endif //FILE_TRANSFER_H 