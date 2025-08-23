#include "OperationMenu.h"
#include <unistd.h>
#include "FriendManager.h"
#include "chat.h"
#include "G_chat.h"
#include "FileTransfer.h"
#include "Notifications.h"
#include <functional>
#include <map>
#include <iostream>
#include <thread>
#include <iomanip>
#include "nlohmann/json.hpp"
#include "../utils/proto.h"
#include "../utils/IO.h"

using namespace std;
using json = nlohmann::json;

void operationMenu() {
   std::cout << "\033[1;34m--------------------------------------\033[0m" << std::endl;
    cout << "[1]开始聊天                  [2]添加好友" << endl;
    cout << "[3]查看添加好友请求          [4]删除好友" << endl;
    cout << "[5]屏蔽好友                  [6]解除屏蔽" << endl;
    cout << "[7]群聊                      [8]查看历史记录" << endl;
    cout << "[9]注销账户" << endl;
    cout << "按[0]退出登陆" << endl;
    cout << BLUE << "请输入你的选择" << RESET << endl;
}




void clientOperation(int fd, User &user) {
    string my_uid = user.getUID();
    //好友容器，对
    vector<pair<string, User>> my_friends;
    FriendManager friendManager(fd, user);
    ChatSession chatSession(fd, user);
    G_chat gChat(fd, user);
        FileTransfer fileTransfer;
    

    // thread heartbeatThread(heartbeat, user.getUID());
    // heartbeatThread.detach();
    thread unifiedRecver(unifiedMessageReceiver, user.getUID());
    unifiedRecver.detach();

    // 离线消息现在由统一接收线程立即显示，不需要额外处理

    while (true) {
        operationMenu();
        string option;
        getline(cin, option);
        char *end_ptr;
        if (option.empty()) {
            cout << "输入不能为空" << endl;
            continue;
        }
        if (option.length() > 1) {
            cout << "输入错误" << endl;
            continue;
        }
        //0退出登陆
        if (option == "0") {
            json req;
            req["flag"] = C2S_LOGOUT_REQUEST;
            if (sendMsg(fd, req.dump()) <= 0) {
                cout << "服务器连接已断开，客户端退出" << endl;
            } else {
                cout << "退出成功" << endl;
            }
            return;
        }
       
        //字符串类型变成整型，只识别能转换的数字部分
        int opt = (int) strtol(option.c_str(), &end_ptr, 10);
        if (end_ptr == option.c_str() ||   *end_ptr != '\0' || option.find(' ') != std::string::npos) 
        {
            std::cout << "输入格式错误，请输入纯数字选项！" << std::endl;
            continue;
        }
        if ( option.find(' ') != std::string::npos) {
            std::cout << "输入格式错误 请重新输入" << std::endl;
            continue;
        }
        if (opt == 1) {
            json req;
            req["flag"] = C2S_GET_CHAT_LISTS;
            if (sendMsg(fd, req.dump()) <= 0) {
                cout << "服务器连接已断开，无法获取聊天列表" << endl;
                continue;
            }

            string res_str;
            if (recvMsg(fd, res_str) <= 0) {
                cout << "服务器连接已断开，获取聊天列表失败" << endl;
                continue;
            }

            try {
                json res = json::parse(res_str);
                if (res.value("flag", 0) == S2C_CHAT_LISTS_RESPONSE) {
                    my_friends.clear();
                    vector<Group> joinedGroup;

                    // 解析好友列表
                    if (res["data"].contains("friends")) {
                        for (const auto& friend_json : res["data"]["friends"]) {
                            User myfriend;
                            myfriend.json_parse(friend_json);
                            my_friends.emplace_back(my_uid, myfriend);
                        }
                        cout << "[DEBUG] Parsed " << my_friends.size() << " friends." << endl;
                    }

                    // 解析群组列表
                    if (res["data"].contains("groups")) {
                        for (const auto& group_json : res["data"]["groups"]) {
                            Group group;
                            group.json_parse(group_json.dump()); // 假设Group有json_parse方法
                            joinedGroup.push_back(group);
                        }
                    }

                    chatSession.startChat(my_friends, joinedGroup);
                } else {
                    cout << "获取聊天列表失败: " << res["data"].value("reason", "未知错误") << endl;
                }
            } catch (const json::parse_error& e) {
                cerr << "解析聊天列表响应失败: " << e.what() << endl;
            }
        } else if (opt == 2) {
            friendManager.addFriend(my_friends);
        } else if (opt == 3) {
            friendManager.findRequest(my_friends);
        } else if (opt == 4) {
            friendManager.delFriend(my_friends);
        } else if (opt == 5) {
            friendManager.blockedLists(my_friends);
        } else if (opt == 6) {
            friendManager.unblocked(my_friends);
        } else if (opt == 7) {
            json req;
            req["flag"] = C2S_GET_CHAT_LISTS;
            if (sendMsg(fd, req.dump()) <= 0) {
                cout << "服务器连接已断开，无法进入群聊菜单" << endl;
                continue;
            }

            string res_str;
            if (recvMsg(fd, res_str) <= 0) {
                cout << "服务器连接已断开，获取群聊列表失败" << endl;
                continue;
            }

            try {
                json res = json::parse(res_str);
                if (res.value("flag", 0) == S2C_CHAT_LISTS_RESPONSE) {
                    vector<Group> joinedGroup;
                    if (res["data"].contains("groups")) {
                        for (const auto& group_json : res["data"]["groups"]) {
                            Group group;
                            group.json_parse(group_json.dump());
                            joinedGroup.push_back(group);
                        }
                    }
                    gChat.groupctrl(my_friends);
                } else {
                    cout << "进入群聊菜单失败: " << res["data"].value("reason", "未知错误") << endl;
                }
            } catch (const json::parse_error& e) {
                cerr << "解析群聊列表响应失败: " << e.what() << endl;
            }
        } else if (opt == 8) {
            vector<Group> joinedGroup;
            friendManager.listFriends(my_friends, joinedGroup);
            // gChat.syncGL(joinedGroup); // This call is redundant as listFriends already syncs groups, causing a crash.
            chatSession.history(my_friends, joinedGroup);
        } else if (opt == 9) {
            if (deactivateAccount(fd, user)) {
                return; // 注销成功，退出循环
            }
        } else {
            cout << "没有这个选项，请重新输入" << endl;
        }
    }
}

bool deactivateAccount(int fd, User &user) {
    cout << "\n警告：注销账户将永久删除您的所有数据，包括好友和聊天记录。" << endl;
    cout << "此操作无法撤销。您确定要继续吗？ (输入 'yes' 以确认): ";
    string confirmation;
    getline(cin, confirmation);

    if (confirmation == "yes") {
        nlohmann::json req;
        req["flag"] = C2S_DEACTIVATE_ACCOUNT_REQUEST;
        sendMsg(fd, req.dump());

        string response_str;
        if (recvMsg(fd, response_str) > 0) {
            try {
                auto res = nlohmann::json::parse(response_str);
                if (res["flag"].get<int>() == S2C_DEACTIVATE_ACCOUNT_RESPONSE && res["data"]["success"].get<bool>()) {
                    cout << "[系统提示] " << res["data"].value("reason", "") << endl;
                    cout << "感谢您的使用，再见。" << endl;
                    return true; // 注销成功
                } else {
                    cout << "[错误] 注销失败: " << res["data"].value("reason", "未知错误") << endl;
                }
            } catch (const nlohmann::json::parse_error& e) {
                cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
            }
        } else {
            cout << "[错误] 未收到服务器确认，请重新登录后再试。" << endl;
        }
    } else {
        cout << "已取消注销操作。" << endl;
    }
    return false; // 注销失败或取消
}

