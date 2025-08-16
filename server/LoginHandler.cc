#include "LoginHandler.h"
#include "Transaction.h"
#include <functional>
#include <sys/epoll.h>
#include "../utils/IO.h"
#include "../utils/proto.h"

// 显式声明utils中的阻塞IO函数，避免与server/IO.h冲突
extern int sendMsg(int fd, std::string msg);
extern int recvMsg(int fd, std::string &msg);
// 显式声明ET下的非阻塞接收函数，避免包含server/IO.h与utils/IO.h的宏冲突
extern int recvMsgET(int fd, std::string &msg);
#include "Redis.h"
#include <iostream>
#include <map>
#include <unistd.h>
#include <curl/curl.h>
#include <string>
#include <nlohmann/json.hpp>
#include <random>
#include <ctime>
#include <fcntl.h>
#include <arpa/inet.h>
#include <atomic>
#include <vector>
#include "./group_chat.h"
#include "../client/service/Notifications.h"

using namespace std;
using json = nlohmann::json;


//fd转换
void serverLoginWithData(int epfd, int fd, const std::string& loginData) {
    cout << "[DEBUG] 进入登录函数（带数据），fd=" << fd << ", 数据长度: " << loginData.size() << endl;

    // 将socket切换为阻塞模式
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("[ERROR] fcntl F_GETFL failed");
        close(fd);
        return;
    }
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        perror("[ERROR] fcntl F_SETFL failed");
        close(fd);
        return;
    }

    User user;
    Redis redis;

    if (!redis.connect()) {
        cout << "Redis connection failed during login(with data)" << endl;
        sendMsg(fd, "-4"); // 服务器内部错误
        close(fd);
        return;
    }

    LoginRequest loginRequest;
    try {
        loginRequest.json_parse(loginData);
    } catch (const exception& e) {
        cout << "[ERROR] 登录请求JSON解析失败: " << e.what() << endl;
        cout << "[ERROR] 请求内容: " << loginData << endl;
        sendMsg(fd, "-4"); // 服务器内部错误
        return;
    }

    string email = loginRequest.getEmail();
    string password = loginRequest.getPassword();
    cout << "[DEBUG] 登录请求(带数据): 邮箱=" << email << endl;

    if (!redis.hexists("email_to_uid", email)) {
        sendMsg(fd, "-1"); // 账号不存在
        return;
    }
    string UID = redis.hget("email_to_uid", email);
    string user_info = redis.hget("user_info", UID);

    if (user_info.empty()) {
        cout << "[ERROR] 用户信息为空，UID: " << UID << endl;
        sendMsg(fd, "-1");
        return;
    }

    try {
        user.json_parse(user_info);
    } catch (const exception& e) {
        cout << "[ERROR] 用户信息JSON解析失败: " << e.what() << endl;
        cout << "[ERROR] 用户信息: " << user_info << endl;
        sendMsg(fd, "-4");
        return;
    }

    if (password != user.getPassword()) {
        sendMsg(fd, "-2"); // 密码错误
        return;
    }

    if (redis.hexists("is_online", UID)) {
        cout << "[DEBUG] 用户 " << UID << " 已经在线，拒绝重复登录" << endl;
        sendMsg(fd, "-3");
        return;
    }

    sendMsg(fd, "1");
    redis.hset("is_online", UID, to_string(fd));
    sendMsg(fd, user_info);

    cout << "[DEBUG] 登录成功(带数据)，开始serverOperation" << endl;
    serverOperation(fd, user);
}


//登陆

void serverLogin(int epfd, int fd) {
    struct epoll_event temp;
    temp.data.fd = fd;
    temp.events = EPOLLIN;
    User user;
    Redis redis;
    // 确保 Redis 连接成功
    if (!redis.connect()) {
        cout << "Redis connection failed during login" << endl;
        sendMsg(fd, "-4"); // 服务器内部错误
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    string request;

    int recv_ret = recvMsg(fd, request);
    
    if (recv_ret < 0 || request.empty()) {
        cout << "[ERROR] 接收登录请求失败或为空" << endl;
        sendMsg(fd, "-4"); // 服务器内部错误
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    
    LoginRequest loginRequest;
    try {
        loginRequest.json_parse(request);
    } catch (const exception& e) {
        cout << "[ERROR] 登录请求JSON解析失败: " << e.what() << endl;
        cout << "[ERROR] 请求内容: " << request << endl;
        sendMsg(fd, "-4"); // 服务器内部错误
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    //得到用户发送的邮箱和密码
    string email = loginRequest.getEmail();
    string password = loginRequest.getPassword();
    cout << "[DEBUG] 登录请求: 邮箱=" << email << endl;

    if (!redis.hexists("email_to_uid", email)) {
        sendMsg(fd, "-1"); // 账号不存在
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    string UID = redis.hget("email_to_uid", email);
    string user_info = redis.hget("user_info", UID);

    if (user_info.empty()) {
        cout << "[ERROR] 用户信息为空，UID: " << UID << endl;
        sendMsg(fd, "-1"); // 账号不存在
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }

    try {
        user.json_parse(user_info);//向user存入所有相关信息
    } catch (const exception& e) {
        cout << "[ERROR] 用户信息JSON解析失败: " << e.what() << endl;
        cout << "[ERROR] 用户信息: " << user_info << endl;
        sendMsg(fd, "-4"); // 服务器内部错误
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    if (password != user.getPassword()) {
        sendMsg(fd, "-2"); // 密码错误
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    
  
    
    //用户已经登录检查
    if (redis.hexists("is_online", UID)) {
        cout << "[DEBUG] 用户 " << UID << " 已经在线，拒绝重复登录" << endl;
        sendMsg(fd, "-3");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    //登录成功
    sendMsg(fd, "1");
    redis.hset("is_online", UID, to_string(fd));
    //发送从数据库获取的用户信息
    sendMsg(fd, user_info);
    serverOperation(fd, user);
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
}




//登陆后业务分发

void serverOperation(int fd, User &user) {
 
    Redis redis;
    redis.connect();
    string temp;
    int ret;
    while (true) {
        //接收用户输入的操作
        ret = recvMsg(fd, temp);
       
        if (temp == BACK ) {
            cout << "收到客户端在主页退出，成功" << endl ;

            break;
        }

        
        if (temp == START_CHAT) {
            start_chat_mysql(fd, user);  // 只有聊天消息存储用MySQL
        } else if (temp == LIST_FRIENDS) {
            list_friend(fd, user);
        } else if (temp == ADD_FRIEND) {
            add_friend(fd, user);  // 恢复Redis版本
        } else if (temp == FIND_REQUEST) {
            findRequest(fd, user);
        } else if (temp == DEL_FRIEND) {
            del_friend(fd, user);  // 恢复Redis版本
        } else if (temp == BLOCKED_LISTS) {
            blockedLists(fd, user);  // 恢复Redis版本
        } else if (temp == UNBLOCKED) {
            unblocked(fd, user);  // 恢复Redis版本
        }  else if (temp == SYNC) {
            synchronize(fd, user);
        }  else if (temp == GROUP) {
            GroupChat groupChat(fd, user);
            groupChat.group(fd, user);
        } else if (temp == SYNCGL) {
            GroupChat groupChat(fd, user);
            groupChat.synchronizeGL(fd, user);
        }  else if ( temp == GROUPCHAT ) {
            GroupChat groupChat(fd, user);
            groupChat.startChat();
        }  else if (temp == "F_HISTORY") {
            F_history(fd, user);  // 只有历史消息获取用MySQL
        }  else if (temp == "G_HISTORY") {
            G_history(fd, user);  // 群聊历史保持Redis版本
        }  else if (temp == DEACTIVATE_ACCOUNT) {
            deactivateAccount(fd, user);
        } else {
            cout << "[DEBUG] 收到未知协议: '" << temp << "' (长度: " << temp.length() << ")" << endl;
            cout << "没有这个选项，请重新输入: " << temp << endl;
            continue;
        }
    }
    redis.hdel("is_online", user.getUID());
    redis.hdel("unified_receiver", user.getUID());  // 清理统一接收连接记录
  
}

void notify(int fd, const string &UID) {//离线通知
    
    
    bool msgnum=false;//是否有离线消息
    Redis redis;
    redis.connect();
   

    //疏漏:消息推送没设循环，只能发一条,已修改
    // 好友申请ok
    int num = redis.scard(UID +"add_f_notify");
     if (num != 0) {
        redisReply **arr = redis.smembers(UID +"add_f_notify");
        if (arr != nullptr) {
            for (int i = 0; i < num; i++) {
                sendMsg(fd, REQUEST_NOTIFICATION);
                msgnum=true;
                redis.srem(UID +"add_f_notify", arr[i]->str);
                freeReplyObject(arr[i]);
            }
        }
    }
    

    // 群聊申请ok
     num = redis.scard(UID+"add_group");
     if (num != 0) {
         string groupName = redis.hget("group_request_info", UID);
        redisReply **arr = redis.smembers(UID +"add_group");
        if (arr != nullptr) {
            for (int i = 0; i < num; i++) {
                sendMsg(fd, "GROUP_REQUEST:" + groupName);
                msgnum=true;
                redis.srem(UID +"add_group", arr[i]->str);
                freeReplyObject(arr[i]);
            }
        }
        redis.hdel("group_request_info", UID);  
    }
    

    //注销后群聊解散通知
     num = redis.scard(UID +"deleteAC_notify");
     if (num != 0) {
        redisReply **arr = redis.smembers(UID +"deleteAC_notify");
        if (arr != nullptr) {
            for (int i = 0; i < num; i++) {
                string groupName = arr[i]->str;
                sendMsg(fd, "deleteAC_notify:" + groupName);
                msgnum=true;
                redis.srem(UID +"deleteAC_notify", arr[i]->str);
                freeReplyObject(arr[i]);
            }
        }
        //redis.hdel("group_request_info", UID);  
    }

    // //入群通知，先不搞了，被踢告知一下就行
    // num = redis.scard("approve" + UID);
    //  if (num != 0) {
    //     redisReply **arr = redis.smembers("approve" + UID);
    //     if (arr != nullptr) {
    //         for (int i = 0; i < num; i++) {

    //             sendMsg(fd, "GROUP_REQUEST:" + groupName);
    //             msgnum=true;
    //             redis.srem(UID +"add_group", arr[i]->str);
    //             freeReplyObject(arr[i]);
    //         }
    //     }
    //     redis.hdel("group_request_info", UID);  
    // }





    //被设置为管理员
     num = redis.scard("appoint_admin" + UID);
    if (num != 0) {
        redisReply **arr = redis.smembers("appoint_admin" + UID);
        if (arr != nullptr) {
            for (int i = 0; i < num; i++) {
                sendMsg(fd, "ADMIN_ADD:" + string(arr[i]->str));
                msgnum=true;
                redis.srem("appoint_admin" + UID, arr[i]->str);
                freeReplyObject(arr[i]);
            }
        }
    }
    //被踢出群聊,都带引号吧
    num = redis.scard("REMOVE" + UID);
    if (num != 0) {
        redisReply **arr = redis.smembers("REMOVE" + UID);
        if (arr != nullptr) {
            for (int i = 0; i < num; i++) {
                sendMsg(fd, "REMOVE:" + string(arr[i]->str));
                msgnum=true;
                redis.srem("REMOVE" + UID , arr[i]->str);
                freeReplyObject(arr[i]);
            }
        }
    }

    //被移出群聊
    num = redis.scard("DELETE" + UID);
    if (num != 0) {
        redisReply **arr = redis.smembers("DELETE" + UID);
        if (arr != nullptr) {
            for (int i = 0; i < num; i++) {
                sendMsg(fd, "DELETE:" + string(arr[i]->str));
                msgnum=true;
                redis.srem("DELETE" + UID , arr[i]->str);
                freeReplyObject(arr[i]);
            }
        }
    }
 
    // 检查离线文件通知,arr[i]->str 是一个 char* 指针  ok
     num = redis.scard(UID+"file_notify");//里面是要存名字！！！
     if (num != 0) {
        redisReply **arr = redis.smembers(UID +"file_notify");
        if (arr != nullptr) {
            for (int i = 0; i < num; i++) {
                sendMsg(fd, "FILE:" + string(arr[i]->str) );
                msgnum=true;
                redis.srem(UID+"file_notify",arr[i]->str); 
                freeReplyObject(arr[i]);
            }
        } 
    }



    // 离线消息ok
    int offlineMsgNum = redis.llen("off_msg" + UID);
    if (offlineMsgNum != 0) {
        redisReply **offlineMsgArr = redis.lrange("off_msg" + UID, "0", to_string(offlineMsgNum - 1));

        // 统计每个用户的消息数量
        map<string, int> senderCount;
        for (int i = 0; i < offlineMsgNum; i++) {
            string sender = string(offlineMsgArr[i]->str);
            senderCount[sender]++;
            freeReplyObject(offlineMsgArr[i]);
        }

        // 发送合并后的通知
        for (const auto& pair : senderCount) {
            string sender = pair.first;
            int count = pair.second;
            if (count == 1) {
                sendMsg(fd, "MESSAGE:" + sender);
                msgnum=true;
            } else {
                sendMsg(fd, "MESSAGE:" + sender + "(" + to_string(count) + "条)");
                msgnum=true;
            }
        }

        // 清空整个离线消息列表
        redis.del("off_msg" + UID);
        cout << "[DEBUG] 已推送 " << senderCount.size() << " 个用户的 " << offlineMsgNum << " 条离线消息通知给用户 " << UID << endl;
    }

    //被取消管理员权限
    num = redis.scard("revoke_admin" + UID);
    if (num != 0) {
        redisReply **arr = redis.smembers("revoke_admin" + UID);
        if (arr != nullptr) {
            for (int i = 0; i < num; i++) {
                sendMsg(fd, "ADMIN_REMOVE:" + string(arr[i]->str));
                msgnum=true;
                redis.srem("revoke_admin" + UID, arr[i]->str);
                freeReplyObject(arr[i]);
            }
        }
    }

    //bool变量暂时没用了
    
    if(msgnum==false){
        sendMsg(fd,"nomsg");
    }
}

// 处理统一接收线程连接

extern void addFdToUid(int fd, const std::string& uid);

void handleUnifiedReceiver(int epfd, int fd) {
    cout << "[DEBUG] 处理统一接收线程连接，fd: " << fd << endl;

    Redis redis;
    redis.connect();

    string UID;
    int ret = recvMsg(fd, UID);
    if (ret <= 0) {
        cout << "[ERROR] 接收UID失败，关闭连接" << endl;
        close(fd);
        return;
    }

    // 将此连接标记为统一接收连接
    redis.hset("unified_receiver", UID, to_string(fd));


    // 添加fd到uid的映射，用于心跳管理。内存更快更高效，其实全局都可以用内存
    addFdToUid(fd, UID);

    // 重加epoll监听
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);

    // 离线通知检查
    notify(fd, UID);
}



std::string generateCode() {
    // 取当前时间戳的后两位
    time_t timer;
    time(&timer);
    std::string timeStamp = std::to_string(timer).substr(std::to_string(timer).size() - 2, 2);
    // 生成2位随机数
    std::random_device rd;
    std::mt19937 eng(rd());
    std::uniform_int_distribution<> dist(10, 99);
    int random_num = dist(eng);
    // 拼接成4位验证码
    return timeStamp + std::to_string(random_num);
}

#include <iostream>
#include <string>
#include <curl/curl.h>

// 定义上传状态结构体
struct upload_status {
    size_t bytes_read;
    const std::string* payload;
};

// 自定义 payload_source 回调函数
static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp) {
    struct upload_status *upload_ctx = (struct upload_status *)userp;
    const std::string& payload = *(upload_ctx->payload);
    size_t left = payload.size() - upload_ctx->bytes_read;
    size_t len = size * nmemb;
    if (len > left) {
        len = left;
    }
    if (len) {
        memcpy(ptr, payload.data() + upload_ctx->bytes_read, len);
        upload_ctx->bytes_read += len;
    }
    return len;
}

bool sendMail(const std::string& to_email, const std::string& code, bool is_find) {
    CURL *curl;
    CURLcode res = CURLE_OK;
    curl = curl_easy_init();
    
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.163.com:465");
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_USERNAME, "17750601137@163.com");
        curl_easy_setopt(curl, CURLOPT_PASSWORD, "GAWKe3AaL7bEV64e");
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, "17750601137@163.com");
        struct curl_slist *recipients = nullptr;
        recipients = curl_slist_append(recipients, to_email.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        // 构建更详细的邮件内容
        std::string from = "From: 17750601137@163.com\r\n";
        std::string to = "To: " + to_email + "\r\n";
        std::string subject = "Subject: 验证码\r\n";
        std::string mimeVersion = "MIME-Version: 1.0\r\n";
        std::string contentType = "Content-Type: text/plain; charset=utf-8\r\n";
        std::string body = "你好，\r\n\r\n你的验证码是：" + code + "\r\n请在 5 分钟内使用此验证码完成验证。\r\n\r\n感谢你的使用！";

        std::string full_mail_payload = from + to + subject + mimeVersion + contentType + "\r\n" + body;

        upload_status up_status = { 0, &full_mail_payload };
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &up_status);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE, (long)full_mail_payload.length());

        res = curl_easy_perform(curl);
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);

        return res == CURLE_OK;
    }

    return false;
}

void handleRequestCode(int epfd, int fd) {
    Redis redis;
    redis.connect();
    struct epoll_event temp;
    temp.data.fd = fd;
    temp.events = EPOLLIN;
    // 1. 接收邮箱,发验证码之前看看，邮箱和用户名是否存在，存在了就先不发
    int ret;
    string email;
    while (true) {
        ret = recvMsg(fd, email);
        std::cout << "[SERVER] 收到邮箱输入: " << email << std::endl;
        if (ret <= 0) { 
            std::cout << "[SERVER] 客户端断开" << std::endl;
            return;
        }
        if (email == "0") { 
            std::cout << "[SERVER] 用户选择返回注册菜单" << std::endl;
            epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
            return;   
        }
        if (!redis.hexists("email_to_uid", email)) {
            std::cout << "[SERVER] 邮箱未注册，进入后续流程" << std::endl;
            sendMsg(fd, "未注册");
            break;
        }
        std::cout << "[SERVER] 邮箱已注册，发送提示,让对方重新输入/返回" << std::endl;
        sendMsg(fd, "邮箱已存在");
    }

     // 检查用户名是否已注册，若已注册则循环让客户端重新输入
     string username;
     while (true) {

        ret = recvMsg(fd, username);
        if(ret<=0){
            std::cout << "[SERVER] 客户端断开" << std::endl;
            return;
        }
        std::cout << "[SERVER] 收到用户名输入: " << username << std::endl;
        
         if (username == "0") { // 用户选择返回
             epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
         return;
         }
         if (!redis.hexists("username_to_uid", username)) {
            sendMsg(fd, "未注册");
             break; // 未注册，可以继续后续流程
         }
         sendMsg(fd, "已存在");
         // 循环，等待客户端输入新邮箱
     }
   
   
    // 2. 生成验证码
    string code = generateCode();
    // 3. 存到Redis，5分钟有效

    redis.hset("verify_code", email, code);
    // 4. 发邮件
    bool ok = sendMail(email, code, false);
    // 5. 通知客户端
    if (ok) {
        sendMsg(fd, "验证码已发送，请查收邮箱");
    } else {
        sendMsg(fd, "验证码发送失败，请稍后重试");
    }
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
}

void handleResetCode(int epfd, int fd) {
    // 1. 接收邮箱
    string email;
    recvMsg(fd, email);
    // 2. 生成验证码
    string code = generateCode();
    // 3. 存到Redis，5分钟有效
    Redis redis;
    redis.connect();
    redis.hset("reset_code", email, code);
    // 4. 发邮件
    bool ok = sendMail(email, code, true);
    // 5. 通知客户端
    if (ok) {
        sendMsg(fd, "验证码已发送，请查收邮箱");
    } else {
        sendMsg(fd, "验证码发送失败，请稍后重试");
    }
    struct epoll_event temp;
    temp.data.fd = fd;
    temp.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
}

void serverRegisterWithCode(int epfd, int fd) {
    struct epoll_event temp;
    temp.data.fd = fd;
    temp.events = EPOLLIN;
    string json_str;

    Redis redis;

    string email;
    int ret;
   //收信息
    ret = recvMsg(fd, json_str);
    if (ret <= 0) {
        sendMsg(fd, "接收注册信息失败");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }

    json root;
    root = json::parse(json_str);
     email = root["email"].get<string>();
    string code = root["code"].get<string>();
    string username = root["username"].get<string>();
    string password = root["password"].get<string>();

    if (!redis.connect()) {
        sendMsg(fd, "服务器内部错误");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }

    // 检查邮箱是否已被注销用户使用
    if (redis.hexists("email_to_uid", email)) {
        string existingUID = redis.hget("email_to_uid", email);
        if (redis.sismember("deactivated_users", existingUID)) {
            sendMsg(fd, "该邮箱已被注销用户使用，无法注册");
            epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
            return;
        }
    }

    // 验证验证码
    string real_code = redis.hget("verify_code", email);
    if (real_code != code) {
        sendMsg(fd, "验证码错误");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
   

    // 生成UID并创建User对象
    User user;
    user.setEmail(email);
    user.setUsername(username);
    user.setPassword(password);
    // UID自动生成

    string user_info = user.to_json();
    if (!redis.hset("user_info", user.getUID(), user_info)) {
        sendMsg(fd, "服务器内部错误");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    if (!redis.hset("email_to_uid", email, user.getUID())) {
        sendMsg(fd, "服务器内部错误");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    if (!redis.hset("uid_to_username", user.getUID(), username)) {
        sendMsg(fd, "服务器内部错误");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    if (!redis.hset("username_to_uid", username, user.getUID())) {
        sendMsg(fd, "服务器内部错误");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    if (!redis.sadd("all_uid", user.getUID())) {
        sendMsg(fd, "服务器内部错误");
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
        return;
    }
    sendMsg(fd, "注册成功");
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
}

// void resetPasswordWithCode(int epfd, int fd) {
//     struct epoll_event temp;
//     temp.data.fd = fd;
//     temp.events = EPOLLIN;
//     string json_str;
//     recvMsg(fd, json_str);
//     json root = json::parse(json_str);
//     string email = root["email"].get<string>();
//     string code = root["code"].get<string>();
//     string password = root["password"].get<string>();
//     Redis redis;
//     redis.connect();
//     string real_code = redis.hget("reset_code", email);
//     if (real_code != code) {
//         sendMsg(fd, "验证码错误");
//         epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
//         return;
//     }
//     // 检查用户是否存在
//     if (!redis.hexists("email_to_uid", email)) {
//         sendMsg(fd, "该邮箱未注册");
//         epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
//         return;
//     }
//     string UID = redis.hget("email_to_uid", email);
//     string user_info = redis.hget("user_info", UID);
//     User user;
//     user.json_parse(user_info);
//     user.setPassword(password);
//     redis.hset("user_info", UID, user.to_json());
//     sendMsg(fd, "密码重置成功");
//     epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
// }

// void findPasswordWithCode(int epfd, int fd) {
//     struct epoll_event temp;
//     temp.data.fd = fd;
//     temp.events = EPOLLIN;
//     string json_str;
//     recvMsg(fd, json_str);
//     nlohmann::json root;
//     try {
//         root = nlohmann::json::parse(json_str);
//     } catch (const nlohmann::json::exception& e) {
//         sendMsg(fd, "JSON 格式错误");
//         epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
//         return;
//     }
//     string email = root["email"].get<string>();
//     string code = root["code"].get<string>();
//     Redis redis;
//     redis.connect();
//     string real_code = redis.hget("reset_code", email);
//     if (real_code != code) {
//         sendMsg(fd, "验证码错误");
//         epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
//         return;
//     }
//     // 检查用户是否存在
//     if (!redis.hexists("email_to_uid", email)) {
//         sendMsg(fd, "该邮箱未注册");
//         epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
//         return;
//     }
//     string UID = redis.hget("email_to_uid", email);
//     string user_info = redis.hget("user_info", UID);
//     nlohmann::json user_json;
//     try {
//         user_json = nlohmann::json::parse(user_info);
//     } catch (const nlohmann::json::exception& e) {
//         sendMsg(fd, "用户信息损坏");
//         epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
//         return;
//     }
//     if (!user_json.contains("password")) {
//         sendMsg(fd, "未找到密码信息");
//         epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
//         return;
//     }
//     string password = user_json["password"].get<string>();
//     sendMsg(fd, "你的密码是: " + password);
//     epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &temp);
// }

