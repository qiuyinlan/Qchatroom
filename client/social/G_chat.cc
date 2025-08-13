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
    sendMsg(fd, GROUP);
    vector<Group> joinedGroup;
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
    int ret = sendMsg(fd, SYNCGL);
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
    sendMsg(fd, "1");
    string groupName;
     string reply;
    while (true) {
        cout << "你要创建的群聊名称是什么" << endl;
        return_last();
        getline(cin, groupName);
        if(groupName.empty()){
            cout << "群聊名称不能为空" << endl;
            continue;
        }
        if(groupName == "0"){
            sendMsg(fd,BACK);
            return;
        }
        if (cin.eof()) {
            cout << "读到文件结尾" << endl;
            sendMsg(fd,BACK);
            return;
        }
        if (groupName.find(' ') != string::npos) {
            cout << "群聊名称不能出现空格" << endl;
            continue;
        }
        //发名字
        sendMsg(fd,groupName);
        recvMsg(fd,reply);
        if(reply == "已存在"){
            cout << "该群名已存在，请重新输入" << endl;
            continue;
        }else{
             break;
        }
    }
   
    
    Group group(groupName, user.getUID());

    sendMsg(fd, group.to_json());
    cout << "创建成功，可让其他用户搜索群聊名称加入" << endl;
    string temp;
    cout << "按enter返回" << endl;
    getline(cin, temp);
    if (cin.eof()) {
        cout << "读到文件结尾" << endl;
        return;
    }
}

void G_chat::joinGroup() const {
   
    sendMsg(fd, "2");
    string group_name;
    while (true) {
        cout << "输入你要加入的群聊名称" << endl;
        return_last();
        getline(cin, group_name);
        if (cin.eof()) {
            cout << "文件读到结尾" << endl;
            sendMsg(fd,BACK);
            return;
        }
        if (group_name == "0") {
            sendMsg(fd,BACK);
            return;
        }   
        if (group_name.empty()) {
            cout << "群聊名称不能为空" << endl;
            continue;
        }
        if (group_name.find(' ') != string::npos) {
            cout << "群聊名称不能包含空格" << endl;
            continue;
        }
        break;
    }
    sendMsg(fd, group_name);
    string response;

    recvMsg(fd, response);
    if (response == "-1") {
        cout << "该群不存在" << endl;
    } else if (response == "-2") {
        cout << "你已经是该群成员" << endl;
    }else if (response == "-3"){
        cout << "你已经向该群发送了入群请求,等待同意" << endl;
    }else{
        cout << "请求已经发出，等待同意" << endl;
    }
    string temp;
    cout << "按enter返回" << endl;
    getline(cin, temp);
    if (cin.eof()) {
        cout << "读到文件结尾" << endl;
        sendMsg(fd,BACK);
        return;
    }
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

    sendMsg(fd, "3");
    string temp;
    if (managedGroup.empty()) {
        cout << "你当前还没有可以管理的群... 按enter返回" << endl;
        sendMsg(fd, BACK);
        getline(cin, temp);
        return;
    }
    cout << "-----------------------------------" << endl;
    cout << "你管理的群及你的身份" << endl;
    // 打印群聊列表。群主&&管理权限
    //引入role,只需要判断，打印role即可，统一形式，不需要额外打印管理和群主
    for (int i = 0; i < managedGroup.size(); i++) {
        const Group& group = managedGroup[i];  // 获取当前群聊对象
        string groupName = group.getGroupName();
        string role;  // 用于存储身份标识（群主/管理员）
    
        if (group.getOwnerUid() == user.getUID()) {
            role = "(群主)";
        }
        else {
                role = "(管理员)";
            }
    
    
        // 打印群聊信息（带身份标识）
        cout << i + 1 << ". " << groupName << role << endl;
    }
    
    
    
    cout << "-----------------------------------" << endl;
    cout << "选择你要管理的群" << endl;
    return_last();
    int which;
    //选择群聊
    while (!(cin >> which) || which < 0 || which > managedGroup.size()) { //输入失败的时候
        if (cin.eof()) {
            //如果退出仍然需要输入，那一定要加clear
            cin.clear(); 
            cout << "读到文件结尾" << endl;
            return;
        }
        cout << "输入格式错误，请输入1-" << managedGroup.size() << "之间的数字" << endl;
        cin.clear(); //输入失败，需要重置状态
        cin.ignore(INT32_MAX, '\n');
    }
    //输入成功
    cin.ignore(INT32_MAX, '\n');
    if (which == 0) {
        sendMsg(fd, BACK);
        return;
    }
    
    which--;

    sendMsg(fd, managedGroup[which].to_json());
    //选择操作
    //如果是群主
    if ( managedGroup[which].getOwnerUid() == user.getUID()) {
        while (true) {
            ownerMenu();
            int choice;
           while (true) {
                // 先尝试读取整数
                if (cin >> choice) {
                    // 如果在允许范围内 → 跳出
                    if (choice == 0 || choice >0 || choice < 6) {
                        cin.ignore(INT32_MAX, '\n'); // 清掉这一行剩余的输入
                        break;
                    } else {
                        cout << "输入格式错误" << endl;
                    }
                } 
                else {
                    // 读取失败
                    if (cin.eof()) {
                        cout << "读到文件结尾" << endl;
                        sendMsg(fd, BACK);
                        return;
                    }
                    cout << "输入格式错误" << endl;
                    cin.clear(); // 清理 fail 状态
                }

                // 无论是范围错误还是解析失败，都清掉剩余输入
                cin.ignore(INT32_MAX, '\n');
            }

            if (choice == 0) {
                sendMsg(fd, BACK);
                break;
            } else if (choice == 1) {
                approve();
                break;
            } else if (choice == 2) {
                remove(managedGroup[which]);
                break;
            } else if (choice == 3) {
                appointAdmin(managedGroup[which]);
                break;
            } else if (choice == 4) {
                revokeAdmin(managedGroup[which]);
                break;
            } else if (choice == 5) {
                deleteGroup(managedGroup[which]);
                break;
            } 
            
        }



    }
    else {
        while (true) {
            managedMenu();
            int choice;
            while (true) { // 先尝试读取整数
                if (cin >> choice) {
                    // 如果在允许范围内 → 跳出
                    if (choice == 0 || choice == 1 || choice == 2) {
                        cin.ignore(INT32_MAX, '\n'); // 清掉这一行剩余的输入
                        break;
                    } else {
                        cout << "输入格式错误" << endl;
                    }
                } 
                else {
                    // 读取失败
                    if (cin.eof()) {
                        cout << "读到文件结尾" << endl;
                        sendMsg(fd, BACK);
                        return;
                    }
                    cout << "输入格式错误" << endl;
                    cin.clear(); // 清理 fail 状态
                }

                // 无论是范围错误还是解析失败，都清掉剩余输入
                cin.ignore(INT32_MAX, '\n');
            }

            if (choice == 0) {
    
                sendMsg(fd, BACK);
                break;
            }
            if (choice == 1) {
                approve();
                break;
            } else if (choice == 2) {
                remove(managedGroup[which]);
                break;
            }
    
        }
    }
    
}

void G_chat::approve() const {
    sendMsg(fd, "1");
    string nums;

    recvMsg(fd, nums);
    int num = stoi(nums);
    if (num == 0) {
        cout << "暂无入群申请" << endl;
        return;
    }
    string buf;
    for (int i = 0; i < num; i++) {

        recvMsg(fd, buf);
        cout << "收到" << buf << "的入群申请" << endl;
        cout << "[y]YES,[n]NO" << endl;
        string choice;
        while (!(cin >> choice) || (choice != "y" && choice != "n")) {
            cout << "输入格式错误，请输入y或n" << endl;
            cin.clear();
        }

        sendMsg(fd, choice);
        if (choice == "y") {
            cout << "添加成功" << endl;
        } else {
            cout << "添加失败" << endl;
        }
    }
    cout << "按enter返回" << endl;
    getline(cin, buf);
    if (cin.eof()) {
        cout << "读到文件结尾" << endl;
        return;
    }
}

void G_chat::remove(Group &group) const {
    sendMsg(fd, "2");
    string buf;
    User member;
    vector<User> arr;
    //接收服务器发送的群员数量
    recvMsg(fd, buf);
    int num = stoi(buf);

    cout << YELLOW<< "-----------------------------------" << RESET << endl;
    for (int i = 0; i < num; i++) {
        //接收服务器发送的群员信息
        recvMsg(fd, buf);
        member.json_parse(buf);
        arr.push_back(member);
        if (member.getUID() == group.getOwnerUid()) {
            cout << i + 1 << "." << member.getUsername() << "(群主)" << endl;
        } else {
            cout << i + 1 << "." << member.getUsername() << endl;
        }
    }

    cout << YELLOW<< "-----------------------------------" << RESET << endl;
    cout << "tip:不能踢群主和自己哦" << endl;


    while (true) {
        cout << "你要踢谁" << endl;
        return_last();
        int who;
        while (!(cin >> who) || who < 0 || who > (int)arr.size()) {
            if (cin.eof()) {
                cout << "读到文件结尾" << endl;
                return;
            }
            cout << "输入格式错误，请输入0-" << arr.size() << "之间的数字" << endl;
            cin.clear();
            cin.ignore(INT32_MAX, '\n');
        }
        cin.ignore(INT32_MAX, '\n');
        if (who == 0) {
            sendMsg(fd, "0");
            return;
        }
        who--;
        if (arr[who].getUID() == group.getOwnerUid()) {
            cout << "该用户是群主，你不能踢！" << endl;
            continue;
        }
        if (arr[who].getUID() == user.getUID()) {
            cout << "你不能踢自己哦" << endl;
            continue;
        }
        sendMsg(fd, arr[who].to_json());

        cout << "删除成功，按enter返回" << endl;
        getline(cin, buf);
        if (cin.eof()) {
            cout << "读到文件结尾" << endl;
            return;
        }
        break;
    }
}


void G_chat::appointAdmin(Group &createdGroup) const {
    sendMsg(fd, "3");
    vector<User> arr;
    string nums;
    User member;

    recvMsg(fd, nums);
    int num = stoi(nums);
    string member_info;

    cout << YELLOW << "-----------------------------------" << RESET << endl;
    for (int i = 0; i < num; i++) {

        recvMsg(fd, member_info);
        member.json_parse(member_info);
        arr.push_back(member);
        if (member.getUID() == createdGroup.getOwnerUid()) {
            cout << i + 1 << ". " << member.getUsername() << "群主" << endl;
        } else {
            cout << i + 1 << ". " << member.getUsername() << endl;
        }
    }

    cout << YELLOW << "-----------------------------------" << RESET << endl;
    int who;
    while (true) {
        cout << "你要任命谁为管理员" << endl;
        while (!(cin >> who) || who < 0 || who > arr.size()) {
            if (cin.eof()) {
                cout << "读到文件结尾" << endl;
                return;
            }
            cin.clear();
            cin.ignore(INT32_MAX, '\n');
        }
        cin.ignore(INT32_MAX, '\n');
        who--;
        if (arr[who].getUID() == createdGroup.getOwnerUid()) {
            cout << "该用户为群主" << endl;
            continue;
        }
        break;
    }

    sendMsg(fd, arr[who].to_json());
    string reply;

    recvMsg(fd, reply);
    if (reply == "-1") {
        cout << "该用户为管理员，无法多次设置" << endl;
        return;
    }
    cout << "按enter返回" << endl;
    string temp;
    getline(cin, temp);
    if (cin.eof()) {
        cout << "读到文件结尾" << endl;
        return;
    }
}

void G_chat::revokeAdmin(Group &createdGroup) const {
    sendMsg(fd, "4");
    string nums;

    recvMsg(fd, nums);
    int num = stoi(nums);
    string admin_info;
    User admin;
    vector<User> arr;
    for (int i = 0; i < num; i++) {

        recvMsg(fd, admin_info);
        admin.json_parse(admin_info);
        arr.push_back(admin);
        if (admin.getUID() == createdGroup.getOwnerUid()) {
            cout << i + 1 << "." << admin.getUsername() << "(群主)" << endl;
        } else {
            cout << i + 1 << "." << admin.getUsername() << endl;
        }
    }
    int who;
    while (true) {
        cout << "选择你要取消的人,tip：不能取消群主你自己的管理权限哦" << endl;
        while (!(cin >> who) || who < 0 || who > arr.size()) {
            if (cin.eof()) {
                cout << "读到文件结尾" << endl;
                return;
            }
            cin.clear();
            cin.ignore(INT32_MAX, '\n');
        }
        cin.ignore(INT32_MAX, '\n');
        who--;
        if (arr[who].getUID() == createdGroup.getOwnerUid()) {
            cout << "不能取消群主的管理权限" << endl;
            continue;
        }
        break;
    }

    sendMsg(fd, arr[who].to_json());
    cout << "撤销成功，按enter返回" << endl;
    string temp;
    getline(cin, temp);
    if (cin.eof()) {
        cout << "读到文件结尾" << endl;
        return;
    }
}

void G_chat::deleteGroup(Group &createdGroup) const {
    sendMsg(fd, "5");
    cout << "解散成功，按enter返回" << endl;
    string temp;
    getline(cin, temp);
    if (cin.eof()) {
        cout << "读到文件结尾" << endl;
        return;
    }
}

void G_chat::showMembers(std::vector<Group> &group) {
    sendMsg(fd, "4");
    //传入的是加入的群的数组——joinedgroup
    string temp;
    if (group.empty()) {
        cout << "你当前还没有加入任何群聊... 按enter返回" << endl;
        sendMsg(fd,BACK);
        getline(cin, temp);
        return;
    }
    cout << user.getUsername() << endl;
    cout << "--------------------------" << endl;
    for (int i = 0; i < group.size(); i++) {
        cout << i + 1 << ". " << group[i].getGroupName() << endl;
    }
    cout << "--------------------------" << endl;
    cout << "你要查看哪个群" << endl;
    return_last();
    int which;
    while (!(cin >> which) || which < 0 || which > group.size()) {
        if (cin.eof()) {
            cout << "读到文件结尾" << endl;
            return;
        }
        cout << "输入格式错误" << endl;
        cin.clear();
        cin.ignore(INT32_MAX, '\n');
    }
    cin.ignore(INT32_MAX, '\n');

    if (which == 0) {
        sendMsg(fd, BACK);
        return;  // 用户选择退出
    }
    which--;
    cout << "-------------------------------------------" << endl;
    cout << "群聊名称：" << group[which].getGroupName() << endl;
    cout << "【成员列表】" << endl;

    sendMsg(fd, group[which].to_json());
    string nums;

    //收
    recvMsg(fd, nums);
    int num = stoi(nums);
    string member_info;
    for (int i = 0; i < num; i++) {

        recvMsg(fd, member_info);
        cout << i + 1 << ". " << member_info << endl;
    }
    cout << "-------------------------------------------" << endl;
    cout << "按enter返回" << endl;
    if (cin.eof()) {
        cout << "读到文件结尾" << endl;
        return;
    }
}

void G_chat::quit(vector<Group> &joinedGroup) {

    sendMsg(fd, "5");
    string temp;
    if (joinedGroup.empty()) {
        cout << "你当前没有加入任何群聊" << endl;
        cout << "按enter返回" << endl;
        getline(cin, temp);
        if (cin.eof()) {
            return;
        }
        return;
    }
    cout << "-------------------------------------------" << endl;
    cout << "你加入的群及你的身份" << endl;
    for (int i = 0; i < joinedGroup.size(); i++) {
        if (joinedGroup[i].getOwnerUid() == user.getUID()) {
            cout << i + 1 << ". " << joinedGroup[i].getGroupName() << "(群主)" << endl;
        } else {
            cout << i + 1 << ". " << joinedGroup[i].getGroupName() << endl;
        }
    }
    cout << "-------------------------------------------" << endl;
    int which;
    while (true) {
        cout << "tip:群主不能退出群聊哦" << endl;
        cout << "输入你要退出的群" << endl;
        return_last();
        while (!(cin >> which) || which < 0 || which > joinedGroup.size()) {
            if (cin.eof()) {
                cout << "读到文件结尾" << endl;
                return;
            }
            cout << "输入格式错误" << endl;
            cin.clear();
            cin.ignore(INT32_MAX, '\n');
        }
        if (which == 0) {
            sendMsg(fd,BACK);
            return;  // 用户选择退出
        }
        cin.ignore(INT32_MAX, '\n');
        which--;
        if (joinedGroup[which].getOwnerUid() == user.getUID()) {
            cout << "你是该群群主，不能退出" << endl;
            continue;
        }
        break;
    }

    sendMsg(fd, joinedGroup[which].to_json());
    cout << "你已退出该群，按enter返回" << endl;
    getline(cin, temp);
    if (cin.eof()) {
        cout << "读到文件结尾" << endl;
        return;
    }
}

 void G_chat::getperson(vector<Group> &group){
    //传入的是加入的群的数组——joinedgroup

    sendMsg(fd,"6");
    sendMsg(fd,user.getUID());
    
    
    string temp;
    if (group.empty()) {
        cout << "你当前还没有加入任何群聊... 按enter返回" << endl;
        sendMsg(fd,"0");
        getline(cin, temp);
        return;
    }
    cout << user.getUsername() << endl;
    cout << "--------------------------" << endl;
    for (int i = 0; i < group.size(); i++) {
        cout << i + 1 << ". " << group[i].getGroupName() << endl;
    }
    cout << "--------------------------" << endl;
    cout << "你要向哪个群拉入你的好友" << endl;
    return_last();
    int which;
    while (!(cin >> which) || which < 0 || which > group.size()) {
        if (cin.eof()) {
            cout << "读到文件结尾" << endl;
            return;
        }
        cout << "输入格式错误" << endl;
        cin.clear();
        cin.ignore(INT32_MAX, '\n');
    }
    cin.ignore(INT32_MAX, '\n');

    if (which == 0) {
        sendMsg(fd, "0");
        return;  // 用户选择退出
    }
    which--;
    cout << "-------------------------------------------" << endl;
    cout << "群聊名称：" << group[which].getGroupName() << endl;

    sendMsg(fd,group[which].to_json());

    ////////////////////////////////////
    
    string name,reply;
    while(true) {
         cout << "请输入你要拉入群聊的好友名字" << endl;
        return_last();
        getline(cin, name);
        if (name == "0") {
            sendMsg(fd,name);
            return;
        }
        sendMsg(fd,name);
        recvMsg(fd,reply);
        
        if (reply == "-1") {
            cout << RED <<"TA不是你的好友（你们可能没加好友或者你默默被他（她）删除了）" << RESET << endl;
            continue;
        }
        else if (reply == "-3") {
            cout << RED <<"该好友已注销，不能拉入群聊" << RESET << endl;
            continue;
        }
        else if (reply == "-2") {
            cout << RED <<"该好友已经在群聊中，不能重复添加" << RESET << endl;
            continue;
        }
        else if (reply == "-4") {
            cout << RED <<"不能拉自己" << RESET << endl;
            continue;
        }
        else if (reply == "1") {
            break;
        }
    }
        cout << GREEN << "添加成功" << RESET << endl;
        cout << "按enter返回" << endl;
        getline(cin, temp);
}