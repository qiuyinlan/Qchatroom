#include "FriendManager.h"
#include "../utils/proto.h"
#include "../utils/IO.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
using namespace std;

FriendManager::FriendManager(int fd, User user) : fd(fd), user(std::move(user)) {}

void FriendManager::listFriends(std::vector<std::pair<std::string, User>> &my_friends, std::vector<Group> &joinedGroup) {
    nlohmann::json req;
    req["flag"] = C2S_GET_CHAT_LISTS;
    sendMsg(fd, req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器获取好友列表失败" << endl;
        return;
    }

    try {
        nlohmann::json res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() == S2C_CHAT_LISTS_RESPONSE) {
            my_friends.clear();
            if (res["data"].contains("friends")) {
                for (const auto& friend_json : res["data"]["friends"]) {
                    User friend_user;
                    friend_user.json_parse(friend_json.dump());
                    my_friends.push_back({"", friend_user});
                }
            }

            joinedGroup.clear();
            if (res["data"].contains("groups")) {
                for (const auto& group_json : res["data"]["groups"]) {
                    Group group;
                    group.json_parse(group_json.dump());
                    joinedGroup.push_back(group);
                }
            }
        }
    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析好友列表响应失败: " << e.what() << endl;
    }
}


void FriendManager::addFriend(vector<pair<string, User>> &my_friends) const {
    string username;
    cout << "\n======================================" << endl;
    cout << "请输入要添加好友的用户名: ";
    cin >> username;

    if (username == "0") {
        return;
    }

    nlohmann::json req;
    req["flag"] = C2S_ADD_FRIEND_REQUEST;
    req["data"]["friend_username"] = username;
    sendMsg(fd, req.dump());

    // 等待并接收服务器的响应
    string response_str;
    if (recvMsg(fd, response_str) > 0) {
        try {
            nlohmann::json res = nlohmann::json::parse(response_str);
            if (res["flag"].get<int>() == S2C_ADD_FRIEND_RESPONSE) {
                string reason = res["data"]["reason"].get<string>();
                cout << "[系统提示] " << reason << endl;
            } else {
                cout << "[错误] 收到未知的响应类型" << endl;
            }
        } catch (const nlohmann::json::parse_error& e) {
            cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
        }
    } else {
        cout << "[错误] 从服务器接收响应失败" << endl;
    }
    cout << "======================================\n" << endl;
}

void FriendManager::findRequest(vector<pair<string, User>> &my_friends) const {
    nlohmann::json get_req;
    get_req["flag"] = C2S_GET_FRIEND_REQUESTS;
    sendMsg(fd, get_req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器接收响应失败" << endl;
        return;
    }

    try {
        nlohmann::json res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() != S2C_FRIEND_REQUESTS_RESPONSE || !res["data"]["success"].get<bool>()) {
            cout << "[错误] 获取好友请求列表失败: " << res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto requests = res["data"]["requests"];
        if (requests.empty()) {
            cout << "\n目前没有好友申请。\n" << endl;
            return;
        }

        cout << "\n你收到 " << requests.size() << " 条好友申请:" << endl;
        cout << "======================================" << endl;

        for (const auto& req_user_json : requests) {
            User requester;
            requester.json_parse(req_user_json.dump());

            cout << "收到来自 " << requester.getUsername() << " (UID: " << requester.getUID() << ") 的好友申请。" << endl;
            cout << "请选择: [1]同意 [2]拒绝 [3]忽略 [0]返回菜单" << endl;

            int choice;
            cin >> choice;
            cin.ignore(INT32_MAX, '\n');

            if (choice == 0) break;
            if (choice == 3) continue;

            if (choice == 1 || choice == 2) {
                nlohmann::json respond_req;
                respond_req["flag"] = C2S_RESPOND_TO_FRIEND_REQUEST;
                respond_req["data"]["requester_uid"] = requester.getUID();
                respond_req["data"]["accepted"] = (choice == 1);
                sendMsg(fd, respond_req.dump());

                string respond_res_str;
                if (recvMsg(fd, respond_res_str) > 0) {
                    auto respond_res = nlohmann::json::parse(respond_res_str);
                    cout << "[系统提示] " << respond_res["data"].value("reason", "") << endl;
                } else {
                    cout << "[错误] 未收到服务器确认" << endl;
                }
            }
        }
        cout << "======================================\n" << endl;

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
}

void FriendManager::delFriend(vector<pair<string, User>> &my_friends) {
    if (my_friends.empty()) {
        cout << "\n你当前没有好友。\n" << endl;
        return;
    }

    cout << "\n你的好友列表:" << endl;
    cout << "======================================" << endl;
    for (int i = 0; i < my_friends.size(); ++i) {
        cout << i + 1 << ". " << my_friends[i].second.getUsername() << endl;
    }
    cout << "======================================" << endl;
    cout << "请输入要删除的好友序号 (输入0返回): ";

    int who;
    cin >> who;
    cin.ignore(INT32_MAX, '\n');

    if (who == 0) return;
    if (who < 1 || who > my_friends.size()) {
        cout << "[错误] 无效的序号。" << endl;
        return;
    }

    nlohmann::json req;
    req["flag"] = C2S_DELETE_FRIEND_REQUEST;
    req["data"]["friend_uid"] = my_friends[who - 1].second.getUID();
    sendMsg(fd, req.dump());

    string response_str;
    if (recvMsg(fd, response_str) > 0) {
        try {
            auto res = nlohmann::json::parse(response_str);
            if (res["flag"].get<int>() == S2C_DELETE_FRIEND_RESPONSE) {
                cout << "[系统提示] " << res["data"].value("reason", "") << endl;
            } else {
                cout << "[错误] 收到未知的响应类型" << endl;
            }
        } catch (const nlohmann::json::parse_error& e) {
            cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
        }
    } else {
        cout << "[错误] 未收到服务器确认" << endl;
    }
    cout << "======================================\n" << endl;
}

void FriendManager::blockedLists(vector<pair<string, User>> &my_friends) const {
    if (my_friends.empty()) {
        cout << "\n你当前没有好友。\n" << endl;
        return;
    }

    cout << "\n好友列表:" << endl;
    cout << "======================================" << endl;
    for (int i = 0; i < my_friends.size(); ++i) {
        cout << i + 1 << ". " << my_friends[i].second.getUsername() << endl;
    }
    cout << "======================================" << endl;
    cout << "请输入要屏蔽的好友序号 (输入0返回): ";

    int who;
    cin >> who;
    cin.ignore(INT32_MAX, '\n');

    if (who == 0) return;
    if (who < 1 || who > my_friends.size()) {
        cout << "[错误] 无效的序号。" << endl;
        return;
    }

    nlohmann::json req;
    req["flag"] = C2S_BLOCK_FRIEND_REQUEST;
    req["data"]["friend_uid"] = my_friends[who - 1].second.getUID();
    sendMsg(fd, req.dump());

    string response_str;
    if (recvMsg(fd, response_str) > 0) {
        try {
            auto res = nlohmann::json::parse(response_str);
            if (res["flag"].get<int>() == S2C_BLOCK_FRIEND_RESPONSE) {
                cout << "[系统提示] " << res["data"].value("reason", "") << endl;
            } else {
                cout << "[错误] 收到未知的响应类型" << endl;
            }
        } catch (const nlohmann::json::parse_error& e) {
            cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
        }
    } else {
        cout << "[错误] 未收到服务器确认" << endl;
    }
    cout << "======================================\n" << endl;
}

void FriendManager::unblocked(vector<pair<string, User>> &my_friends) const {
    nlohmann::json get_req;
    get_req["flag"] = C2S_GET_BLOCKED_LIST_REQUEST;
    sendMsg(fd, get_req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器接收响应失败" << endl;
        return;
    }

    try {
        nlohmann::json res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() != S2C_BLOCKED_LIST_RESPONSE || !res["data"]["success"].get<bool>()) {
            cout << "[错误] 获取屏蔽列表失败: " << res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto blocked_list = res["data"]["blocked_list"];
        if (blocked_list.empty()) {
            cout << "\n你的屏蔽列表为空。\n" << endl;
            return;
        }

        vector<User> blocked_users;
        cout << "\n你的屏蔽列表:" << endl;
        cout << "======================================" << endl;
        int i = 1;
        for (const auto& user_json : blocked_list) {
            User blocked_user;
            blocked_user.json_parse(user_json.dump());
            blocked_users.push_back(blocked_user);
            cout << i++ << ". " << blocked_user.getUsername() << endl;
        }
        cout << "======================================" << endl;
        cout << "请输入要解除屏蔽的好友序号 (输入0返回): ";

        int who;
        cin >> who;
        cin.ignore(INT32_MAX, '\n');

        if (who == 0) return;
        if (who < 1 || who > blocked_users.size()) {
            cout << "[错误] 无效的序号。" << endl;
            return;
        }

        nlohmann::json unblock_req;
        unblock_req["flag"] = C2S_UNBLOCK_FRIEND_REQUEST;
        unblock_req["data"]["friend_uid"] = blocked_users[who - 1].getUID();
        sendMsg(fd, unblock_req.dump());

        string unblock_res_str;
        if (recvMsg(fd, unblock_res_str) > 0) {
            auto unblock_res = nlohmann::json::parse(unblock_res_str);
            cout << "[系统提示] " << unblock_res["data"].value("reason", "") << endl;
        } else {
            cout << "[错误] 未收到服务器确认" << endl;
        }

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
    cout << "======================================\n" << endl;
}
