#include "G_chat.h"
#include <stdexcept>
#include <unistd.h>
#include "User.h"
#include "../utils/IO.h"
#include "Group.h"
#include "proto.h"
#include <iostream>
#include <vector>
#include <cstdint>
#include <thread>
#include <functional>
#include <map>
#include <exception>
#include "Notifications.h"
using namespace std;

void G_chat::groupMenu() {
    
    cout << "[1] 创建群聊                   [2] 加入群聊" << endl;
    cout << "[3] 管理我的群                 [4] 查看群成员" << endl;
    cout << "[5] 退出群聊                   [6] 拉人进群聊" << endl;
    cout << "[0] 返回" << endl;
    cout << "请输入你的选择" << endl;

}


void G_chat::groupctrl(vector<pair<string, User>> &my_friends) {
    vector<Group> joinedGroup;
    syncGL(joinedGroup);
    sendMsg(fd, GROUP);
    vector<Group> managedGroup;
    vector<Group> createdGroup;
    
    int option;
    while (true) {
        groupMenu();
        //自动发完，两个同步函数，对应同步
        sync(createdGroup, managedGroup, joinedGroup);
        //在调用的函数里面send信号，调用服务器函数
        while (!(cin >> option)) {
            if (cin.eof()) {
                cout << "读到文件结尾" << endl;
                return;
            }
            cout << "输入格式错误" << endl;
            cin.clear();
            cin.ignore(INT32_MAX, '\n');
        }
        cin.ignore(INT32_MAX, '\n');
     
        if (option == 0) {
            sendMsg(fd, BACK);
            return;
        }
        else if (option == 1) {
            createGroup();
            continue;
        } else if (option == 2) {
            joinGroup();
            continue;
        } 
        else if (option == 3) {
            managed_Group(managedGroup);
            continue;
        } 
        else if (option == 4) {
            showMembers(joinedGroup);
            continue;
        } else if (option == 5) {
            quit(joinedGroup);
            continue;
        } else if (option == 6) {
            getperson(joinedGroup);
            continue;
        } else {
            cout << "没有这个选项！" << endl;
            continue;
        }
       

    }
} 

G_chat::G_chat(int fd, const User &user) : fd(fd), user(user) {
    joined = "joined" + user.getUID();
    created = "created" + user.getUID();
    managed = "managed" + user.getUID();
}

void G_chat::syncGL(std::vector<Group> &joinedGroup) {
    joinedGroup.clear();
    // 发送群聊列表获取请求
    nlohmann::json req;
    req["flag"] = C2S_GET_JOINED_GROUPS_REQUEST;
    int ret = sendMsg(fd, req.dump());
    if (ret <= 0) {
        cerr << "[ERROR] 发送同步群聊列表请求失败" << endl;
        return;
    }
    string nums;

    //接受群聊数量
    int recv_ret = recvMsg(fd, nums);
    if (recv_ret <= 0) {
        cerr << "[ERROR] 接收群聊数量失败，连接可能断开" << endl;
        return;
    }
    int num = stoi(nums);
    if(num == 0){
        return;
        //如果要同步三个，就不能直接返回了。直接返回——没有东西被push进数组，数组为空
    }
    string group_info;
    //接收群info

    for (int i = 0; i < num; i++) {
        Group group;

        //接收群聊,如果有群聊,就要json解析，没有直接return
        int ret = recvMsg(fd, group_info);
        if(ret <= 0){
            cerr << "[ERROR] 接收群聊名称失败，连接可能断开" << endl;
            return;
        }
        try {
            if (group_info == "0" ) {
                return;
            }
            
            group.json_parse(group_info);
            joinedGroup.push_back(group);
        } catch (const exception& e) {
            cerr << "[ERROR] 创建的群JSON解析失败: " << e.what() << endl;
            cerr << "[ERROR] 问题JSON: " << group_info << endl;
            throw; 
        }

    }

}



void G_chat::sync(vector<Group> &createdGroup, vector<Group> &managedGroup, vector<Group> &joinedGroup) const {
    //sendMsg(fd,"sync");
    createdGroup.clear();
    managedGroup.clear();
    joinedGroup.clear();


    string nums;

    //得到群聊数量
    int recv_ret = recvMsg(fd, nums);

    if (recv_ret <= 0) {
        cerr << "[ERROR] 接收创建群数量失败，连接可能断开" << endl;
    }

    int num;
    try {
        if (nums.empty()) {
            cerr << "[ERROR] 接收到空的创建群数量" << endl;
            throw runtime_error("接收到空数据");
        }
        num = stoi(nums);
        if (num < 0 || num > 1000) {
            cerr << "[ERROR] 创建群数量异常: " << num << endl;
            throw runtime_error("数据异常");
        }
    } catch (const exception& e) {
        cerr << "[ERROR] 解析创建群数量失败: " << e.what() << ", 内容: '" << nums << "'" << endl;
        throw;
    }
    string group_info;
    for (int i = 0; i < num; i++) {
        Group group;

        recvMsg(fd, group_info);
        try {
            // 验证JSON格式
            if (group_info.empty() || group_info[0] != '{' || group_info.back() != '}') {
                cerr << "[ERROR] 接收到无效的JSON格式: " << group_info << endl;
                throw runtime_error("JSON格式错误");
            }
            group.json_parse(group_info);
            createdGroup.push_back(group);
        } catch (const exception& e) {
            cerr << "[ERROR] 创建的群JSON解析失败: " << e.what() << endl;
            cerr << "[ERROR] 问题JSON: " << group_info << endl;
            throw; // 重新抛出异常，让上层处理
        }
    }
    //收
    recv_ret = recvMsg(fd, nums);

    if (recv_ret <= 0) {
        cerr << "[ERROR] 接收管理群数量失败，连接可能断开" << endl;
        throw runtime_error("网络连接错误");
    }

    try {
        if (nums.empty()) {
            cerr << "[ERROR] 接收到空的管理群数量" << endl;
            throw runtime_error("接收到空数据");
        }
        num = stoi(nums);
        if (num < 0 || num > 1000) {
            cerr << "[ERROR] 管理群数量异常: " << num << endl;
            throw runtime_error("数据异常");
        }
    } catch (const exception& e) {
        cerr << "[ERROR] 解析管理群数量失败: " << e.what() << ", 内容: '" << nums << "'" << endl;
        throw;
    }
    for (int i = 0; i < num; i++) {
        Group group;

        recvMsg(fd, group_info);
        try {
            group.json_parse(group_info);
            managedGroup.push_back(group);
        } catch (const exception& e) {
            cerr << "[ERROR] 管理的群JSON解析失败: " << e.what() << endl;
            cerr << "[ERROR] 问题JSON: " << group_info << endl;
        }
    }
    //收
    recv_ret = recvMsg(fd, nums);

    if (recv_ret <= 0) {
        cerr << "[ERROR] 接收加入群数量失败，连接可能断开" << endl;
        throw runtime_error("网络连接错误");
    }

    try {
        if (nums.empty()) {
            cerr << "[ERROR] 接收到空的加入群数量" << endl;
            throw runtime_error("接收到空数据");
        }
        num = stoi(nums);
        if (num < 0 || num > 1000) {
            cerr << "[ERROR] 加入群数量异常: " << num << endl;
            throw runtime_error("数据异常");
        }
    } catch (const exception& e) {
        cerr << "[ERROR] 解析加入群数量失败: " << e.what() << ", 内容: '" << nums << "'" << endl;
        throw;
    }
    for (int i = 0; i < num; i++) {
        Group group;

        recvMsg(fd, group_info);
        try {
            group.json_parse(group_info);
            joinedGroup.push_back(group);
        } catch (const exception& e) {
            cerr << "[ERROR] 加入的群JSON解析失败: " << e.what() << endl;
            cerr << "[ERROR] 问题JSON: " << group_info << endl;
        }
    }

}


void G_chat::createGroup() {
    cout << "\n======================================" << endl;
    cout << "请输入你要创建的群聊名称 (输入0返回): ";
    string groupName;
    getline(cin, groupName);

    if (groupName == "0" || groupName.empty()) {
        cout << "已取消创建群聊。" << endl;
        cout << "======================================\n" << endl;
        return;
    }

    nlohmann::json req;
    req["flag"] = C2S_CREATE_GROUP_REQUEST;
    req["data"]["group_name"] = groupName;
    sendMsg(fd, req.dump());

    string response_str;
    if (recvMsg(fd, response_str) > 0) {
        try {
            auto res = nlohmann::json::parse(response_str);
            if (res["flag"].get<int>() == S2C_CREATE_GROUP_RESPONSE) {
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

void G_chat::joinGroup() const {
    cout << "\n======================================" << endl;
    cout << "请输入你要加入的群聊名称 (输入0返回): ";
    string groupName;
    getline(cin, groupName);

    if (groupName == "0" || groupName.empty()) {
        cout << "已取消加入群聊。" << endl;
        cout << "======================================\n" << endl;
        return;
    }

    nlohmann::json req;
    req["flag"] = C2S_JOIN_GROUP_REQUEST;
    req["data"]["group_name"] = groupName;
    sendMsg(fd, req.dump());

    string response_str;
    if (recvMsg(fd, response_str) > 0) {
        try {
            auto res = nlohmann::json::parse(response_str);
            if (res["flag"].get<int>() == S2C_JOIN_GROUP_RESPONSE) {
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


void G_chat::managedMenu() {
    cout << "[1]处理入群申请" << endl;
    cout << "[2]踢出群用户" << endl;
    cout << "[0]返回" << endl;
}

void G_chat::ownerMenu() {
    cout << "[1]处理入群申请" << endl;
    cout << "[2]踢出群用户" << endl;
    cout << "[3]设置管理员" << endl;
    cout << "[4]撤销管理员" << endl;
    cout << "[5]解散群聊" << endl;
    cout << "[0]返回" << endl;
}


void G_chat::managed_Group(vector<Group> &managedGroup) const {
    nlohmann::json req;
    req["flag"] = C2S_GET_MANAGED_GROUPS_REQUEST;
    sendMsg(fd, req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器获取列表失败" << endl;
        return;
    }

    try {
        auto res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() != S2C_MANAGED_GROUPS_RESPONSE || !res["data"]["success"].get<bool>()) {
            cout << "[错误] " << res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto managed_groups_json = res["data"]["managed_groups"];
        if (managed_groups_json.empty()) {
            cout << "\n你当前还没有可以管理的群...\n" << endl;
            return;
        }

        vector<Group> local_managed_groups;
        for (const auto& group_json : managed_groups_json) {
            Group group;
            group.json_parse(group_json.dump());
            local_managed_groups.push_back(group);
        }

        cout << "\n-----------------------------------" << endl;
        cout << "你管理的群及你的身份" << endl;
        for (int i = 0; i < local_managed_groups.size(); i++) {
            const Group& group = local_managed_groups[i];
            string role = (group.getOwnerUid() == user.getUID()) ? "(群主)" : "(管理员)";
            cout << i + 1 << ". " << group.getGroupName() << role << endl;
        }
        cout << "-----------------------------------" << endl;
        cout << "选择你要管理的群 (输入0返回): ";

        int which;
        cin >> which;
        cin.ignore(INT32_MAX, '\n');

        if (which == 0) return;
        if (which < 1 || which > local_managed_groups.size()) {
            cout << "[错误] 无效的序号。" << endl;
            return;
        }

        Group& selected_group = local_managed_groups[which - 1];

        // TODO: 将下面的管理操作也迁移到JSON协议
        sendMsg(fd, "3"); // 发送旧的管理群组信号，以便服务器进入旧的处理流程
        sendMsg(fd, selected_group.to_json());

        if (selected_group.getOwnerUid() == user.getUID()) {
            ownerMenu();
            int choice;
            cin >> choice;
            cin.ignore(INT32_MAX, '\n');
            if (choice == 0) { sendMsg(fd, BACK); return; }
                        if (choice == 1) approve(selected_group);
            else if (choice == 2) remove(selected_group);
            else if (choice == 3) appointAdmin(selected_group);
            else if (choice == 4) revokeAdmin(selected_group);
            else if (choice == 5) deleteGroup(selected_group);
        } else {
            managedMenu();
            int choice;
            cin >> choice;
            cin.ignore(INT32_MAX, '\n');
            if (choice == 0) { sendMsg(fd, BACK); return; }
                        if (choice == 1) approve(selected_group);
            else if (choice == 2) remove(selected_group);
        }

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
}

void G_chat::approve(const Group& group) const {
    nlohmann::json get_req;
    get_req["flag"] = C2S_GET_GROUP_JOIN_REQUESTS;
    get_req["data"]["group_uid"] = group.getGroupUid();
    sendMsg(fd, get_req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器获取列表失败" << endl;
        return;
    }

    try {
        auto res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() != S2C_GROUP_JOIN_REQUESTS_RESPONSE || !res["data"]["success"].get<bool>()) {
            cout << "[错误] 获取申请列表失败: " << res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto requests = res["data"]["requests"];
        if (requests.empty()) {
            cout << "\n该群聊暂无新的入群申请。\n" << endl;
            return;
        }

        cout << "\n收到 " << requests.size() << " 条入群申请:" << endl;
        cout << "======================================" << endl;

        for (const auto& req_user_json : requests) {
            User requester;
            requester.json_parse(req_user_json.dump());

            cout << "收到来自 " << requester.getUsername() << " (UID: " << requester.getUID() << ") 的入群申请。" << endl;
            cout << "请选择: [1]同意 [2]拒绝 [3]忽略" << endl;

            int choice;
            cin >> choice;
            cin.ignore(INT32_MAX, '\n');

            if (choice == 3) continue;

            if (choice == 1 || choice == 2) {
                nlohmann::json respond_req;
                respond_req["flag"] = C2S_RESPOND_TO_GROUP_JOIN_REQUEST;
                respond_req["data"]["group_uid"] = group.getGroupUid();
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

void G_chat::remove(Group &group) const {
    nlohmann::json get_req;
    get_req["flag"] = C2S_GET_GROUP_MEMBERS_REQUEST;
    get_req["data"]["group_uid"] = group.getGroupUid();
    sendMsg(fd, get_req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器获取成员列表失败" << endl;
        return;
    }

    try {
        auto res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() != S2C_GROUP_MEMBERS_RESPONSE || !res["data"]["success"].get<bool>()) {
            cout << "[错误] " << res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto members_json = res["data"]["members"];
        if (members_json.empty()) {
            cout << "\n群聊中没有其他成员。\n" << endl;
            return;
        }

        vector<User> members;
        cout << "\n-----------------------------------" << endl;
        cout << "群成员列表:" << endl;
        int i = 1;
        for (const auto& member_json : members_json) {
            User member;
            member.json_parse(member_json.dump());
            members.push_back(member);
            string role = (member.getUID() == group.getOwnerUid()) ? "(群主)" : "";
            cout << i++ << ". " << member.getUsername() << role << endl;
        }
        cout << "-----------------------------------" << endl;
        cout << "请输入要踢出的成员序号 (输入0返回): ";

        int who;
        cin >> who;
        cin.ignore(INT32_MAX, '\n');

        if (who == 0) return;
        if (who < 1 || who > members.size()) {
            cout << "[错误] 无效的序号。" << endl;
            return;
        }

        User& to_kick = members[who - 1];

        if (to_kick.getUID() == group.getOwnerUid()) {
            cout << "[错误] 不能踢出群主。" << endl;
            return;
        }
        if (to_kick.getUID() == user.getUID()) {
            cout << "[错误] 不能踢出自己。" << endl;
            return;
        }

        nlohmann::json kick_req;
        kick_req["flag"] = C2S_KICK_GROUP_MEMBER_REQUEST;
        kick_req["data"]["group_uid"] = group.getGroupUid();
        kick_req["data"]["kick_uid"] = to_kick.getUID();
        sendMsg(fd, kick_req.dump());

        string kick_res_str;
        if (recvMsg(fd, kick_res_str) > 0) {
            auto kick_res = nlohmann::json::parse(kick_res_str);
            cout << "[系统提示] " << kick_res["data"].value("reason", "") << endl;
        } else {
            cout << "[错误] 未收到服务器确认" << endl;
        }

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
    cout << "======================================\n" << endl;
}


void G_chat::appointAdmin(Group &group) const {
    nlohmann::json get_req;
    get_req["flag"] = C2S_GET_GROUP_MEMBERS_REQUEST;
    get_req["data"]["group_uid"] = group.getGroupUid();
    sendMsg(fd, get_req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器获取成员列表失败" << endl;
        return;
    }

    try {
        auto res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() != S2C_GROUP_MEMBERS_RESPONSE || !res["data"]["success"].get<bool>()) {
            cout << "[错误] " << res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto members_json = res["data"]["members"];
        vector<User> non_admins;
        for (const auto& member_json : members_json) {
            User member;
            member.json_parse(member_json.dump());
            // 客户端可以预先过滤掉已经是管理员或群主的成员
            // 这里为了演示，假设服务器会做最终校验，显示所有非群主成员
            if (member.getUID() != group.getOwnerUid()) {
                non_admins.push_back(member);
            }
        }

        if (non_admins.empty()) {
            cout << "\n没有可以任命为管理员的成员。\n" << endl;
            return;
        }

        cout << "\n-----------------------------------" << endl;
        cout << "选择要任命为管理员的成员:" << endl;
        for (int i = 0; i < non_admins.size(); ++i) {
            cout << i + 1 << ". " << non_admins[i].getUsername() << endl;
        }
        cout << "-----------------------------------" << endl;
        cout << "请输入序号 (输入0返回): ";

        int who;
        cin >> who;
        cin.ignore(INT32_MAX, '\n');

        if (who == 0) return;
        if (who < 1 || who > non_admins.size()) {
            cout << "[错误] 无效的序号。" << endl;
            return;
        }

        nlohmann::json appoint_req;
        appoint_req["flag"] = C2S_APPOINT_ADMIN_REQUEST;
        appoint_req["data"]["group_uid"] = group.getGroupUid();
        appoint_req["data"]["appoint_uid"] = non_admins[who - 1].getUID();
        sendMsg(fd, appoint_req.dump());

        string appoint_res_str;
        if (recvMsg(fd, appoint_res_str) > 0) {
            auto appoint_res = nlohmann::json::parse(appoint_res_str);
            cout << "[系统提示] " << appoint_res["data"].value("reason", "") << endl;
        } else {
            cout << "[错误] 未收到服务器确认" << endl;
        }

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
    cout << "======================================\n" << endl;
}

void G_chat::revokeAdmin(Group &group) const {
    nlohmann::json get_req;
    get_req["flag"] = C2S_GET_GROUP_ADMINS_REQUEST;
    get_req["data"]["group_uid"] = group.getGroupUid();
    sendMsg(fd, get_req.dump());

    string response_str;
    if (recvMsg(fd, response_str) <= 0) {
        cout << "[错误] 从服务器获取管理员列表失败" << endl;
        return;
    }

    try {
        auto res = nlohmann::json::parse(response_str);
        if (res["flag"].get<int>() != S2C_GROUP_ADMINS_RESPONSE || !res["data"]["success"].get<bool>()) {
            cout << "[错误] " << res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto admins_json = res["data"]["admins"];
        vector<User> revocable_admins;
        for (const auto& admin_json : admins_json) {
            User admin;
            admin.json_parse(admin_json.dump());
            if (admin.getUID() != group.getOwnerUid()) {
                revocable_admins.push_back(admin);
            }
        }

        if (revocable_admins.empty()) {
            cout << "\n没有可以撤销的管理员。\n" << endl;
            return;
        }

        cout << "\n-----------------------------------" << endl;
        cout << "选择要撤销的管理员:" << endl;
        for (int i = 0; i < revocable_admins.size(); ++i) {
            cout << i + 1 << ". " << revocable_admins[i].getUsername() << endl;
        }
        cout << "-----------------------------------" << endl;
        cout << "请输入序号 (输入0返回): ";

        int who;
        cin >> who;
        cin.ignore(INT32_MAX, '\n');

        if (who == 0) return;
        if (who < 1 || who > revocable_admins.size()) {
            cout << "[错误] 无效的序号。" << endl;
            return;
        }

        nlohmann::json revoke_req;
        revoke_req["flag"] = C2S_REVOKE_ADMIN_REQUEST;
        revoke_req["data"]["group_uid"] = group.getGroupUid();
        revoke_req["data"]["revoke_uid"] = revocable_admins[who - 1].getUID();
        sendMsg(fd, revoke_req.dump());

        string revoke_res_str;
        if (recvMsg(fd, revoke_res_str) > 0) {
            auto revoke_res = nlohmann::json::parse(revoke_res_str);
            cout << "[系统提示] " << revoke_res["data"].value("reason", "") << endl;
        } else {
            cout << "[错误] 未收到服务器确认" << endl;
        }

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
    cout << "======================================\n" << endl;
}

void G_chat::deleteGroup(Group &group) const {
    cout << "\n警告：解散群聊将永久删除所有群成员和聊天记录。" << endl;
    cout << "此操作无法撤销。您确定要继续吗？ (输入 'yes' 以确认): ";
    string confirmation;
    getline(cin, confirmation);

    if (confirmation != "yes") {
        cout << "已取消解散群聊操作。" << endl;
        return;
    }

    nlohmann::json req;
    req["flag"] = C2S_DELETE_GROUP_REQUEST;
    req["data"]["group_uid"] = group.getGroupUid();
    sendMsg(fd, req.dump());

    string response_str;
    if (recvMsg(fd, response_str) > 0) {
        try {
            auto res = nlohmann::json::parse(response_str);
            cout << "[系统提示] " << res["data"].value("reason", "") << endl;
        } catch (const nlohmann::json::parse_error& e) {
            cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
        }
    } else {
        cout << "[错误] 未收到服务器确认" << endl;
    }
    cout << "======================================\n" << endl;
}

void G_chat::showMembers(std::vector<Group> &joinedGroup) {
    nlohmann::json get_groups_req;
    get_groups_req["flag"] = C2S_GET_JOINED_GROUPS_REQUEST;
    sendMsg(fd, get_groups_req.dump());

    string groups_res_str;
    if (recvMsg(fd, groups_res_str) <= 0) {
        cout << "[错误] 从服务器获取群聊列表失败" << endl;
        return;
    }

    try {
        auto groups_res = nlohmann::json::parse(groups_res_str);
        if (groups_res["flag"].get<int>() != S2C_JOINED_GROUPS_RESPONSE || !groups_res["data"]["success"].get<bool>()) {
            cout << "[错误] " << groups_res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto joined_groups_json = groups_res["data"]["joined_groups"];
        if (joined_groups_json.empty()) {
            cout << "\n你当前还没有加入任何群聊...\n" << endl;
            return;
        }

        vector<Group> local_joined_groups;
        for (const auto& group_json : joined_groups_json) {
            Group group;
            group.json_parse(group_json.dump());
            local_joined_groups.push_back(group);
        }

        cout << "\n--------------------------" << endl;
        for (int i = 0; i < local_joined_groups.size(); i++) {
            cout << i + 1 << ". " << local_joined_groups[i].getGroupName() << endl;
        }
        cout << "--------------------------" << endl;
        cout << "你要查看哪个群 (输入0返回): ";

        int which;
        cin >> which;
        cin.ignore(INT32_MAX, '\n');

        if (which == 0) return;
        if (which < 1 || which > local_joined_groups.size()) {
            cout << "[错误] 无效的序号。" << endl;
            return;
        }

        Group& selected_group = local_joined_groups[which - 1];

        nlohmann::json get_members_req;
        get_members_req["flag"] = C2S_GET_GROUP_MEMBERS_REQUEST;
        get_members_req["data"]["group_uid"] = selected_group.getGroupUid();
        sendMsg(fd, get_members_req.dump());

        string members_res_str;
        if (recvMsg(fd, members_res_str) <= 0) {
            cout << "[错误] 从服务器获取成员列表失败" << endl;
            return;
        }

        auto members_res = nlohmann::json::parse(members_res_str);
        if (members_res["flag"].get<int>() != S2C_GROUP_MEMBERS_RESPONSE || !members_res["data"]["success"].get<bool>()) {
            cout << "[错误] " << members_res["data"].value("reason", "未知错误") << endl;
            return;
        }

        cout << "\n-----------------------------------" << endl;
        cout << "群聊名称: " << selected_group.getGroupName() << endl;
        cout << "【成员列表】" << endl;
        auto members_json = members_res["data"]["members"];
        int i = 1;
        for (const auto& member_json : members_json) {
            User member;
            member.json_parse(member_json.dump());
            cout << i++ << ". " << member.getUsername() << endl;
        }
        cout << "-----------------------------------\n" << endl;

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
}

void G_chat::quit(vector<Group> &joinedGroup) {
    nlohmann::json get_groups_req;
    get_groups_req["flag"] = C2S_GET_JOINED_GROUPS_REQUEST;
    sendMsg(fd, get_groups_req.dump());

    string groups_res_str;
    if (recvMsg(fd, groups_res_str) <= 0) {
        cout << "[错误] 从服务器获取群聊列表失败" << endl;
        return;
    }

    try {
        auto groups_res = nlohmann::json::parse(groups_res_str);
        if (groups_res["flag"].get<int>() != S2C_JOINED_GROUPS_RESPONSE || !groups_res["data"]["success"].get<bool>()) {
            cout << "[错误] " << groups_res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto joined_groups_json = groups_res["data"]["joined_groups"];
        if (joined_groups_json.empty()) {
            cout << "\n你当前还没有加入任何群聊...\n" << endl;
            return;
        }

        vector<Group> local_joined_groups;
        cout << "\n-------------------------------------------" << endl;
        cout << "你加入的群及你的身份" << endl;
        int i = 1;
        for (const auto& group_json : joined_groups_json) {
            Group group;
            group.json_parse(group_json.dump());
            local_joined_groups.push_back(group);
            string role = (group.getOwnerUid() == user.getUID()) ? "(群主)" : "";
            cout << i++ << ". " << group.getGroupName() << role << endl;
        }
        cout << "-------------------------------------------" << endl;
        cout << "输入你要退出的群 (输入0返回): ";

        int which;
        cin >> which;
        cin.ignore(INT32_MAX, '\n');

        if (which == 0) return;
        if (which < 1 || which > local_joined_groups.size()) {
            cout << "[错误] 无效的序号。" << endl;
            return;
        }

        Group& selected_group = local_joined_groups[which - 1];

        if (selected_group.getOwnerUid() == user.getUID()) {
            cout << "[错误] 你是该群群主，不能退出，请在管理群聊中解散该群。" << endl;
            return;
        }

        nlohmann::json quit_req;
        quit_req["flag"] = C2S_QUIT_GROUP_REQUEST;
        quit_req["data"]["group_uid"] = selected_group.getGroupUid();
        sendMsg(fd, quit_req.dump());

        string quit_res_str;
        if (recvMsg(fd, quit_res_str) > 0) {
            auto quit_res = nlohmann::json::parse(quit_res_str);
            cout << "[系统提示] " << quit_res["data"].value("reason", "") << endl;
        } else {
            cout << "[错误] 未收到服务器确认" << endl;
        }

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
    cout << "======================================\n" << endl;
}

 void G_chat::getperson(vector<Group> &group){
    nlohmann::json get_groups_req;
    get_groups_req["flag"] = C2S_GET_JOINED_GROUPS_REQUEST;
    sendMsg(fd, get_groups_req.dump());

    string groups_res_str;
    if (recvMsg(fd, groups_res_str) <= 0) {
        cout << "[错误] 从服务器获取群聊列表失败" << endl;
        return;
    }

    try {
        auto groups_res = nlohmann::json::parse(groups_res_str);
        if (groups_res["flag"].get<int>() != S2C_JOINED_GROUPS_RESPONSE || !groups_res["data"]["success"].get<bool>()) {
            cout << "[错误] " << groups_res["data"].value("reason", "未知错误") << endl;
            return;
        }

        auto joined_groups_json = groups_res["data"]["joined_groups"];
        if (joined_groups_json.empty()) {
            cout << "\n你当前还没有加入任何群聊...\n" << endl;
            return;
        }

        vector<Group> local_joined_groups;
        cout << "\n--------------------------" << endl;
        for (int i = 0; i < joined_groups_json.size(); i++) {
            Group group_item;
            group_item.json_parse(joined_groups_json[i].dump());
            local_joined_groups.push_back(group_item);
            cout << i + 1 << ". " << group_item.getGroupName() << endl;
        }
        cout << "--------------------------" << endl;
        cout << "你要向哪个群拉入你的好友 (输入0返回): ";

        int which;
        cin >> which;
        cin.ignore(INT32_MAX, '\n');

        if (which == 0) return;
        if (which < 1 || which > local_joined_groups.size()) {
            cout << "[错误] 无效的序号。" << endl;
            return;
        }

        Group& selected_group = local_joined_groups[which - 1];

        cout << "请输入你要拉入群聊的好友名字 (输入0返回): ";
        string friend_username;
        getline(cin, friend_username);

        if (friend_username == "0" || friend_username.empty()) {
            cout << "已取消邀请。" << endl;
            return;
        }

        nlohmann::json invite_req;
        invite_req["flag"] = C2S_INVITE_TO_GROUP_REQUEST;
        invite_req["data"]["group_uid"] = selected_group.getGroupUid();
        invite_req["data"]["friend_username"] = friend_username;
        sendMsg(fd, invite_req.dump());

        string invite_res_str;
        if (recvMsg(fd, invite_res_str) > 0) {
            auto invite_res = nlohmann::json::parse(invite_res_str);
            cout << "[系统提示] " << invite_res["data"].value("reason", "") << endl;
        } else {
            cout << "[错误] 未收到服务器确认" << endl;
        }

    } catch (const nlohmann::json::parse_error& e) {
        cout << "[错误] 解析服务器响应失败: " << e.what() << endl;
    }
    cout << "======================================\n" << endl;
}