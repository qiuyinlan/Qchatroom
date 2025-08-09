#include "FileTransfer.h"
#include "../utils/proto.h"
#include "../utils/IO.h"
#include "../utils/Group.h"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <fstream>
#include <thread>
#include "../utils/TCP.h"

using namespace std;

// 默认构造函数
FileTransfer::FileTransfer() : fd(-1), user() {}

// 是为了得到好友的user——,好友也是user类，让服务器得到信息，帮忙发送
FileTransfer::FileTransfer(int fd, User user) : fd(fd), user(std::move(user)) {}


// 私聊发送文件
void FileTransfer::sendFile_Friend(const User& targetUser, const User& myUser) const {
    // 发送文件传输协议
      int fd = Socket();
    Connect(fd, IP, PORT);
    sendMsg(fd, SENDFILE_F);

    string filePath;
    int inputFile;
    off_t offset = 0;
    struct stat fileStat;
    while (true) {
        cout << "请输入发送文件的绝对路径" << endl;
          std::cout << "\033[90m输入【0】取消发送文件，返回聊天\033[0m" << std::endl;

        getline(cin,filePath);
        if (cin.eof()) {
            cout << "读到文件结尾" << endl;
            return;
        }
        //按0返回
        if(filePath == "0" ){
            std::cout << "\033[90m已取消发送文件\033[0m" << std::endl;
            // 不要发送"0"给服务器，直接返回
            return;
        }
        inputFile = open(filePath.c_str(), O_RDONLY);
    
    // bug目录路径是可以打开的，但是不可以用目录路径去sendfile,会崩溃！！！
        if (inputFile == -1) {
            cerr << "无法打开文件,请检查输入路径" << endl;
            continue;
        }
        offset = 0;
        if (fstat(inputFile, &fileStat) == -1) {
            cerr << "获取文件状态失败" << endl;
            close(inputFile);
            continue;
        }

       if (!S_ISREG(fileStat.st_mode)) {
            cerr << RED <<"该路径不是普通文件，不能发送（可能是目录路径!!!）"<<RESET << endl;
            close(inputFile);
            continue;
        }
        break;
    }

     sendMsg(fd, filePath);
     filesystem::path path(filePath);
     string fileName = path.filename().string();
     sendMsg(fd, fileName);


    off_t fileSize = fileStat.st_size;

    thread sendfileThread([=]() {
    this->sendFileThread(fd, targetUser, myUser, inputFile, fileSize, offset, filePath);
});
sendfileThread.detach();


 }
   
 
 
void FileTransfer::sendFileThread(int fd,const User& targetUser, const User& myUser, int inputFile, off_t fileSize, off_t offset,string filePath) const{

   
    //发
    User targetCopy = targetUser;
    sendMsg(fd, targetCopy.to_json());

    User myCopy = myUser;
    sendMsg(fd, myCopy.to_json());


    
    sendMsg(fd, to_string(fileSize));
    //发
    

   ssize_t totalSent = 0;
    while (totalSent < fileSize) {
        ssize_t n = sendfile(fd, inputFile, &offset, fileSize - totalSent);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000); // 或用 epoll 写事件更优雅
                continue;
            }
            perror("sendfile failed");
            sendMsg(fd, "no");
            close(inputFile);
            close(fd);
            return;
        }
        totalSent += n;
    }



    system("sync");
   
    //注意，写入内核缓冲区！=成功，只有写入+服务器成功读入，才算真的成功
    string result ;
    recvMsg(fd, result);
    if (result == "no") {
        cerr << "服务器写入文件失败，请稍后重试" << endl;
        close(inputFile);
        close(fd);
        return;
    }
    filesystem::path path(filePath);
     string fileName = path.filename().string();
    cout << "文件" << fileName << "发送完成" << endl;
    close(inputFile);
    close(fd);
}
 
void FileTransfer::recvFile_Friend(User& myUser) const {
    int fd = Socket();
    Connect(fd, IP, PORT);
    sendMsg(fd, RECVFILE_F);

    sendMsg(fd, myUser.to_json());
    string nums;
    Message message;
    string filePath;
    string fileName;
    //先接收服务器发来的文件数
    int ret = recvMsg(fd, nums);
    if (ret <= 0) {
        cout << "服务器断开连接" << endl;
        return;
    }
    int num;
    try {
        num = stoi(nums);
    } catch (const exception& e) {
        cout << "[ERROR] 解析文件数量失败: " << e.what() << endl;
        cout << "[ERROR] 接收到的内容: '" << nums << "'" << endl;
        return;
    }
    if (num == 0) {
        cout << "当前没有要接收的文件" << endl;
        close(fd);
        return;
    }
    cout << "你有" << num << "个文件待接收" << endl;
    
    string file_info;
  
    recvMsg(fd, file_info);
    message.json_parse(file_info);
   
    string reply;
    recvMsg(fd, reply);
    if (reply == "FILE_NOT_FOUND") {
        cout << "接收失败，文件在服务器已经不存在或无法访问" << endl;
        return;
    }

    cout << "是否要接收[1]YES, [0]NO" << endl;
     
   
    int choice;
    while (!(cin >> choice) || (choice != 0 && choice != 1)) {
        cout << "输入格式错误" << endl;
        cin.clear();
        cin.ignore(INT32_MAX, '\n');
    }
    cin.ignore(INT32_MAX, '\n');
    
    if (choice == 1) {
        reply = "YES";
    } else{
        reply = "NO";
    }

    sendMsg(fd, reply);
    if (choice == 0) {
        string temp;
        cout << "你拒绝接收了该文件, 按enter返回" << endl;
        getline(cin, temp);
        close(fd);
        return;
    }
    fileName = message.getGroupName();
    filePath = "./fileBuffer_recv/" + fileName;
    if (!filesystem::exists("./fileBuffer_recv/")) {
        filesystem::create_directories("./fileBuffer_recv/");
    }
    
    string ssize;
    recvMsg(fd, ssize);
    off_t size = stoll(ssize);

    thread recvfileThread([=]() {
    this->recvFileThread(fd, fileName, size, filePath);
});
    // 分离线程，避免阻塞
    recvfileThread.detach();
    
 }
       
 

void  FileTransfer::recvFileThread(int fd, string fileName,  off_t size, string filePath) const{
     char buf[BUFSIZ];
     int n;
     ofstream ofs(filePath);
     while (size > 0) {
            if (size > sizeof(buf)) {
                n = read_n(fd, buf, sizeof(buf));
            } else {
                n = read_n(fd, buf, size);
            }
            size -= n;
            ofs.write(buf, n);
      }
        cout << "文件 " << fileName << "接收完成" << endl;
        ofs.close();
        close(fd);
}




// 群聊发送文件
void FileTransfer::sendFile_Group( const Group& targetGroup, const User& myUser) const {
    // 发送群文件传输协议
     int fd = Socket();
    Connect(fd, IP, PORT);
    sendMsg(fd, SENDFILE_G);

    User userCopy = myUser;
    sendMsg(fd, userCopy.to_json());

    // 创建Group副本来调用to_json
    Group groupCopy = targetGroup;
    sendMsg(fd, groupCopy.to_json());

    string filePath;
    int inputFile;
    off_t offset = 0;
    struct stat fileStat;

    while (true) {
        cout << "请输入发送文件的绝对路径" << endl;
        cout << "\033[90m输入【0】取消发送文件，返回聊天\033[0m" << endl;

        getline(cin, filePath);
        if (cin.eof()) {
            cout << "读到文件结尾" << endl;
            return;
        }

        // 按0返回
        if (filePath == "0") {
            cout << "\033[90m已取消发送文件\033[0m" << endl;
            // 不要发送"0"给服务器，直接返回
            return;
        }

        inputFile = open(filePath.c_str(), O_RDONLY);
        if (inputFile == -1) {
            cerr << "无法打开文件,请检查输入路径" << endl;
            continue;
        }

        offset = 0;
        if (fstat(inputFile, &fileStat) == -1) {
            cerr << "获取文件状态失败" << endl;
            close(inputFile);
            continue;
        }
        //bug目录路径可以打开的
        if (!S_ISREG(fileStat.st_mode)) {
            cerr << RED <<"该路径不是普通文件，不能发送（可能是目录路径!!!）"<<RESET << endl;
            close(inputFile);
            continue;
        }
       
        break;
    }
    
     sendMsg(fd, filePath);
    filesystem::path path(filePath);
    string fileName = path.filename().string();
    sendMsg(fd, fileName);

    off_t fileSize = fileStat.st_size;


     thread G_sendfileThread([=]() {
    this->G_sendFileThread(fd, targetGroup, myUser, inputFile, fileSize, offset, filePath);
});
G_sendfileThread.detach();

    
}

void FileTransfer::G_sendFileThread(int fd,const Group& targetUser, const User& myUser, int inputFile, off_t fileSize, off_t offset,string filePath) const{

   sendMsg(fd, to_string(fileSize));
cout << "文件大小: " << fileSize << endl;

    ssize_t totalSent = 0;
    while (totalSent < fileSize) {
        ssize_t n = sendfile(fd, inputFile, &offset, fileSize - totalSent);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000); // 或用 epoll 写事件更优雅
                continue;
            }
            perror("sendfile failed");
            sendMsg(fd, "no");
            close(inputFile);
            close(fd);
            return;
        }
        totalSent += n;
    }

    system("sync");
    
    string result ;
    recvMsg(fd, result);
    if (result == "no") {
        cerr << "服务器写入文件失败，请稍后重试" << endl;
        close(inputFile);
        close(fd);
        return;
    }
    filesystem::path path(filePath);
     string fileName = path.filename().string();
    cout << "文件" << fileName << "发送完成" << endl;
    close(inputFile);
    close(fd);

}
// 群聊接收文件
void FileTransfer::recvFile_Group( const User& myUser,string G_uid) const {
    int fd = Socket();
    Connect(fd, IP, PORT);
    sendMsg(fd, RECVFILE_G);

    User userCopy = myUser;
    sendMsg(fd, userCopy.to_json());
    
    string nums;
    sendMsg(fd, G_uid);
    recvMsg(fd, nums);
    int num = stoi(nums);

    if (num == 0) {
        cout << "暂无群聊文件可接收" << endl;
        close(fd);
        return;
    }

    cout << "你有" << num << "个群聊文件待接收" << endl;

    Message message;
    string file_info;
    recvMsg(fd, file_info);
    message.json_parse(file_info);

    // 从content中提取文件名（去掉路径前缀）
    string fullPath = message.getContent();
    string fileName = fullPath.substr(fullPath.find_last_of("/\\") + 1);
    cout << "你收到" << message.getUsername() << "的文件: " << fileName << endl;
    cout << "是否要接收[1]YES, [0]NO" << endl;

    int choice;
    while (!(cin >> choice) || (choice != 0 && choice != 1)) {
        cout << "输入格式错误" << endl;
        cin.clear();
        cin.ignore(INT32_MAX, '\n');
    }
    cin.ignore(INT32_MAX, '\n');

    string reply = (choice == 1) ? "YES" : "NO";
    sendMsg(fd, reply);

    if (choice == 0) {
        cout << "你拒绝接收了该文件" << endl;
        close(fd);
        return;
    }

    // 接收文件 - 使用之前提取的fileName
    string filePath = "./fileBuffer_recv/" + fileName;

    if (!filesystem::exists("./fileBuffer_recv")) {
        filesystem::create_directories("./fileBuffer_recv");
    }

    string ssize;
    recvMsg(fd, ssize);

    off_t size = stoll(ssize);
    
    thread recvfileThread([=]() {
    this->recvFileThread(fd, fileName, size, filePath);
});
    // 分离线程，避免阻塞
    recvfileThread.detach(); 
    
}

 void  FileTransfer::G_recvFileThread(int fd, string fileName,  off_t size, string filePath)const{
    int n;
    char buf[BUFSIZ];
    ofstream ofs(filePath, ios::binary);

    while (size > 0) {
        if (size > sizeof(buf)) {
            n = read_n(fd, buf, sizeof(buf));
        } else {
            n = read_n(fd, buf, size);
        }
        size -= n;
        ofs.write(buf, n);
    }
    cout << "文件 " << fileName << "接收完成" ;

    ofs.close();
    close(fd);
 }

