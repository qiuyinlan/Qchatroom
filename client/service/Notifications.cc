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
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

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



//统一消息接收线程  fd接收和发送心跳两全
void unifiedMessageReceiver( string UID) {
    bool state = true; //线程开启
    ClientState::myUID = UID;


    int receiveFd = Socket();
    Connect(receiveFd, IP, PORT);

    // 新协议不再需要手动注册接收者，服务器通过心跳来识别

    string receivedMsg;

    // 启动心跳发送线程
    thread heartbeatThread([receiveFd, UID]() {
        while (true) {
            sleep(30);
            json heartbeat_req;
            heartbeat_req["flag"] = C2S_HEARTBEAT;
            heartbeat_req["data"]["uid"] = UID; // 在心跳中携带UID
            if (sendMsg(receiveFd, heartbeat_req.dump()) <= 0) {
                cout << "[心跳] 心跳发送失败，连接可能断开" << endl;
                break;
            }
        }
    });
    heartbeatThread.detach();  // 分离线程

    while (true) {
        //一直处于接收状态，收到——>丢给处理函数去处理
        int ret = recvMsg(receiveFd, receivedMsg);
        if (ret <= 0) {
            cout << "退出消息接收,已退出登陆" << endl;
            break;
        }

        // 处理心跳响应
        // if (receivedMsg == "HEARTBEAT_ACK") {
        //     cout << "[心跳] 收到心跳响应" << endl;
        //     continue;
        // }

        //处理不同类型的消息
        processUnifiedMessage(receivedMsg,state);
        if (state == false){
            return;
        }
    }

    close(receiveFd);
}

void processUnifiedMessage(const string& msg, bool& state) {


    if(msg=="nomsg"){
        cout<<"没有离线消息"<<endl;
        return;
    }

    //注销
    if (msg == "deAC") {
        state == false;
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

    
    else if ( msg.find("deleteAC_notify:") == 0) {
        string groupName = msg.substr(16);
        string notifyMsg = "群聊[" + groupName + "]的群主已注销，群聊已解散";
    
            cout << RED <<notifyMsg <<RESET << endl;
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
