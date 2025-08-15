#include "Transaction.h"
#include "Redis.h"
#include "MySQL.h"
#include "../utils/IO.h"
#include "../utils/proto.h"
#include <iostream>
#include "group_chat.h"
#include "../utils/Group.h"
#include <functional>
#include <map>
#include <sys/stat.h>
#include <filesystem>
#include <vector>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <nlohmann/json.hpp>


using namespace std;
using json = nlohmann::json;




//同步好友 
void synchronize(int fd, User &user) {
    Redis redis;
    redis.connect();
    string friend_info;
    int num = redis.scard(user.getUID());

    redisReply **arr = redis.smembers(user.getUID());

    // 先过滤有效好友，收集有效的好友信息
    vector<string> validFriendInfos;
    if (arr != nullptr) {
        for (int i = 0; i < num; i++) {
            cout << "[DEBUG] 检查好友 " << (i+1) << "/" << num << ", UID: " << arr[i]->str << endl;
            friend_info = redis.hget("user_info", arr[i]->str);

            if (friend_info.empty() || friend_info.length() < 10) {
                cout << "[ERROR] 好友信息无效，跳过 UID: " << arr[i]->str << endl;
            } else {
                validFriendInfos.push_back(friend_info);
                cout << "[DEBUG] 有效好友信息: " << friend_info.substr(0, 50) << "..." << endl;
            }
            freeReplyObject(arr[i]);
        }
    }

    // 发送有效好友数量
    int validCount = validFriendInfos.size();
    cout << "[DEBUG] 发送有效好友数量: " << validCount << " (原数量: " << num << ")" << endl;
    sendMsg(fd, to_string(validCount));

    // 发送有效好友详细信息
    for (int i = 0; i < validCount; i++) {
        cout << "[DEBUG] 发送好友 " << (i+1) << "/" << validCount << endl;
        sendMsg(fd, validFriendInfos[i]);
    }
}



void list_friend(int fd, User &user) {
    Redis redis;
    redis.connect();
    string temp;
    cout << "[DEBUG] listfriend 开始"  << endl;
    //收
    recvMsg(fd, temp);
    int num = stoi(temp);
    string friend_uid;
    for (int i = 0; i < num; ++i) {
        //收
        recvMsg(fd, friend_uid);
        if (redis.hexists("is_online", friend_uid)) {
            //在线发送"1"
            sendMsg(fd, "1");
        } else
            //不在线发送"0"
            sendMsg(fd, "0");
    }
}

void add_friend(int fd, User &user) {
    Redis redis;
    redis.connect();
    string username;

    recvMsg(fd, username);

    if (username == "0") {
        return;
    }
    cout << "[DEBUG] 添加好友请求: " << user.getUsername() << " 想添加 " << username << endl;
    
    
    string UID = redis.hget("username_to_uid", username);
    if (redis.sismember("deactivated_users", UID)) {
        sendMsg(fd, "-6"); // 已注销，无法添加
        return;
    }
    if (!redis.hexists("user_info", UID)) {
        sendMsg(fd, "-1");
        return;
    } else if (redis.sismember(user.getUID(), UID) && redis.sismember(UID, user.getUID())) {
        sendMsg(fd, "-2"); // 双方都互为好友才算已经是好友,无法添加
        return;
    } else if (UID == user.getUID()) {
        sendMsg(fd, "-3");
        return;
    }
    if (redis.sismember(UID + "add_friend", user.getUID())) {
        sendMsg(fd, "-5"); // 已经发送过申请，等待对方处理
        return;
    }

    sendMsg(fd, "1");
    redis.sadd(UID + "add_friend", user.getUID());//对方的好友申请列表

    // 不管在不在，先设置好友申请通知标记，在的话直接推
    //好友申请的离线通知列表
    redis.sadd(UID +"add_f_notify", user.getUID());//对方的好友申请通知列表


    //UID是想要添加的好友的UID
    // 用户在线
    if (redis.hexists("unified_receiver", UID)) {
        string receiver_fd_str = redis.hget("unified_receiver", UID);
        int receiver_fd = stoi(receiver_fd_str);
        sendMsg(receiver_fd, REQUEST_NOTIFICATION);
     
        // 清除通知
        redis.srem(UID +"add_f_notify", user.getUID());
    }

    string user_info = redis.hget("user_info", UID);
    sendMsg(fd, user_info);
}

void findRequest(int fd, User &user) {
    Redis redis;
    redis.connect();
    //只是一个好友申请缓冲区
    int num = redis.scard(user.getUID() + "add_friend");
    //发送缓冲区申请数量
    sendMsg(fd, to_string(num));
    if (num == 0) {
        return;
    }
    redisReply **arr = redis.smembers(user.getUID() + "add_friend");
    if (arr != nullptr) {
        string user_info;
        User friendRequest;
        for (int i = 0; i < num; i++) {
            user_info = redis.hget("user_info", arr[i]->str);
            friendRequest.json_parse(user_info);

            sendMsg(fd, friendRequest.getUsername());
            string reply;

            recvMsg(fd, reply);
            if (reply == "REFUSE") {
                sendMsg(fd, user_info);
                redis.srem(user.getUID() + "add_friend", arr[i]->str);
                freeReplyObject(arr[i]);
                continue; 
            }else if (reply == "IGNORE") {
                freeReplyObject(arr[i]);
                continue;
            }else if (reply == "ACCEPT") {
                redis.sadd(user.getUID(), arr[i]->str);
                redis.sadd(arr[i]->str, user.getUID());
                redis.srem(user.getUID() + "add_friend", arr[i]->str);

                //加上好友之后，我俩的好友申请列表里，都不会再出现对方了！！！
                redis.srem(string(arr[i]->str) + "add_friend", user.getUID());

                // ========== 新增：设置好友加入时间戳 ==========
                // 添加好友时，设置当前时间为加入时间
                time_t now = time(nullptr);
                redis.hset("friend_join_time", user.getUID() + "_" + string(arr[i]->str), to_string(now));
                redis.hset("friend_join_time", string(arr[i]->str) + "_" + user.getUID(), to_string(now));

                cout << "[DEBUG] 添加好友，设置加入时间戳: " << user.getUID() << " <-> " << arr[i]->str << "，时间: " << now << endl;

                freeReplyObject(arr[i]);
                continue;
            }else if (reply == "0") {
                freeReplyObject(arr[i]);
                return; 
            }

           
        }
    }
}

void del_friend(int fd, User &user) {
    Redis redis;
    redis.connect();
    string UID;

    recvMsg(fd, UID);
    if (UID == "0") {
        return;
    }
    //从我的好友列表删除对方（单向删除）
    redis.srem(user.getUID(), UID);
    //删除我对他的历史记录
    redis.del(user.getUID() + UID);
    //删除我对他的屏蔽关系
    redis.srem("blocked" + user.getUID(), UID);

    // ========== 新增：删除好友的逻辑 ==========

    // 清除好友加入时间记录，等待重新添加时设置新的时间戳
    redis.hdel("friend_join_time", user.getUID() + "_" + UID);
    cout << "[DEBUG] 用户 " << user.getUID() << " 删除了好友 " << UID << "，清除加入时间记录" << endl;

    // 移除通知机制 - 不再发送删除通知
    // redis.sadd(UID + "del", user.getUsername());
}

//屏蔽好友
void blockedLists(int fd, User &user) {
    Redis redis;
    redis.connect();
    User blocked;
    string blocked_uid;
    //接收客户端发送的要屏蔽用户的UID
    recvMsg(fd, blocked_uid);
    redis.sadd("blocked" + user.getUID(), blocked_uid);
}

//解除屏蔽
void unblocked(int fd, User &user) {
    Redis redis;
    redis.connect();
    int num = redis.scard("blocked" + user.getUID());
    //发送屏蔽名单数量
    sendMsg(fd, to_string(num));
    if (num == 0) {
        return;
    }
    redisReply **arr = redis.smembers("blocked" + user.getUID());
    if (arr != nullptr) {
        string blocked_info;
        for (int i = 0; i < num; ++i) {
            blocked_info = redis.hget("user_info", arr[i]->str);
            //循环发送屏蔽名单信息
            sendMsg(fd, blocked_info);
            freeReplyObject(arr[i]);
        }
    }
    //接收解除屏蔽的信息
    string UID;
    recvMsg(fd, UID);
    redis.srem("blocked" + user.getUID(), UID);
}


 void sendFile_Friend(int epfd, int fd) {//新fd
        Redis redis;
        redis.connect();

        string filePath, fileName;
        int ret = recvMsg(fd, filePath);
        if (ret <= 0) {
            cout << "[ERROR] 接收文件路径失败" << endl;
            return;
        }

        cout << "[DEBUG] 接收到文件路径: " << filePath << endl;

        // 检查是否取消发送
        if (filePath == "0") {
            cout << "[DEBUG] 用户取消发送文件" << endl;
            return;
        }

        recvMsg(fd, fileName);
    
    ///////////////////////////////////////////线程///////////////////
        //接收好友对象
        string friend_info;
        User _friend;
        ret = recvMsg(fd, friend_info);
        if (ret <= 0) {
            cout << "[ERROR] 接收目标用户信息失败" << endl;
            return;
        }
        _friend.json_parse(friend_info);

        //接收自己
        string my_info;
        User user;
        ret = recvMsg(fd, my_info);
        if (ret <= 0) {
            cout << "[ERROR] 接收发送方信息失败" << endl;
            return;
        }
        user.json_parse(my_info);

        //文件名加时间戳防重复
         size_t dotPos = fileName.rfind('.');
         string name = fileName.substr(0, dotPos);
        string ext = fileName.substr(dotPos);
        fileName = name + "_" + user.get_time() + ext;


        cout << "传输文件名: " << fileName << endl;
        filePath = "./fileBuffer_send/" + fileName;

       
        //创建文件传输消息，groupName设为"1"表示私聊
        Message message(user.getUsername(), user.getUID(), _friend.getUID(), "1");
        message.setContent(filePath);
        message.setGroupName(fileName);  // 文件名存储在groupName中
        if (!filesystem::exists("./fileBuffer_send")) {
            filesystem::create_directories("./fileBuffer_send");
        }
        ofstream ofs(filePath, ios::binary);
        if (!ofs.is_open()) {
            cerr << "Can't open file" << endl;
            return;
        }
        string ssize;

        ret = recvMsg(fd, ssize);
         if (ret <= 0) {
            cout << "[ERROR] 接收发送方信息失败" << endl;
            return;
        }

        off_t size = stoll(ssize);
        off_t originalSize = size;  
        off_t sum = 0;
        int n;
        char buf[BUFSIZ];
        int progressCounter = 0;  // 进度计数器

      
       

        while (size > 0) {
            if (size > sizeof(buf)) {
                 n = read_n(fd, buf, sizeof(buf));
            } else {
                n = read_n(fd, buf, size);
            }
            if (n <= 0) {
                sendMsg(fd, "no");
                cout << "读取文件失败" << endl;
                return;
            }
             //cout << "剩余文件大小: " << size << endl; 
            size -= n;
            sum += n;
            ofs.write(buf, n);


        }

        sendMsg(fd, "ok");
        
        

        // 创建文件消息并保存到聊天历史记录
        Message fileMessage;
        fileMessage.setUidFrom(user.getUID());
        fileMessage.setUidTo(_friend.getUID());
        fileMessage.setUsername(user.getUsername());
        fileMessage.setContent("[文件]" + fileName);
        fileMessage.setGroupName("1");  // 私聊标识

        
        // 保存到双方（Redis + MySQL）
        string senderHistory = user.getUID() + _friend.getUID();
        string receiverHistory = _friend.getUID() + user.getUID();
        string fileMessageJson = fileMessage.to_json();
        redis.lpush(senderHistory, fileMessageJson);
        redis.lpush(receiverHistory, fileMessageJson);

        // 同时保存到MySQL
        MySQL mysql;
        if (mysql.connect()) {
            mysql.insertPrivateMessage(user.getUID(), _friend.getUID(), "[文件]" + fileName);
            cout << "[DEBUG] 文件传输记录已保存到MySQL" << endl;
        }

        // 保存到接收方的文件接收队列
        redis.sadd("recv" + _friend.getUID(), message.to_json());

        // 离线
        if (!redis.hexists("is_online", _friend.getUID())) {
            // 用户离线，保存文件通知到离线消息
            redis.sadd( _friend.getUID() + "file_notify" , _friend.getUsername());
            cout << "[DEBUG] 用户离线，保存文件通知到离线消息" << endl;
            ofs.close();
            return;
        }
        // 在线
        if (redis.hexists("unified_receiver", _friend.getUID())) {
            string receiver_fd_str = redis.hget("unified_receiver", _friend.getUID());
            int receiver_fd = stoi(receiver_fd_str);
            cout << "[DEBUG] 推送文件通知到统一接收连接 fd: " << receiver_fd << endl;

            // 检查对方是否在聊天中，并且是否在与发送者的聊天中
            bool inChat = redis.sismember("is_chat", _friend.getUID());
            cout << "[DEBUG] 接收方是否在聊天中: " << inChat << endl;

            if (inChat) {
                // 对方在聊天中，发送聊天格式的文件消息
                Message fileMsg;
                fileMsg.setUidFrom(user.getUID());
                fileMsg.setUidTo(_friend.getUID());
                fileMsg.setUsername(user.getUsername());
                fileMsg.setContent("[文件]" + fileName);
                fileMsg.setGroupName("1");  // 私聊标识
                sendMsg(receiver_fd, fileMsg.to_json());
            } else {
                // 如果不在聊天中，发送普通文件通知
                sendMsg(receiver_fd, "FILE:" + user.getUsername());
            }
        } else {
            cout << "[DEBUG] 未找到用户 " << _friend.getUID() << " 的统一接收连接" << endl;
        }

        ofs.close();

        cout << "[DEBUG] 文件发送完成" << endl;
 }



void recvFile_Friend(int epfd, int fd) {
    User user;
    Redis redis;
    redis.connect();
    char buf[BUFSIZ];

    //收user
    string user_info;
    recvMsg(fd, user_info);
    user.json_parse(user_info);

    int num = redis.scard("recv" + user.getUID());

    cout << "[DEBUG] 用户 " << user.getUsername() << " 有 " << num << " 个文件待接收" << endl;
    sendMsg(fd, to_string(num));
    cout << "[DEBUG] 已发送文件数量: " << num << endl;
    if (num == 0) {
        return;
    }
    Message message;
    string path;
    
    redisReply **arr = redis.smembers("recv" + user.getUID());

    cout << "[DEBUG] 接收队列中的原始数据: " << arr[0]->str << endl;

    // 检查数据格式
    try {
        json test_json = json::parse(arr[0]->str);
        cout << "[DEBUG] JSON格式验证通过" << endl;
    } catch (const exception& e) {
        cout << "[ERROR] 接收队列中的数据不是有效JSON: " << e.what() << endl;
        cout << "[ERROR] 原始数据: " << arr[0]->str << endl;
        sendMsg(fd, "INVALID_FILE_DATA");
        freeReplyObject(arr[0]);
        return;
    }

    sendMsg(fd, arr[0]->str);
    message.json_parse(arr[0]->str);
    path = message.getContent();
    cout << "[DEBUG] 尝试访问文件路径: " << path << endl;

    struct stat info;
    if (stat(path.c_str(), &info) == -1) {
        cout << "[ERROR] 文件不存在或无法访问: " << path << endl;
        cout << "[ERROR] 错误原因: " << strerror(errno) << endl;

        // 尝试检查文件是否存在
        if (filesystem::exists(path)) {
            cout << "[DEBUG] 文件存在但无法stat，可能是权限问题" << endl;
        } else {
            cout << "[DEBUG] 文件不存在" << endl;
        }

        // 跳过这个文件
        sendMsg(fd, "FILE_NOT_FOUND");
        redis.srem("recv" + user.getUID(), arr[0]->str);
        freeReplyObject(arr[0]);
        return;
    }
    string reply;

    sendMsg(fd, "yes");

    cout << "[DEBUG] 等待客户端回复是否接收文件..." << endl;
    int _ret = recvMsg(fd, reply);
    cout << "[DEBUG] 收到客户端回复: '" << reply << "', 长度: " << reply.length() << endl;

    if (_ret <= 0) {
        cout << "[ERROR] 接收客户端回复失败" << endl;
        redis.hdel("is_online", user.getUID());
        // redis.srem("recv" + user.getUID(), arr[0]->str);
        freeReplyObject(arr[0]);
        return;
    }

    if (reply == "NO") {
        cout << "[DEBUG] 客户端拒绝接收文件" << endl;
        redis.srem("recv" + user.getUID(), arr[0]->str);
        freeReplyObject(arr[0]);
        return;
    }

    cout << "[DEBUG] 客户端同意接收文件，开始传输" << endl;

    int fp = open(path.c_str(), O_RDONLY);

    sendMsg(fd, to_string(info.st_size));
    off_t ret;
    off_t sum = info.st_size;
    off_t size = 0;
    while (true) {
        ret = sendfile(fd, fp, nullptr, info.st_size);
        if (ret == 0) {
            cout << "文件传输成功" << buf << endl;
            break;
        } else if (ret > 0) {
            //   cout << ret << endl;
            sum -= ret;
            size += ret;
        }
    }
    redis.srem("recv" + user.getUID(), arr[0]->str);
    close(fp);
    freeReplyObject(arr[0]);
    
}



// 群聊发送文件
void sendFile_Group(int epfd, int fd) {
    Redis redis;
    redis.connect();
    //user
    string user_info;
    recvMsg(fd, user_info);
    User user;
    user.json_parse(user_info);

    //group
    string group_info;
    Group group;
    int ret = recvMsg(fd, group_info);
   

    // cout << "[DEBUG] 接收到群聊信息: " << group_info << endl;
    group.json_parse(group_info);
    string filePath, fileName;

    ret = recvMsg(fd, filePath);
   

    cout << "[DEBUG] 接收到文件路径: " << filePath << endl;

    // 检查是否取消发送
    if (filePath == "0") {
        cout << "[DEBUG] 用户取消发送群聊文件" << endl;
        return;
    }

    recvMsg(fd, fileName);
  
    //文件名加时间戳防重复
         size_t dotPos = fileName.rfind('.');
         string name = fileName.substr(0, dotPos);
        string ext = fileName.substr(dotPos);
        fileName = name + "_" + user.get_time() + ext;


    

    
    if (!filesystem::exists("./fileBuffer_send")) {
        filesystem::create_directories("./fileBuffer_send");
    }


    // 创建接收的文件消息
    filePath = "./fileBuffer_send/" + fileName;
    Message filemessage(user.getUsername(), user.getUID(), group.getGroupUid(), group.getGroupName());
    filemessage.setContent(filePath); 

    ofstream ofs(filePath, ios::binary);
    if (!ofs.is_open()) {
        cerr << "Can't open file" << endl;
        return;
    }


//////////////////////////////线程/////////////////////////////


    string ssize;
    recvMsg(fd, ssize);
    off_t size = stoll(ssize);
    cout << "[DEBUG] 接收到文件大小: " << size << endl;
    off_t sum = 0;
    int n;
    char buf[BUFSIZ];

    while (size > 0) {
        if (size > sizeof(buf)) {
                n = read_n(fd, buf, sizeof(buf));
        } else {
            n = read_n(fd, buf, size);
        }
        if (n <= 0) {
            sendMsg(fd, "no");
            cout << "读取文件失败" << endl;
            return;
        }
           // cout << "剩余文件大小: " << size << endl; 
        size -= n;
        sum += n;
        ofs.write(buf, n);


    }

    sendMsg(fd, "ok");


    // 创建群聊文件消息
    Message message(user.getUsername(), user.getUID(), group.getGroupUid(), group.getGroupName());
    message.setContent("[文件]" + fileName);  // 文件路径存储在content中

    // 保存文件消息到群聊历史记录
    redis.lpush(group.getGroupUid() + "history", message.to_json());
    
    // 将文件添加到群聊成员的接收队列
    redisReply **members = redis.smembers(group.getMembers());
    int memberCount = redis.scard(group.getMembers());


    //注意，添加的是纯路径的消息
    for (int i = 0; i < memberCount; i++) {
        string memberUID = string(members[i]->str);
        if (memberUID != user.getUID()) {  // 不给发送者自己添加
            redis.sadd("recv" + group.getGroupUid()+memberUID, filemessage.to_json());
        }
        freeReplyObject(members[i]);
    }

    
        int len = redis.scard(group.getMembers());
        

        message.setUidTo(group.getGroupUid());
        redisReply **arr = redis.smembers(group.getMembers());
        string UIDto;
        for (int i = 0; i < len; i++) {
            UIDto = string(arr[i]->str);
            
            if (UIDto == user.getUID()) {
                freeReplyObject(arr[i]);
                continue;
            }
            //不在线
            if (!redis.hexists("is_online", UIDto)) {
                
                redis.sadd(UIDto + "file_notify", group.getGroupName());
                freeReplyObject(arr[i]);
                continue;
            }
            //在线，不在群聊中，发送通知
            if (!redis.sismember("group_chat", UIDto)) {
                // 使用统一接收连接发送通知
                if (redis.hexists("unified_receiver", UIDto)) {
                    string receiver_fd_str = redis.hget("unified_receiver", UIDto);
                    int receiver_fd = stoi(receiver_fd_str);
                    sendMsg(receiver_fd, "FILE:" + group.getGroupName());
                }
                freeReplyObject(arr[i]);
                continue;
            }
            // 在群聊中，使用统一接收连接发送实时消息
            if (redis.hexists("unified_receiver", UIDto)) {
                string receiver_fd_str = redis.hget("unified_receiver", UIDto);
                int receiver_fd = stoi(receiver_fd_str);
                sendMsg(receiver_fd, message.to_json());
            }
            freeReplyObject(arr[i]);
        }

    cout << "[DEBUG] 群聊文件发送完成: " << fileName << endl;
    ofs.close();
}



// 群聊接收文件
void recvFile_Group(int epfd, int fd) {

    
    cout << "recvFile_Group开始" << endl;
     string user_info;
    recvMsg(fd, user_info);
    User user;
    user.json_parse(user_info);
    
    
    Redis redis;
    redis.connect();

    string G_uid;
    recvMsg(fd,G_uid);
cout << "G_uid" << G_uid << endl;
    // 获取用户的群聊文件数量
    int num = redis.scard("recv" + G_uid + user.getUID());
    cout << "[DEBUG] 用户" << user.getUsername() << " 有 " << num << " 个群聊文件待接收" << endl;

    // 发送文件数量
    sendMsg(fd, to_string(num));
    cout << "[DEBUG] 已发送群聊文件数量: " << num << endl;

    if (num == 0) {
        return;
    }

    redisReply **arr = redis.smembers("recv" + G_uid + user.getUID());
    
    sendMsg(fd, arr[0]->str);

    Message message;
    message.json_parse(arr[0]->str);
    string path = message.getContent();

    cout << "[DEBUG] 尝试访问群聊文件路径: " << path << endl;

    struct stat info;
    if (stat(path.c_str(), &info) == -1) {
        cout << "[ERROR] 群聊文件不存在或无法访问: " << path << endl;
        cout << "[ERROR] 错误原因: " << strerror(errno) << endl;

        // 跳过这个文件
        redis.srem("recv" + G_uid + user.getUID(), arr[0]->str);
        freeReplyObject(arr[0]);
        return;
    }

    string reply;
    cout << "[DEBUG] 等待客户端回复是否接收群聊文件..." << endl;
    int _ret = recvMsg(fd, reply);
    cout << "[DEBUG] 收到客户端回复: '" << reply << "'" << endl;

    if (_ret <= 0) {
        cout << "[ERROR] 接收客户端回复失败" << endl;
        redis.hdel("is_online", user.getUID());
        redis.srem("recv" + G_uid + user.getUID(), arr[0]->str);
        freeReplyObject(arr[0]);
        return;
    }

    if (reply == "NO") {
        cout << "[DEBUG] 客户端拒绝接收群聊文件" << endl;
        redis.srem("recv" + G_uid + user.getUID(), arr[0]->str);
        freeReplyObject(arr[0]);
        return;
    }

    cout << "[DEBUG] 客户端同意接收群聊文件，开始传输" << endl;

    int fp = open(path.c_str(), O_RDONLY);
    if (fp == -1) {
        cout << "[ERROR] 无法打开群聊文件: " << path << endl;
        redis.srem("recv" + G_uid + user.getUID(), arr[0]->str);
        freeReplyObject(arr[0]);
        return;
    }

    sendMsg(fd, to_string(info.st_size));
    off_t ret;
    off_t sum = info.st_size;

    while (true) {
        ret = sendfile(fd, fp, nullptr, info.st_size);
        if (ret == 0) {
            cout << "[DEBUG] 群聊文件传输成功" << endl;
            break;
        } else if (ret > 0) {
            sum -= ret;
        }
    }

    redis.srem("recv" + G_uid + user.getUID(), arr[0]->str);
    close(fp);
    freeReplyObject(arr[0]);
    
}




// 注销账户
void deactivateAccount(int fd, User &user) {
    Redis redis;
    redis.connect();
    string UID = user.getUID();
    string created = "created" + user.getUID();
    Group group;
    redisReply **brr;
    int num = redis.scard(created);
    string UIDto;
    // 将用户添加到注销集合
    redis.sadd("deactivated_users", user.getUID());

    //发送解散群聊通知，解散群
    if(num != 0 ) {
        redisReply **arr = redis.smembers(created);
      
        for (int i = 0; i < num; i++) {
            string json = redis.hget("group_info", arr[i]->str);

            //具体创建的群聊，得到了群的具体信息消息实时通知
            group.json_parse(json);
            string groupName = group.getGroupName();
            //具体群的群成员
            brr = redis.smembers(group.getMembers());
            int len = redis.scard(group.getMembers());
            for (int j = 0; j < len; j++) {
                //集合里存的是群聊UID,是UID的主键世界
                UIDto = brr[j]->str;
                if (UIDto == user.getUID()) {
                    continue;
                }
                //不在线
                if (!redis.hexists("is_online", UIDto)) {
                    redis.sadd(UIDto +"deleteAC_notify", group.getGroupName());
                    continue;
                }
                //发送通知
                string receiver_fd_str = redis.hget("unified_receiver", UIDto);
                int receiver_fd = stoi(receiver_fd_str);
                sendMsg(receiver_fd, "deleteAC_notify:" + group.getGroupName());
                
            }

        //关闭客户端线程
        string receiver_fd_str = redis.hget("unified_receiver", UIDto);
        int receiver_fd = stoi(receiver_fd_str);
        sendMsg(receiver_fd, "deAC");
        close(receiver_fd);
            
            for (int k = 0; k < len; k++) {
                UIDto = brr[k]->str;//每个群成员的uid
                redis.srem("joined" + UIDto, group.getGroupUid());
                redis.srem("created" + UIDto, group.getGroupUid());
                redis.srem("managed" + UIDto, group.getGroupUid());
                redis.hdel("user_join_time", group.getGroupUid() + UIDto);
                freeReplyObject(brr[k]);
            }
            
       
            //删群的相关业务
            redis.del(group.getMembers());
            redis.del(group.getAdmins());
            redis.del(group.getGroupUid() + "history");
            redis.srem("group_Name", group.getGroupName());
            redis.hdel("group_info", group.getGroupUid());

            // ========== 新增：删除MySQL中的群聊消息 ==========
            MySQL mysql;
            if (mysql.connect()) {
                mysql.deleteGroupMessages(group.getGroupUid());
                cout << "[DEBUG] 解散群聊，已删除MySQL中的群聊消息: " << group.getGroupUid() << endl;
            }
            freeReplyObject(arr[i]);
        }
    }


     string user_info = redis.hget("user_info", user.getUID());

     json root = json::parse(user_info);
            
                root["email"] = "";  
                redis.hset("user_info", user.getUID(), root.dump());
                redis.hdel("email_to_uid", user.getEmail());


    
    // ========== 新增：删除用户的所有私聊历史记录 ==========
    MySQL mysql;
    if (mysql.connect()) {
        // 获取用户的所有好友
        redisReply **friends_arr = redis.smembers(user.getUID());
        int friends_count = redis.scard(user.getUID());

        for (int i = 0; i < friends_count; i++) {
            string friend_uid = friends_arr[i]->str;
            // 删除与每个好友的私聊记录
            mysql.deleteMessagesCompletely(user.getUID(), friend_uid);
            cout << "[DEBUG] 已删除用户 " << user.getUID() << " 与 " << friend_uid << " 的私聊记录" << endl;
            freeReplyObject(friends_arr[i]);
        }
        free(friends_arr);
    }

    // 从在线状态中移除
    redis.hdel("is_online", user.getUID());
    redis.hdel("unified_receiver", user.getUID());
    redis.srem("is_chat", user.getUID());

    cout << "[DEBUG] 用户 " << user.getUsername() << " 已注销，所有相关数据已清理" << endl;

}

// ========== MySQL版本的函数实现 ==========

// MySQL版本的聊天功能
void start_chat_mysql(int fd, User &user) {
    Redis redis;
    redis.connect();
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "[ERROR] start_chat_mysql: MySQL连接失败" << endl;
        return;
    }

    redis.sadd("is_chat", user.getUID());
    string records_index;
    recvMsg(fd, records_index);

    cout << "[DEBUG] start_chat_mysql: 用户 " << user.getUID() << " 请求与 " << records_index << " 的聊天" << endl;
    cout << "[DEBUG] records_index 长度: " << records_index.length() << endl;
    cout << "[DEBUG] records_index 原始内容: '" << records_index << "'" << endl;

    // 修正用户ID格式：如果records_index是两个用户ID的拼接，需要提取对方的ID
    string target_user_id = records_index;
    string my_uid = user.getUID();

    // 如果records_index以我的UID开头，那么对方的ID是后面的部分
    if (records_index.length() > my_uid.length() && records_index.substr(0, my_uid.length()) == my_uid) {
        target_user_id = records_index.substr(my_uid.length());
        cout << "[DEBUG] 检测到拼接格式，提取对方ID: " << target_user_id << endl;
    }
    // 如果records_index以我的UID结尾，那么对方的ID是前面的部分
    else if (records_index.length() > my_uid.length() && records_index.substr(records_index.length() - my_uid.length()) == my_uid) {
        target_user_id = records_index.substr(0, records_index.length() - my_uid.length());
        cout << "[DEBUG] 检测到拼接格式，提取对方ID: " << target_user_id << endl;
    }

    cout << "[DEBUG] 最终目标用户ID: " << target_user_id << endl;

    // 从MySQL获取历史消息（使用时间戳0显示所有消息）
    vector<string> history = mysql.getPrivateHistoryAfterTime(user.getUID(), target_user_id, 0, 50);
    int num = history.size();
    cout << "[DEBUG] start_chat_mysql: 获取到 " << num << " 条历史消息" << endl;

    if (num > 50) {
        num = 50;
    }

    cout << "[DEBUG] start_chat_mysql: 发送消息数量: " << num << endl;
    sendMsg(fd, to_string(num));

    // 发送历史消息（倒序，最新的在前）
    for (int i = num - 1; i >= 0; i--) {
        cout << "[DEBUG] start_chat_mysql: 发送历史消息 " << (num-i) << "/" << num << endl;
        sendMsg(fd, history[i]);  // 直接发送JSON字符串，无需解析验证
    }

    // 开始接收新消息
    string msg;
    while (true) {
        int ret = recvMsg(fd, msg);
        if (ret <= 0) {
            redis.srem("is_chat", user.getUID());
            return;
        }

        if (msg == EXIT) {
            redis.srem("is_chat", user.getUID());
            return;
        }

        cout << "[DEBUG] 收到消息: " << msg << endl;

        // 文件传输的特殊检查
        if (msg == "send" || msg == "recv") {
            cout << "[DEBUG] 检测到文件传输命令: " << msg << endl;

            // 检查是否被对方删除
            if (!redis.sismember(target_user_id, user.getUID())) {
                string receiver_fd_str = redis.hget("unified_receiver", user.getUID());
                int receiver_fd = stoi(receiver_fd_str);
                sendMsg(receiver_fd, "FRIEND_VERIFICATION_NEEDED");
                sendMsg(fd, "fail");
                continue;
            }

            // 检查是否被屏蔽
            if (redis.sismember("blocked" + target_user_id, user.getUID())) {
                string receiver_fd_str = redis.hget("unified_receiver", user.getUID());
                int receiver_fd = stoi(receiver_fd_str);
                sendMsg(receiver_fd, "BLOCKED_MESSAGE");
                sendMsg(fd, "fail");
                continue;
            }

            // 检查是否注销
            if (redis.sismember("deactivated_users", target_user_id)) {
                string receiver_fd_str = redis.hget("unified_receiver", user.getUID());
                int receiver_fd = stoi(receiver_fd_str);
                sendMsg(receiver_fd, "DEACTIVATED_MESSAGE");
                sendMsg(fd, "fail");
                continue;
            }

            // 如果都没问题，发送成功信号
            cout << "[DEBUG] 文件传输权限检查通过" << endl;
            sendMsg(fd, "success");
            continue;
        }

        // 解析消息
        Message message;
        string UID;

        try {
            message.json_parse(msg);
            UID = message.getUidTo();
        } catch (const exception& e) {
            cout << "[DEBUG] JSON解析失败，可能是纯文本消息: " << msg << endl;
            // 如果不是JSON，可能是纯文本消息，使用target_user_id作为目标用户
            UID = target_user_id;
            message.setContent(msg);
            message.setUidFrom(user.getUID());
            message.setUidTo(UID);
            message.setUsername(user.getUsername());
            cout << "[DEBUG] 处理为纯文本消息，目标用户: " << UID << endl;
        }
        //保存消息
        mysql.insertPrivateMessage(user.getUID(), UID, message.getContent());

        // 检查是否被对方删除
        if (!redis.sismember(UID, user.getUID())) {
            string receiver_fd_str = redis.hget("unified_receiver", user.getUID());
            int receiver_fd = stoi(receiver_fd_str);
            sendMsg(receiver_fd, "FRIEND_VERIFICATION_NEEDED");
            continue;
        }

        // 屏蔽检查（双向）
        bool i_blocked_him = redis.sismember("blocked" + user.getUID(), UID);
        bool he_blocked_me = redis.sismember("blocked" + UID, user.getUID());

        if (i_blocked_him) {
            // 我屏蔽了对方，不能发送消息
            string receiver_fd_str = redis.hget("unified_receiver", user.getUID());
            int receiver_fd = stoi(receiver_fd_str);
            sendMsg(receiver_fd, "YOU_BLOCKED_USER");  // 提示：你已屏蔽该用户
            cout << "[DEBUG] 用户 " << user.getUID() << " 屏蔽了 " << UID << "，消息被拒绝" << endl;
            continue;
        }

        if (he_blocked_me) {
            // 对方屏蔽了我，消息不会发送给对方
            string receiver_fd_str = redis.hget("unified_receiver", user.getUID());
            int receiver_fd = stoi(receiver_fd_str);
            sendMsg(receiver_fd, "BLOCKED_MESSAGE");  // 提示：消息被屏蔽
            cout << "[DEBUG] 用户 " << UID << " 屏蔽了 " << user.getUID() << "，消息被拒绝" << endl;
            continue;
        }

        // 注销
        if (redis.sismember("deactivated_users", UID)) {
            string receiver_fd_str = redis.hget("unified_receiver", user.getUID());
            int receiver_fd = stoi(receiver_fd_str);
            sendMsg(receiver_fd, "DEACTIVATED_MESSAGE");
            continue;
        }

        // 对方不在线
        if (!redis.hexists("is_online", UID)) {
            // 离线通知还是用Redis
            redis.lpush("off_msg" + UID, message.getUsername());
            cout << "[DEBUG] 用户 " << UID << " 离线，保存消息通知: " << message.getUsername() << endl;
            continue;
        }

        // 在线处理
        bool is_chat = redis.sismember("is_chat", UID);
        if (redis.hexists("unified_receiver", UID)) {
            string receiver_fd_str = redis.hget("unified_receiver", UID);
            int receiver_fd = stoi(receiver_fd_str);

            if (is_chat) {
                sendMsg(receiver_fd, msg);
            } else {
                sendMsg(receiver_fd, "MESSAGE:" + message.getUsername());
            }
        }
    }
}

// MySQL版本的历史消息获取（智能过滤屏蔽消息）
void F_history(int fd, User &user) {
    Redis redis;
    redis.connect();
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "[ERROR] F_history_mysql: MySQL连接失败" << endl;
        sendMsg(fd, "0");
        return;
    }

    string records_index;
    recvMsg(fd, records_index);

    cout << "[DEBUG] F_history_mysql: 用户 " << user.getUID() << " 请求与 " << records_index << " 的历史消息" << endl;

    // 修正用户ID格式：如果records_index是两个用户ID的拼接，需要提取对方的ID
    string target_user_id = records_index;
    string my_uid = user.getUID();

    // 如果records_index以我的UID开头，那么对方的ID是后面的部分
    if (records_index.length() > my_uid.length() && records_index.substr(0, my_uid.length()) == my_uid) {
        target_user_id = records_index.substr(my_uid.length());
        cout << "[DEBUG] F_history_mysql: 检测到拼接格式，提取对方ID: " << target_user_id << endl;
    }
    // 如果records_index以我的UID结尾，那么对方的ID是前面的部分
    else if (records_index.length() > my_uid.length() && records_index.substr(records_index.length() - my_uid.length()) == my_uid) {
        target_user_id = records_index.substr(0, records_index.length() - my_uid.length());
        cout << "[DEBUG] F_history_mysql: 检测到拼接格式，提取对方ID: " << target_user_id << endl;
    }

    cout << "[DEBUG] F_history_mysql: 最终目标用户ID: " << target_user_id << endl;

    // 使用Redis检查好友关系和屏蔽关系
    bool is_friend = redis.sismember(user.getUID(), target_user_id);
    bool i_blocked_him = redis.sismember("blocked" + user.getUID(), target_user_id);
    bool he_blocked_me = redis.sismember("blocked" + target_user_id, user.getUID());

    cout << "[DEBUG] 关系检查 - 是好友: " << (is_friend ? "是" : "否")
         << ", 我屏蔽对方: " << (i_blocked_him ? "是" : "否")
         << ", 对方屏蔽我: " << (he_blocked_me ? "是" : "否") << endl;

    // 修改逻辑：如果不是好友且没有屏蔽关系，才拒绝查询
    if (!is_friend && !i_blocked_him && !he_blocked_me) {
        cout << "[DEBUG] 既不是好友也没有屏蔽关系，返回0条消息" << endl;
        sendMsg(fd, "0");
        return;
    }

    // 检查好友加入时间，统一使用afterTime函数
    string friend_join_time_str = redis.hget("friend_join_time", user.getUID() + "_" + target_user_id);
    time_t join_time = 0;  // 默认为0，显示所有历史消息

    if (!friend_join_time_str.empty() && friend_join_time_str != "(nil)") {
        //stolonglong
        join_time = stoll(friend_join_time_str);
        cout << "[DEBUG] 检测到好友加入时间: " << join_time << "，只显示加入后的消息" << endl;
    } else {
        cout << "[DEBUG] 没有好友加入时间记录，显示所有历史消息（老朋友或第一次添加）" << endl;
    }

    // 统一使用afterTime函数，时间戳为0时相当于显示所有消息
    vector<string> all_messages = mysql.getPrivateHistoryAfterTime(user.getUID(), target_user_id, join_time, 100);
    cout << "[DEBUG] 从MySQL获取到 " << all_messages.size() << " 条原始消息" << endl;

    // 智能过滤消息：根据当前屏蔽状态过滤（使用已经获取的屏蔽状态）
    vector<string> filtered_messages;

    cout << "[DEBUG] 屏蔽状态 - 我屏蔽对方: " << (i_blocked_him ? "是" : "否")
         << ", 对方屏蔽我: " << (he_blocked_me ? "是" : "否") << endl;

    for (const string& msg : all_messages) {
        cout << "[DEBUG] 处理消息: " << msg << endl;

        try {
            // 解析JSON消息格式
            json message_json = json::parse(msg);
            string sender = message_json["UID_from"].get<string>();
            cout << "[DEBUG] 消息发送者: " << sender << endl;

            // 过滤逻辑：
            if (sender == user.getUID()) {
                // 我发送的消息：总是显示（让我看到我发过的消息）
                cout << "[DEBUG] 我的消息，添加到过滤列表" << endl;
                filtered_messages.push_back(msg);
            } else {
                // 对方发送的消息：只有在我没有屏蔽对方时才显示
                if (!i_blocked_him) {
                    cout << "[DEBUG] 对方的消息，我没屏蔽对方，添加到过滤列表" << endl;
                    filtered_messages.push_back(msg);
                } else {
                    cout << "[DEBUG] 对方的消息，我屏蔽了对方，跳过" << endl;
                }
            }
        } catch (const exception& e) {
            cout << "[DEBUG] JSON解析失败，跳过消息: " << msg << endl;
            continue;
        }
    }

    cout << "[DEBUG] 过滤后剩余 " << filtered_messages.size() << " 条消息" << endl;

    int num = filtered_messages.size();
    int up = 20;
    int down = 0;
    int first = num;
    bool signal = false;

    if (num > 20) {
        num = 20;
        signal = true;
    } else {
        signal = false;
    }

    sendMsg(fd, to_string(num));

    // 发送消息（倒序，最新的在前）
    for (int i = num - 1; i >= 0; i--) {
        sendMsg(fd, filtered_messages[i]);
    }

    // 发送总消息数给客户端，用于状态判断
    sendMsg(fd, to_string(first));

    // 处理分页请求（兼容原来的1/2/0输入方式）
    string order;
    while (true) {
        int ret = recvMsg(fd, order);
        if (ret <= 0) break;

        if (order == "0") {
            return;  // 退出历史记录
        }

        if (order == "1") { // 查看前20条（更早的消息）
            // 如果当前没有更多消息，直接返回
            if (!signal || down >= first) {
                sendMsg(fd, "less");
                continue;
            }

            // 前20
            up += 20;
            down += 20;

            // 剩余<20，false
            if (up >= first) {
                signal = false;
                sendMsg(fd, "more");
                sendMsg(fd, to_string(first - 1));
                sendMsg(fd, to_string(down));

                // 重新获取消息范围
                int actualCount = first - down;
                if (actualCount <= 0) {
                    sendMsg(fd, "less");
                    continue;
                }

                // 发送消息
                for (int i = first - 1; i >= down; i--) {
                    if (i < filtered_messages.size()) {
                        sendMsg(fd, filtered_messages[i]);
                    }
                }
                continue;
            }

            // 前20，剩余>20，true
            signal = true;
            sendMsg(fd, "more");
            sendMsg(fd, to_string(up - 1));
            sendMsg(fd, to_string(down));

            // 重新获取消息范围
            int actualCount = up - down;
            if (actualCount <= 0) {
                sendMsg(fd, "less");
                continue;
            }

            // 发送消息
            for (int i = up - 1; i >= down; i--) {
                if (i < filtered_messages.size()) {
                    sendMsg(fd, filtered_messages[i]);
                }
            }
            continue;

        } else if (order == "2") { // 查看后20条（更新的消息）
            if (down <= 0) {
                sendMsg(fd, "less");
                continue;
            }

            // 调整分页范围
            up -= 20;
            down -= 20;
            if (down < 0) {
                sendMsg(fd, "less");
                continue;
            }

            // 如果返回到最新页面，重新设置signal为true
            if (down == 0) {
                signal = true;
            }

            sendMsg(fd, "more");
            sendMsg(fd, to_string(up - 1));
            sendMsg(fd, to_string(down));

            // 重新获取消息范围
            int actualCount = up - down;
            if (actualCount <= 0) {
                sendMsg(fd, "less");
                continue;
            }

            // 发送消息
            for (int i = up - 1; i >= down; i--) {
                if (i < filtered_messages.size()) {
                    sendMsg(fd, filtered_messages[i]);
                }
            }
            continue;
        }

        // 兼容旧的"more"请求
        if (order == "more") {
            if (signal) {
                int start_index = up;
                int new_count = filtered_messages.size() - start_index;
                if (new_count > 0 && start_index < filtered_messages.size()) {
                    int actual_count = min(20, new_count);
                    sendMsg(fd, to_string(actual_count));
                    for (int i = start_index + actual_count - 1; i >= start_index; i--) {
                        sendMsg(fd, filtered_messages[i]);
                    }
                    up += 20;
                } else {
                    sendMsg(fd, "less");
                }
            } else {
                sendMsg(fd, "less");
            }
        } else if (order == "exit") {
            break;
        }
    }
}



void G_history(int fd, User &user) {

    Redis redis;
    redis.connect();

    MySQL mysql;
    mysql.connect();

    string group_id;
    recvMsg(fd, group_id);

    string records_index = group_id + "history";  // 保持原有变量名，用于兼容

    // 检查用户加入群聊的时间，统一使用afterTime函数
    string user_join_time_str = redis.hget("user_join_time", group_id + user.getUID());
    time_t join_time = 0;  // 默认为0，显示所有历史消息

    if (!user_join_time_str.empty() && user_join_time_str != "(nil)") {
        join_time = stoll(user_join_time_str);
        cout << "[DEBUG] 用户加入群聊时间: " << join_time << "，只显示加入后的消息" << endl;
    } else {
        cout << "[DEBUG] 没有加入时间记录，显示所有群聊历史消息（老成员或创建者）" << endl;
    }

    // 统一使用afterTime函数，时间戳为0时相当于显示所有消息
    vector<string> all_messages = mysql.getGroupHistoryAfterTime(group_id, join_time, 1000);

    int num = all_messages.size();

    int up = 20;
    int down = 0;
    int first = num;
    bool signal = false;

    sendMsg(fd,redis.hget("user_join_time",group_id+user.getUID()));

    if (num > 20) {
        num = 20;
        signal = true;
    } else {
        // 如果总数小于等于20，说明没有更多消息了
        signal = false;
    }
    sendMsg(fd, to_string(num));

    // 发送MySQL中的群聊历史消息（倒序，最新的在前）
    for (int i = num - 1; i >= 0; i--) {
        string msg_content = all_messages[i];
        try {
            json test_json = json::parse(msg_content);
            sendMsg(fd, msg_content);
        } catch (const exception& e) {
            continue;
        }
    }
    
    // 发送总消息数给客户端，用于状态判断
    sendMsg(fd, to_string(first));

    string order;
    while (true) {
        recvMsg(fd, order);
        if (order == "0") {
            return;
        }

        if (order == "1") { // 查看前20条（更早的消息）

            // 如果当前没有更多消息，直接返回
            if (!signal || down >= first) {

                sendMsg(fd, "less");
                continue;
            }
            
            //前20
            up += 20;
            down += 20;

            
            //剩余<20。false
            if (up >= first) {

                signal = false;
                sendMsg(fd, "more");
                sendMsg(fd, to_string(first - 1));
                sendMsg(fd, to_string(down));
                
                // 从MySQL数据中获取消息范围
                int actualCount = first - down;
                if (actualCount <= 0 || down >= (int)all_messages.size()) {
                    sendMsg(fd, "less");
                    continue;
                }

                // 从all_messages数组中获取指定范围的消息
                int end_idx = min(first - 1, (int)all_messages.size() - 1);
                for (int i = end_idx; i >= down && i >= 0; i--) {
                    string msg_content = all_messages[i];
                    try {
                        json test_json = json::parse(msg_content);
                        sendMsg(fd, msg_content);
                    } catch (const exception& e) {
                        continue;
                    }
                }
                continue;
            }
            
            //前20,剩余>20，true

            signal = true;
            sendMsg(fd, "more");
            sendMsg(fd, to_string(up - 1));
            sendMsg(fd, to_string(down));
            
            // 重新获取消息范围
            int actualCount = up - down;
            if (actualCount <= 0) {
                sendMsg(fd, "less");
                continue;
            }
            // 从MySQL数据中获取消息范围
            int end_idx = min(up - 1, (int)all_messages.size() - 1);
            for (int i = end_idx; i >= down && i >= 0; i--) {
                string msg_content = all_messages[i];
                try {
                    json test_json = json::parse(msg_content);
                    sendMsg(fd, msg_content);
                } catch (const exception& e) {
                    continue;
                }
            }
            continue;
                
        } else if (order == "2") { // 查看后20条（更新的消息）

            if (down <= 0) {

                sendMsg(fd, "less");
                continue;
            }
            
            // 调整分页范围
            up -= 20;
            down -= 20;
            if (down < 0) down = 0;
            // 如果返回到最新页面，重新设置signal为true
            if (down == 0) {
                signal = true;

            }

            

            sendMsg(fd, "more");
            sendMsg(fd, to_string(up - 1));
            sendMsg(fd, to_string(down));
            
            // 重新获取消息范围
            int actualCount = up - down;
            if (actualCount <= 0) {
                sendMsg(fd, "less");
                continue;
            }
            // 从MySQL数据中获取消息范围
            int end_idx = min(up - 1, (int)all_messages.size() - 1);
            for (int i = end_idx; i >= down && i >= 0; i--) {
                string msg_content = all_messages[i];
                try {
                    json test_json = json::parse(msg_content);
                    sendMsg(fd, msg_content);
                } catch (const exception& e) {
                    continue;
                }
            }
            continue;
        }
    }
}
