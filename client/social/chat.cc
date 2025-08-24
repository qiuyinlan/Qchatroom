#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <exception>
#include "chat.h"
#include <unistd.h>
#include "../utils/proto.h"
#include "../utils/IO.h"
#include "Notifications.h"
#include "../utils/User.h"
#include "../client/service/FileTransfer.h"

using namespace std;

// 颜色定义
#define GREEN "\033[32m"
#define RESET "\033[0m"
#define YELLOW "\033[33m"
#define EXCLAMATION "\033[31m"



// 群聊聊天
void ChatSession::startGroupChat(int groupIndex, const vector<Group>& joinedGroups) {
    if (groupIndex < 0 || groupIndex >= joinedGroups.size()) {
        cout << "输入错误，群聊索引无效" << endl;
        return;
    }
    // 从 joinedGroups 中取出一个群，绑定为只读引用，命名为 selectedGroup，从群列表中读取群（引用）
    const Group& selectedGroup = joinedGroups[groupIndex];

   
    // 构建并发送新的JSON请求
    nlohmann::json req;
    req["flag"] = C2S_START_GROUP_CHAT_REQUEST;
    req["data"]["group_uid"] = selectedGroup.getGroupUid();
    sendMsg(fd, req.dump());

    // 接收服务器响应
    string response_str;
    int recv_ret = recvMsg(fd, response_str);
    if (recv_ret <= 0) {
        cout << "接收群聊响应失败，连接可能已断开" << endl;
        return;
    }

    // 解析JSON响应
    try {
        nlohmann::json res = nlohmann::json::parse(response_str);
        if (res["data"]["success"].get<bool>()) {
            Message history_msg;
            for (const auto& msg_json : res["data"]["history"]) {
                history_msg.json_parse(msg_json);
                if (history_msg.getUsername() == user.getUsername()) {
                    cout << "你：" << history_msg.getContent() << endl;
                } else {
                    cout << history_msg.getUsername() << "  :  " << history_msg.getContent() << endl;
                }
                cout << "\t\t\t\t" << history_msg.getTime() << endl;
            }
        } else {
            cout << "开始群聊失败: " << res["data"]["reason"].get<string>() << endl;
            return;
        }
    } catch (const nlohmann::json::parse_error& e) {
        cout << "解析群聊响应失败: " << e.what() << endl;
        return;
    }
    cout << YELLOW << "-----------------------以上为历史消息-----------------------" << RESET << endl;
    
    // 创建消息对象
    Message message(user.getUsername(), user.getUID(), selectedGroup.getGroupUid(), selectedGroup.getGroupName());
    
    // 进入群聊状态，通知统一接收线程
    ClientState::enterChat(selectedGroup.getGroupUid());

    
    string msg;
     std::cout << "\033[90m输入【\\send】发送文件，【\\recv】接收文件，【\\quit】退出聊天\033[0m" << std::endl;

    while (true) {
        getline(cin, msg);

        if (cin.eof()) {
            cout << "输入结束，退出群聊" << endl;
            sendMsg(fd, EXIT);
            return ;
        }

        if (msg == "\\quit") {
            // 退出群聊状态
            ClientState::exitChat();
            nlohmann::json req;
            req["flag"] = C2S_EXIT_CHAT_REQUEST;
            sendMsg(fd, req.dump());
            return;
        }

        if (msg == "\\send") {
            FileTransfer fileTransfer;
            fileTransfer.sendFile_Group(fd, user, selectedGroup);
            cout << "文件发送请求已提交，将在后台发送。您可以继续聊天。" << endl;
            continue;
        }
        if (msg == "\\recv") {
            FileTransfer fileTransfer;
            fileTransfer.recvFile_Group(fd, user, selectedGroup.getGroupUid());
            continue;
        }

        if (msg.empty()) {
            cout << "不能发送空白消息" << endl;
            continue;
        }
        
        cout << "你：" << msg << endl;
        nlohmann::json req;
        req["flag"] = C2S_GROUP_MESSAGE;
        req["data"]["group_uid"] = selectedGroup.getGroupUid();
        req["data"]["content"] = msg;
        sendMsg(fd, req.dump());

    }
}
ChatSession::ChatSession(int fd, User user) : fd(fd), user(std::move(user)) {}

void ChatSession::startChat(vector<pair<string, User>> &my_friends,vector<Group> &joinedGroup) {
    string temp;

    cout << "==============开始聊天================" << endl;
    cout << "【好友聊天】" << endl;

    if (my_friends.empty()) {
        cout << "你当前没有好友，快去添加吧" << endl;
    }
    //调用函数
    else{
        for (int i = 0; i < my_friends.size(); i++) {
            if (my_friends[i].second.getIsOnline()) {
                cout << GREEN << i + 1 << ". " << my_friends[i].second.getUsername() <<  " (在线)" << RESET << endl;
            } else {
                cout << i + 1 << ". " << my_friends[i].second.getUsername()  << " (离线)" << endl;
            }
        }
    }
    cout << endl ;
    cout << "【群聊聊天】" << endl;
    if (joinedGroup.empty()) {
        cout << "当前没有加入的群,快去添加吧" << endl;
    }
    else{
        //群聊打印
        for (int i = 0; i < joinedGroup.size(); i++) {
            cout << my_friends.size() + i + 1 << ". "  << joinedGroup[i].getGroupName()  << endl;
        }
    }

    cout << "--------------------------------------" << endl;
    int totalOptions = my_friends.size() + joinedGroup.size();
    
    if (totalOptions == 0) {
        cout << "没有可聊天的对象，按enter返回" << endl;
        string temp;
        getline(cin, temp);
        return;
    }
    
    cout << "请选择聊天对象" << endl;
    return_last();
    
    int who;
    while (true) {
        if (!(cin >> who)) {
            cout << "输入格式错误，请输入数字" << endl;
            cin.clear();  // 清错误状态
            cin.ignore(INT32_MAX, '\n');  
            continue;
        }

        if (who == 0) {
            cin.ignore(INT32_MAX, '\n');
            return;  
        }
        else if (who < 1 || who > totalOptions) {
            cout << "输入格式错误，请输入1-" << totalOptions << "之间的数字" << endl;
            continue;
        }
        
        break;
    }

    // 清输入缓冲区
    cin.ignore(INT32_MAX, '\n');

    // 根据选择分发到不同的聊天函数
    if (who <= my_friends.size()) {
        // Private Chat Logic Restored
        string friend_uid = my_friends[who - 1].second.getUID();
        ClientState::enterChat(friend_uid); // ENTER CHAT STATE

        cout << "--------------------------------------" << endl;
        cout << "【好友：" << my_friends[who-1].second.getUsername() << "】"<< endl;

        // Request chat history
        nlohmann::json history_req;
        history_req["flag"] = C2S_GET_HISTORY_REQUEST;
        history_req["data"]["target_uid"] = friend_uid;
        history_req["data"]["chat_type"] = "private";
        sendMsg(fd, history_req.dump());

        string history_res_str;
        if (recvMsg(fd, history_res_str) > 0) {
            try {
                auto res = nlohmann::json::parse(history_res_str);
                if (res["data"]["success"].get<bool>()) {
                    for (const auto& msg_str : res["data"]["history"]) {
                        Message history_msg;
                        history_msg.json_parse(msg_str.get<string>());
                        if (history_msg.getUsername() == user.getUsername()) {
                            cout << "你: " << history_msg.getContent() << " (" << history_msg.getTime() << ")" << endl;
                        } else {
                            cout << history_msg.getUsername() << ": " << history_msg.getContent() << " (" << history_msg.getTime() << ")" << endl;
                        }
                    }
                }
            } catch (const nlohmann::json::parse_error& e) {}
        }
        cout << YELLOW << "-------------------以上为历史消息-------------------" << RESET << endl;

        string msg;
        std::cout << "\033[90m输入【\\send】发送文件，【\\recv】接收文件，【\\quit】退出聊天\033[0m" << std::endl;

        while (true) {
            getline(cin, msg);
            if (cin.eof()) {
                cin.clear();
                break;
            }
            if (msg == "\\quit") {
                break;
            }
            if (msg == "\\send") {
                FileTransfer fileTransfer;
                fileTransfer.sendFile_Friend(fd, user, my_friends[who-1].second);
                cout << "文件发送请求已提交，将在后台发送。您可以继续聊天。" << endl;
                continue;
            }
            if (msg == "\\recv") {
                FileTransfer fileTransfer;
                fileTransfer.recvFile_Friend(fd, user);
                continue;
            }
            if (msg.empty()) {
                cout << "不能发送空白消息" << endl;
                continue;
            }

            nlohmann::json req;
            req["flag"] = C2S_PRIVATE_MESSAGE;
            req["data"]["receiver_uid"] = friend_uid;
            req["data"]["content"] = msg;
            sendMsg(fd, req.dump());
            cout << "你：" << msg << endl;
        }
        ClientState::exitChat(); // EXIT CHAT STATE

    } else {
        // Group Chat
        int groupIndex = who - my_friends.size() - 1;
        const Group& selectedGroup = joinedGroup[groupIndex];
        ClientState::enterChat(selectedGroup.getGroupUid()); // ENTER CHAT STATE
        startGroupChat(groupIndex, joinedGroup);
        ClientState::exitChat(); // EXIT CHAT STATE
    }
}

void ChatSession::history(vector<pair<string, User>> &my_friends,vector<Group> &joinedGroup){
    cout << "\n============== 查看历史记录 ================" << endl;
    cout << "【好友】" << endl;
    if (my_friends.empty()) {
        cout << "你当前没有好友。" << endl;
    } else {
        for (int i = 0; i < my_friends.size(); i++) {
            cout << i + 1 << ". " << my_friends[i].second.getUsername() << endl;
        }
    }

    cout << "\n【群聊】" << endl;
    if (joinedGroup.empty()) {
        cout << "当前没有加入的群。" << endl;
    } else {
        for (int i = 0; i < joinedGroup.size(); i++) {
            cout << my_friends.size() + i + 1 << ". " << joinedGroup[i].getGroupName() << endl;
        }
    }

    cout << "--------------------------------------" << endl;
    int totalOptions = my_friends.size() + joinedGroup.size();
    if (totalOptions == 0) {
        cout << "没有可查看历史的对象。" << endl;
        return;
    }

    cout << "请选择要查看历史记录的对象 (输入0返回): ";
    int who;
    cin >> who;
    cin.ignore(INT32_MAX, '\n');

    if (who == 0) return;
    if (who < 1 || who > totalOptions) {
        cout << "[错误] 无效的序号。" << endl;
        return;
    }

    nlohmann::json req;
    req["flag"] = C2S_GET_HISTORY_REQUEST;
    auto& data = req["data"];
    string target_name;

    if (who <= my_friends.size()) {
        data["target_uid"] = my_friends[who - 1].second.getUID();
        data["chat_type"] = "private";
        target_name = my_friends[who - 1].second.getUsername();
    } else {
        int groupIndex = who - my_friends.size() - 1;
        data["target_uid"] = joinedGroup[groupIndex].getGroupUid();
        data["chat_type"] = "group";
        target_name = joinedGroup[groupIndex].getGroupName();
    }

    sendMsg(fd, req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器获取历史记录失败" << endl;
        return;
    }

    try {
        auto res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() != S2C_HISTORY_RESPONSE || !res["data"]["success"].get<bool>()) {
            cout << "[错误] " << res["data"].value("reason", "未知错误") << endl;
            return;
        }

        cout << "\n--------------------------------------" << endl;
        cout << "与 " << target_name << " 的最近50条历史记录:" << endl;
        auto history_json = res["data"]["history"];
        if (history_json.empty()) {
            cout << "暂无历史记录。" << endl;
        } else {
            for (const auto& msg_str : history_json) {
                Message history_msg;
                history_msg.json_parse(msg_str.get<string>());
                if (history_msg.getUsername() == user.getUsername()) {
                    cout << "你: " << history_msg.getContent() << " (" << history_msg.getTime() << ")" << endl;
                } else {
                    cout << history_msg.getUsername() << ": " << history_msg.getContent() << " (" << history_msg.getTime() << ")" << endl;
                }
            }
        }
        cout << "--------------------------------------\n" << endl;

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
}