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

   
    // 发送协议
    sendMsg(fd, GROUPCHAT);
    // 拿一个可以传输的副本（拷贝）
    Group groupCopy = selectedGroup; 

    //发送群相关信息
    sendMsg(fd, groupCopy.to_json());
    
    // 接收历史消息
    string historyNum;
    int recv_ret = recvMsg(fd, historyNum);

    string timestamp;
    recvMsg(fd,timestamp);


    if (recv_ret <= 0) {
        cout << "接收群聊历史消息数量失败，客户端连接断开" << endl;
        return;
    }

    int num;
    
        if (historyNum.empty()) {
            cout << "接收到空的群聊历史消息数量" << endl;
            return;
        }
        num = stoi(historyNum);
        if (num < 0 || num > 10000) {
            cout << "接收到异常的群聊历史消息数量: " << historyNum << endl;
            return;
        }
    
   //接收,打印群聊历史
    for (int i = 0; i < num; i++) {
        string historyMsg;
        Message message;
        recv_ret = recvMsg(fd, historyMsg);
        if (recv_ret <= 0) {
            cout << "接收历史消息失败，停止接收" << endl;
            break;
        }
        
       
        // if (historyMsg.empty()) {
        //     continue;
        // }
        // //消息不显示！！！
        message.json_parse(historyMsg);
        if (message.getTime() < timestamp) {
            // 如果消息时间戳小于指定时间戳，则跳过该消息
            continue;
        }
        if (message.getUsername() == user.getUsername()) {
            cout << "你：" << message.getContent() << endl;
            cout << "\t\t\t\t" << message.getTime() << endl;
            continue;
        }
        cout << message.getUsername() << "  :  " << message.getContent() << endl;
        cout << "\t\t\t\t" << message.getTime() << endl;
        
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
            sendMsg(fd, EXIT);
            return;
        }

        // 发文件，记得先屏蔽检查！！！
        string reply,json;
        if (msg == "\\send") {
            sendMsg(fd, "send");
            recvMsg(fd,reply);
            if (reply == "fail"){
                message.setContent(msg);
                json = message.to_json();
                sendMsg(fd, json);
                continue;
            }
            FileTransfer fileTransfer;
            //thread fileSender(&FileTransfer::sendFile_Group, &fileTransfer, fd, selectedGroup, user);
            
             fileTransfer.sendFile_Group( selectedGroup, user);
              cout << "服务器正在处理，可以继续聊天（输入消息后按enter发送）" << endl;
            continue;
        }
        if (msg == "\\recv") {
             sendMsg(fd, "recv");
            recvMsg(fd,reply);
            if (reply == "fail"){
                message.setContent(msg);
                json = message.to_json();
                sendMsg(fd, json);
                continue;
            }
            string G_uid = selectedGroup.getGroupUid() ;
            FileTransfer fileTransfer;
            fileTransfer.recvFile_Group( user,G_uid);
             cout << "服务器正在处理，可以继续聊天（输入消息后按enter发送）" << endl;
            continue;
        }

        if (msg.empty()) {
            cout << "不能发送空白消息" << endl;
            continue;
        }
        
        cout << "你：" << msg << endl;
        message.setContent(msg);
        string jsonMsg = message.to_json();
        
        sendMsg(fd, jsonMsg);

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
        sendMsg(fd, LIST_FRIENDS);
        //发好友个数
        sendMsg(fd, to_string(my_friends.size()));

        for (int i = 0; i < my_friends.size(); i++) {
            
            //发
            sendMsg(fd, my_friends[i].second.getUID());
            string is_online;

            //收
            int recv_ret = recvMsg(fd, is_online);
            if (is_online == "1") {
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
    //私聊
    if (who <= my_friends.size()) {
        cout << "--------------------------------------" << endl;
        cout << "【好友：" << my_friends[who-1].second.getUsername() << "】"<< endl;
        // 发送好友聊天协议
        sendMsg(fd, START_CHAT);
        string records_index = user.getUID() + my_friends[who-1].second.getUID();
        //发索引
        sendMsg(fd, records_index);

        Message history;
        string nums;
        //收数量
        int recv_ret = recvMsg(fd, nums);
        if (recv_ret <= 0) {
            cout << "接收历史消息数量失败，连接可能已断开" << endl;
            return;
        }

        int num;
        try {
            if (nums.empty()) {
                cout << "接收到空的消息数量字符串" << endl;
                return;
            }
            num = stoi(nums);
            if (num < 0 || num > 10000) {
                cout << "接收到异常的消息数量: " << nums << endl;
                return;
            }
        } catch (const exception& e) {
            cout << "解析消息数量失败: '" << nums << "', 错误: " << e.what() << endl;
            return;
        }


        string history_message;
        for (int j = 0; j < num; j++) {
            //循环收
            int msg_ret = recvMsg(fd, history_message);
            if (msg_ret <= 0) {
                cout << "接收历史消息失败，停止接收" << endl;
                break;
            }
                // //###待完善：空的消息，可以发和接收，但是不打印在历史记录里面
                // if (history_message.empty()) {
                //     continue;
                // }

                history.json_parse(history_message);
                if (history.getUsername() == user.getUsername()) {
                    cout << "你：" << history.getContent() << endl;
                    cout << "\t\t\t\t" << history.getTime() << endl;
                    continue;
                }
                cout << history.getUsername() << "  :  " << history.getContent() << endl;
                cout << "\t\t\t\t" << history.getTime() << endl;
                
        }
        
        cout << YELLOW << "-------------------以上为历史消息-------------------" << RESET << endl;

        
        Message message(user.getUsername(), user.getUID(), my_friends[who-1].second.getUID(),"1");
        string friend_UID = my_friends[who-1].second.getUID();

        
        sendMsg(fd, friend_UID);

        // 通知统一接收线程
        ClientState::enterChat(friend_UID);

       
        string msg, json, reply;

        //真正开始聊天
        std::cout << "\033[90m输入【\\send】发送文件，【\\recv】接收文件，【\\quit】退出聊天\033[0m" << std::endl;

        while (true) {
            getline(cin,msg);
            if (cin.eof()) {
                cout << "\n检测到输入结束 (Ctrl+D)，退出聊天" << endl;
                cin.clear();
                sendMsg(fd, EXIT);
                return;
            }
            if (msg == "\\quit") {
                // 退出聊天状态
                ClientState::exitChat();
                sendMsg(fd, EXIT);
                return;
            }
            // 文件,私聊的时候，就需要检查是否能发成功，再发文件！！！
            if (msg == "\\send") {
                sendMsg(fd, "send");
                recvMsg(fd,reply);
                if (reply == "fail"){
                     message.setContent(msg);
                    json = message.to_json();
                    sendMsg(fd, json);
                    continue;
                }
                FileTransfer fileTransfer;
                // thread fileSender(&FileTransfer::sendFile_Friend, &fileTransfer, fd, my_friends[who-1].second, user);
                // fileSender.detach();
                 fileTransfer.sendFile_Friend(my_friends[who-1].second, user);
                
                 cout << "服务器正在处理，可以继续聊天（输入消息后按enter发送）" << endl;
    
                continue;
            }
            if(msg == "\\recv"){
                sendMsg(fd, "recv");
                recvMsg(fd,reply);
                if (reply == "fail"){
                     message.setContent(msg);
                    json = message.to_json();
                    sendMsg(fd, json);
                    continue;
                }
                FileTransfer fileTransfer;
                fileTransfer.recvFile_Friend( user);
                cout << "服务器正在处理，可以继续聊天（输入消息后按enter发送）" << endl;
                continue;
            }
            else if(msg.empty()){
                cout << "不能发送空白消息" << endl;
                continue;
            }
            message.setContent(msg);
            json = message.to_json();

            sendMsg(fd, json);
            cout << "你：" << msg << endl;
        }
    } else {
        // 选择的是群聊聊天
        int groupIndex = who - my_friends.size() - 1;
        startGroupChat(groupIndex, joinedGroup);
    }
}

void ChatSession::history(vector<pair<string, User>> &my_friends,vector<Group> &joinedGroup){
    string temp;

    cout << "==============查看历史================" << endl;
    cout << "【好友】" << endl;

    if (my_friends.empty()) {
        cout << "你当前没有好友，快去添加吧" << endl;
    }
    //调用函数
    else{
        sendMsg(fd, LIST_FRIENDS);
        //发好友个数
        sendMsg(fd, to_string(my_friends.size()));

        for (int i = 0; i < my_friends.size(); i++) {
            
            //发
            sendMsg(fd, my_friends[i].second.getUID());
            string is_online;

            //收,改成不管怎么样，都没有颜色！！！
            int recv_ret = recvMsg(fd, is_online);
            if (is_online == "1") {
                cout <<  i + 1 << ". " << my_friends[i].second.getUsername() <<   endl;
            } else {
                cout << i + 1 << ". " << my_friends[i].second.getUsername()  <<  endl;
            }
        }
    }
    cout << endl ;
    cout << "【群聊】" << endl;
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
        cout << "没有可查看历史的对象，按enter返回" << endl;
        string temp;
        getline(cin, temp);
        return;
    }
    cout << "请选择查看历史的对象" << endl;
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

    if (who <= my_friends.size()) {
        //好友
        sendMsg(fd, "F_HISTORY");
        cout << "--------------------------------------" << endl;
        cout << "【好友：" << my_friends[who-1].second.getUsername() << "】"<< endl;
        // 发送好友聊天协议

        string records_index = user.getUID() + my_friends[who-1].second.getUID();

        //发索引
        sendMsg(fd, records_index);

        Message history;
        string nums;
        //收数量

        int recv_ret = recvMsg(fd, nums);

        if (recv_ret <= 0) {
            cout << "接收历史消息数量失败，连接可能已断开" << endl;
            return;
        }

        int num;
        try {
            if (nums.empty()) {
                cout << "接收到空的消息数量字符串" << endl;
                return;
            }
            num = stoi(nums);
            if (num < 0 || num > 10000) {
                cout << "接收到异常的消息数量: " << nums << endl;
                return;
            }
        } catch (const exception& e) {
            cout << "解析消息数量失败: '" << nums << "', 错误: " << e.what() << endl;
            return;
        }


        string history_message;
        for (int j = 0; j < num; j++) {
            //循环收
            int msg_ret = recvMsg(fd, history_message);
            if (msg_ret <= 0) {
                cout << "接收历史消息失败，停止接收" << endl;
                break;
            }
                // //###待完善：空的消息，可以发和接收，但是不打印在历史记录里面
                // if (history_message.empty()) {
                //     continue;
                // }

                history.json_parse(history_message);
                if (history.getUsername() == user.getUsername()) {
                    cout << "你：" << history.getContent() << endl;
                    cout << "\t\t\t\t" << history.getTime() << endl;
                    continue;
                }
                cout << history.getUsername() << "  :  " << history.getContent() << endl;
                cout << "\t\t\t\t" << history.getTime() << endl;
                
        }
        cout << YELLOW << "-------------------以上为最近20条或20条以内的历史消息-------------------" << RESET << endl;

        // 接收总消息数
        string totalMsgCount;
        recvMsg(fd, totalMsgCount);
        int totalCount;
        try {
            totalCount = stoi(totalMsgCount);
        } catch (const std::invalid_argument& e) {
            cout << "接收总消息数失败，返回主菜单" << endl;
            return;
        } catch (const std::out_of_range& e) {
            cout << "总消息数超出范围，返回主菜单" << endl;
            return;
        }
        


        // 判断是否有更多消息
        bool hasOlderMessages = (totalCount > 20); // 如果总消息数大于20，说明有更早的消息
        bool hasNewerMessages = false; // 初始显示最新的20条，所以没有更新的消息
        
        // 显示状态信息
        if (!hasOlderMessages) {
           // cout << "【这是最早的消息】" << endl;
        }
        if (!hasNewerMessages) {
          //  cout << "【这是最新的消息】" << endl;
        }

        std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;
        string order, reply;
        
        while (true) {
            getline(cin, order);
            if (order == "0") {
                sendMsg(fd, "0");
                return;
            } else if (order == "1") { // 查看前20条（更早的消息）

                sendMsg(fd, "1");
                recvMsg(fd, reply);

                if (reply == "less") {
                    cout << "已经是最早的消息，没有更多历史消息了哦" << endl;
                    hasOlderMessages = false; // 更新状态：没有更早的消息了
                    continue;
                }
                
                // 只有在收到"more"时才接收消息范围
                if (reply == "more") {
                    // 接收前20条消息
                    system("clear");
                    string up_str, down_str;
                    recvMsg(fd, up_str);
                    recvMsg(fd, down_str);
                
                // 添加错误处理，防止stoi崩溃
                int up_val, down_val;
                try {
                    up_val = stoi(up_str);
                    down_val = stoi(down_str);
                } catch (const std::invalid_argument& e) {
                    cout << "接收消息范围失败，返回主菜单" << endl;
                    cout << "[DEBUG] stoi失败，up_str: '" << up_str << "', down_str: '" << down_str << "'" << endl;
                    return;
                } catch (const std::out_of_range& e) {
                    cout << "消息范围超出范围，返回主菜单" << endl;
                    return;
                }
                
                for (int i = up_val; i >= down_val; i--) {
                    int msg_ret = recvMsg(fd, history_message);
                    if (msg_ret <= 0) {
                        cout << "接收历史消息失败，停止接收" << endl;
                        break;
                    }
                    if (history_message.empty()) continue;
                    
                    history.json_parse(history_message);
                    if (history.getUsername() == user.getUsername()) {
                        cout << "你：" << history.getContent() << endl;
                        cout << "\t\t\t\t" << history.getTime() << endl;
                    } else {
                        cout << history.getUsername() << "  :  " << history.getContent() << endl;
                        cout << "\t\t\t\t" << history.getTime() << endl;
                    }
                }
                cout << YELLOW << "-------------------以上为历史消息-------------------" << RESET << endl;
                
                // 更新状态信息
                hasNewerMessages = true; // 现在有更新的消息了
                if (down_val >= totalCount - 20) {
                    hasOlderMessages = false;
                 //   cout << "【这是最早的消息】" << endl;
                } else {
                    hasOlderMessages = true; // 如果down_val < totalCount - 20，说明还有更早的消息
                }
                if (up_val <= 19) {
                    hasNewerMessages = false;
                 //   cout << "【这是最新的消息】" << endl;
                }
                
                std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;

                }
                continue;
                
            } else if (order == "2") { // 查看后20条（更新的消息）
                sendMsg(fd, "2");
                recvMsg(fd, reply);
                if (reply == "less") {
                    cout << "已经是最新消息，没有更多内容了哦" << endl;
                    hasNewerMessages = false; // 更新状态：没有更新的消息了
                    std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;
                    continue;
                }
                
                // 只有在收到"more"时才接收消息范围
                if (reply == "more") {
                    // 接收后20条消息
                    system("clear");
                    string up_str, down_str;
                    recvMsg(fd, up_str);
                    recvMsg(fd, down_str);
                
                // 添加错误处理，防止stoi崩溃
                int up_val, down_val;
                try {
                    up_val = stoi(up_str);
                    down_val = stoi(down_str);
                } catch (const std::invalid_argument& e) {
                    cout << "接收消息范围失败，返回主菜单" << endl;
                    cout << "[DEBUG] stoi失败，up_str: '" << up_str << "', down_str: '" << down_str << "'" << endl;
                    return;
                } catch (const std::out_of_range& e) {
                    cout << "消息范围超出范围，返回主菜单" << endl;
                    return;
                }
                
                int messageCount = up_val - down_val + 1;
                for (int k = 0; k < messageCount; k++) {
                    int msg_ret = recvMsg(fd, history_message);
                    if (msg_ret <= 0) {
                        cout << "接收历史消息失败，停止接收" << endl;
                        break;
                    }
                    if (history_message.empty()) continue;
                    
                    try {
                        history.json_parse(history_message);
                        if (history.getUsername() == user.getUsername()) {
                            cout << "你：" << history.getContent() << endl;
                            cout << "\t\t\t\t" << history.getTime() << endl;
                        } else {
                            cout << history.getUsername() << "  :  " << history.getContent() << endl;
                            cout << "\t\t\t\t" << history.getTime() << endl;
                        }
                    } catch (const exception& e) {
                        cout << "解析消息失败，跳过此消息" << endl;
                        continue;
                    }
                }
                cout << YELLOW << "-------------------以上为历史消息-------------------" << RESET << endl;
                
                // 更新状态信息
                hasOlderMessages = true; // 现在有更早的消息了
                if (down_val >= totalCount - 20) {
                    hasOlderMessages = false;
                //    cout << "【这是最早的消息】" << endl;
                } else {
                    hasOlderMessages = true; // 如果down_val < totalCount - 20，说明还有更早的消息
                }
                if (up_val <= 19) {
                    hasNewerMessages = false;
                //    cout << "【这是最新的消息】" << endl;
                }
                
                std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;
                }
                continue;
            } else {
                cout << "输入无效，请输入【1】、【2】或【0】" << endl;
                continue;
            }
        }

    } else {
        // 群聊历史消息查看
        sendMsg(fd, "G_HISTORY");
        // 获取群ID
        string group_id = joinedGroup[who - my_friends.size() - 1].getGroupUid();
        sendMsg(fd, group_id);
        cout << "--------------------------------------" << endl;
        cout << "【群聊：" << joinedGroup[who - my_friends.size() - 1].getGroupName() << "】"<< endl;

        string join_time;
        recvMsg(fd,join_time);
        Message history;
        string nums;
        // 接收消息数量
        int recv_ret = recvMsg(fd, nums);
        if (recv_ret <= 0) {
            cout << "接收历史消息数量失败" << endl;
            return;
        }

        int num;
        try {
            if (nums.empty()) {
                cout << "接收到空的消息数量字符串" << endl;
                return;
            }
            num = stoi(nums);
            if (num < 0 || num > 10000) {
                cout << "接收到异常的消息数量: " << nums << endl;
                return;
            }
        } catch (const exception& e) {
            cout << "解析消息数量失败: '" << nums << "', 错误: " << e.what() << endl;
            return;
        }

        string history_message;


        int displayedCount = 0;
        // 接收并显示初始20条消息
        for (int j = 0; j < num; j++) {
            int msg_ret = recvMsg(fd, history_message);
            if (msg_ret <= 0) {
                cout << "接收历史消息失败，停止接收" << endl;
                break;
            }
            if (history_message.empty()) continue;
            
            history.json_parse(history_message);
            
            //时间,真的才计数
            if (history.getTime() < join_time) {
            // 如果消息时间戳小于指定时间戳，则跳过该消息
            continue;
             }

            displayedCount++;

            if (history.getUsername() == user.getUsername()) {
                cout << "你：" << history.getContent() << endl;
            } else {
                cout << history.getUsername() << "  :  " << history.getContent() << endl;
            }
            cout << "\t\t\t\t" << history.getTime() << endl;
        }
        
        // 接收总消息数
        string totalMsgCount;
        recvMsg(fd, totalMsgCount);
        int totalCount;
        try {
            totalCount = stoi(totalMsgCount);
        } catch (const std::invalid_argument& e) {
            cout << "接收总消息数失败，返回主菜单" << endl;
            return;
        } catch (const std::out_of_range& e) {
            cout << "总消息数超出范围，返回主菜单" << endl;
            return;
        }
        
        // 判断是否有更多消息
        bool hasOlderMessages = (displayedCount == 20); // 如果总消息数大于20，说明有更早的消息
        bool hasNewerMessages = false; // 初始显示最新的20条，所以没有更新的消息
        
        cout << YELLOW << "-------------------以上为最近20条或20条以内的群聊历史消息-------------------" << RESET << endl;
        
        // 显示状态信息
        if (!hasOlderMessages) {
          //  cout << "【这是最早的群聊消息】" << endl;
        }
        if (!hasNewerMessages) {
         //   cout << "【这是最新的群聊消息】" << endl;
        }

        // 群聊消息分页控制
        std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;
        string order, reply;
        
        // 分页状态变量
        int currentUp = 20;
        int currentDown = 0;
        
        while (true) {
            getline(cin, order);
            if (order == "0") {
                sendMsg(fd, "0");
                return;
            } else if (order == "1") { // 查看前20条（更早的消息）
                sendMsg(fd, "1");
                recvMsg(fd, reply);
                if (reply == "less") {
                    cout << "已经是最早的消息，没有更多历史消息了哦" << endl;
                    hasOlderMessages = false;
                    std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;
                    continue;
                }
                
                // 只有在收到"more"时才接收消息范围
                if (reply == "more") {
                    // 接收前20条消息
                    system("clear");
                    string up_str, down_str;
                    recvMsg(fd, up_str);
                    recvMsg(fd, down_str);
                    
                                    // 添加错误处理，防止stoi崩溃
                    try {
                        currentUp = stoi(up_str);
                        currentDown = stoi(down_str);
                    } catch (const std::invalid_argument& e) {
                        cout << "接收消息范围失败，返回主菜单" << endl;
                        cout << "[DEBUG] stoi失败，up_str: '" << up_str << "', down_str: '" << down_str << "'" << endl;
                        return;
                    } catch (const std::out_of_range& e) {
                        cout << "消息范围超出范围，返回主菜单" << endl;
                        return;
                    }
                    hasNewerMessages = true; // 现在有更新的消息了
                    
                    int messageCount = currentUp - currentDown + 1;

                    int displayedCountThisPage = 0;
                    for (int k = 0; k < messageCount; k++) {
                        int msg_ret = recvMsg(fd, history_message);
                        if (msg_ret <= 0) {
                            cout << "接收历史消息失败，停止接收" << endl;
                            break;
                        }
                        if (history_message.empty()) continue;
                        
                        try {
                            history.json_parse(history_message);
                            if (history.getUsername() == user.getUsername()) {
                                cout << "你：" << history.getContent() << endl;
                            } else {
                                cout << history.getUsername() << "  :  " << history.getContent() << endl;
                            }
                            cout << "\t\t\t\t" << history.getTime() << endl;
                        } catch (const exception& e) {
                            cout << "解析消息失败，跳过此消息" << endl;
                            continue;
                        }
                    }
                    
                                    cout << YELLOW << "-------------------以上为群聊历史消息-------------------" << RESET << endl;
                
                // 更新状态信息
                hasNewerMessages = true; // 现在有更新的消息了

                if (displayedCount < 20) {
                    hasOlderMessages = false; // 没满，说明到头
                } else {
                    hasOlderMessages = true; // 满了，可能还有
}

                // if (currentDown >= totalCount - 20) {
                //     hasOlderMessages = false;
                //  //   cout << "【这是最早的群聊消息】" << endl;
                // } else {
                //     hasOlderMessages = true; // 如果currentDown < totalCount - 20，说明还有更早的消息
                // }
                // if (currentUp <= 19) {
                //     hasNewerMessages = false;
                //   //  cout << "【这是最新的群聊消息】" << endl;
                // }

                
                std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;
                continue;
                    
                } else {
                    // 如果不是"more"，则继续循环，等待下一次输入
                    continue;
                }
            } else if (order == "2") { // 查看后20条（更新的消息）
                sendMsg(fd, "2");
                recvMsg(fd, reply);
                if (reply == "less") {
                    cout << "已经是最新消息，没有更多内容了哦" << endl;
                    hasNewerMessages = false;
                    std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;
                    continue;
                }
                
                // 接收后20条消息
                system("clear");
                string up_str, down_str;
                recvMsg(fd, up_str);
                recvMsg(fd, down_str);
                
                // 添加错误处理，防止stoi崩溃
                try {
                    currentUp = stoi(up_str);
                    currentDown = stoi(down_str);
                } catch (const std::invalid_argument& e) {
                    cout << "接收消息范围失败，返回主菜单" << endl;
                    cout << "[DEBUG] stoi失败，up_str: '" << up_str << "', down_str: '" << down_str << "'" << endl;
                    return;
                } catch (const std::out_of_range& e) {
                    cout << "消息范围超出范围，返回主菜单" << endl;
                    return;
                }
                hasOlderMessages = true; // 现在有更早的消息了
                
                int messageCount = currentUp - currentDown + 1;
                int displayedCountThisPage = 0;
                for (int k = 0; k < messageCount; k++) {
                    int msg_ret = recvMsg(fd, history_message);
                    if (msg_ret <= 0) {
                        cout << "接收历史消息失败，停止接收" << endl;
                        break;
                    }
                    if (history_message.empty()) continue;
                    
                    try {
                        history.json_parse(history_message);
                        if (history.getUsername() == user.getUsername()) {
                            cout << "你：" << history.getContent() << endl;
                        } else {
                            cout << history.getUsername() << "  :  " << history.getContent() << endl;
                        }
                        cout << "\t\t\t\t" << history.getTime() << endl;
                    } catch (const exception& e) {
                        cout << "解析消息失败，跳过此消息" << endl;
                        continue;
                    }
                }
                
                cout << YELLOW << "-------------------以上为群聊历史消息-------------------" << RESET << endl;
                
                if (displayedCount < 20) {
                    hasOlderMessages = false; // 没满，说明到头
                } else {
                    hasOlderMessages = true; // 满了，可能还有
                }   

                // 更新状态信息
                // hasOlderMessages = true; // 现在有更早的消息了
                // if (currentDown >= totalCount - 20) {
                //     hasOlderMessages = false;
                //  //   cout << "【这是最早的群聊消息】" << endl;
                // } else {
                //     hasOlderMessages = true; // 如果currentDown < totalCount - 20，说明还有更早的消息
                // }
                // if (currentUp <= 19) {
                //     hasNewerMessages = false;
                //   //  cout << "【这是最新的群聊消息】" << endl;
                // }

                
                std::cout << "\033[90m输入【1】查看前20条消息，【2】查看后20条消息，【0】返回\033[0m" << std::endl;
                continue;
            } else {
                cout << "输入无效，请输入【1】、【2】或【0】" << endl;
                continue;
            }
        }
    }
}