#include "Notifications.h"
#include "../utils/proto.h"
#include "../utils/TCP.h"
#include "../utils/IO.h"
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <unistd.h>
#include <chrono>

using namespace std;


std::atomic<bool> stopNotify{false};

// 全局状态管理


    void ClientState::enterChat(const string& targetUID) {
        lock_guard<mutex> lock(messageMutex);
        inChat = true;
        currentChatUID = targetUID;
    }
    void ClientState::exitChat() {
        lock_guard<mutex> lock(messageMutex);
        inChat = false;
        currentChatUID = "";
    }



// 静态成员定义
bool ClientState::inChat = false;
string ClientState::currentChatUID = "";
string ClientState::myUID = "";
mutex ClientState::messageMutex;



//统一消息接收线程（使用独立连接）
void unifiedMessageReceiver( string UID) {
    ClientState::myUID = UID;

    
    int receiveFd = Socket();
    Connect(receiveFd, IP, PORT);

    // 统一接收协议
    sendMsg(receiveFd, UNIFIED_RECEIVER);
    sendMsg(receiveFd, UID);

    string receivedMsg;
    
 
    while (true) {
        //一直处于接收状态，收到——>丢给处理函数去处理
        int ret = recvMsg(receiveFd, receivedMsg);
        if (ret <= 0) {
            cout << "统一接收连接断开，退出消息接收线程" << endl;
            break;
        }

       

        //处理不同类型的消息
        processUnifiedMessage(receivedMsg);
    }

    close(receiveFd);
}

void processUnifiedMessage(const string& msg) {


    if(msg=="nomsg"){
        cout<<"没有离线消息"<<endl;
        return;
    }
    // 处理特殊系统响应,删除和屏蔽不需要保存到离线！！！只是一个提示提示完就消失了
    if (msg == "FRIEND_VERIFICATION_NEEDED") {
        string notifyMsg = "系统提示：对方开启了好友验证，你还不是他（她）朋友，请先发送朋友验证请求，对方验证通过后，才能聊天。";
            cout << YELLOW << notifyMsg << RESET << endl;
        
        return;
    }

    if (msg == "BLOCKED_MESSAGE") {
        string notifyMsg = "系统提示：消息已发出，但被对方拒收了。";
       
            cout << YELLOW << notifyMsg << RESET << endl;
        
        return;
    }

    if (msg == "DEACTIVATED_MESSAGE") {
        string notifyMsg = "系统提示：对方已注销，无法接收消息。";
       
            cout << YELLOW << notifyMsg << RESET << endl;
        
        return;
    }
    if (msg == "NO_IN_GROUP") {
       string notifyMsg = "系统提示：你已被移出群聊，无法发送与接收消息。";
       cout << YELLOW << notifyMsg << RESET << endl;
        return;
    }
   

    // 处理通知消息
    if (msg == REQUEST_NOTIFICATION) {
        string notifyMsg = "你收到一条好友添加申请";
            cout << notifyMsg << endl;
            return;
    }
    else if ( msg.find("REMOVE:") == 0) {
        string groupName = msg.substr(7);
        string notifyMsg = "你被移除群聊[" + groupName + "]";
    
            cout << RED <<notifyMsg <<RESET << endl;
            return;
    }
     else if ( msg.find("DELETE:") == 0) {
        string groupName = msg.substr(7);
        string notifyMsg = "群聊[" + groupName + "]已被解散";
    
            cout << RED <<notifyMsg <<RESET << endl;
            return;
    }
    else if (msg == GROUP_REQUEST) {
        string notifyMsg = "你收到一条群聊添加申请";
        
            cout << notifyMsg << endl;
            return;
    }
    else if (msg.find("GROUP_REQUEST:") == 0) { // 第一个字符的索引位置
        string groupName = msg.substr(14); // 去掉"GROUP_REQUEST:"前缀
        string notifyMsg = "[" + groupName + "]你收到一条群聊添加申请";
        
            cout << notifyMsg << endl;
            return;
    }
    else if (msg.find("ADMIN_ADD:") == 0) {
        string notifyMsg = "你已经被设为" + msg.substr(10) + "的管理员";
            cout << notifyMsg << endl;
            return;
    }
    else if (msg.find("ADMIN_REMOVE:") == 0) {
        string notifyMsg = "你已被取消" + msg.substr(13) + "的管理权限";
     
            cout << notifyMsg << endl;
            return;
    }
    //不在聊天框的普通信息
    else if (msg.find("MESSAGE:") == 0) {
        string senderInfo = msg.substr(8);
        string notifyMsg;

        if (senderInfo.find("(") != string::npos) {
            // 格式：用户名(N条)
            notifyMsg = senderInfo + "消息";
        } else {
            notifyMsg = senderInfo + "给你发来一条消息";
        }

       
            cout << notifyMsg << endl;
            return;
    }
    //不在聊天框的文件通知
    else if (msg.find("FILE:") == 0) {
        //发的时候，搞这个格式，后面跟好友名/群名即可
        string sender = msg.substr(5);
        string notifyMsg = sender + "给你发了文件";

            cout << notifyMsg << endl;
            return;
    }
    else if (msg[0] == '{') {
        try {
            Message message;
            message.json_parse(msg);
            if (message.getGroupName() == "1") {
                // 私
                if (ClientState::inChat && message.getUidFrom() == ClientState::currentChatUID) {
                    // 私聊
                    cout << message.getUsername() << ": " << message.getContent() << endl;
                }
            } else {
                // 群
                if (ClientState::inChat && message.getUidTo() == ClientState::currentChatUID) {
                    // 群聊
                    cout << "[" << message.getGroupName() << "] "
                         << message.getUsername() << ": "
                         << message.getContent() << endl;
                }
                // 注意：不在当前群聊窗口的消息不在这里处理，由MESSAGE:通知处理
            }
        } catch (const exception& e) {
            
            cout << "消息解析失败，跳过" << endl;
        }
    }
}

// 心跳
void heartbeat(string UID) {
    int fd = Socket();
    Connect(fd, IP, PORT);

    sendMsg(fd, "HEARTBEAT");
   
    while (true) {
        this_thread::sleep_for(chrono::seconds(30));  // 秒检测一次

        // 发送心跳包
        if (sendMsg(fd, "HEARTBEAT") <= 0) {
            cout << "心跳检测发消息失败，连接可能断开" << endl;
            break;
        }
    }
}

