
#include <nlohmann/json.hpp>

#include <utility>
#include <random>
#include <iostream>
#include "Group.h"

using namespace std;

Group::Group() {
    groupName = "未知群聊";
    UID = "";
    groupUID = "";
    members = "";
    admins = "";
}

Group::Group(string groupName, string UID) : groupName(std::move(groupName)), UID(std::move(UID)) {
    random_device rd;
    mt19937 eng(rd());
    uniform_int_distribution<> dist(10, 99);
    int random_num = dist(eng);

    time_t timer;
    time(&timer);
    //cout<<"timer: "<<timer<<endl;
    string timeStamp = to_string(timer).substr(8, 2);
    //cout<<"timeStamp: "<<timeStamp<<endl;
    groupUID = to_string(random_num).append(timeStamp);
    admins = groupUID + "admin";
    members = groupUID + "member";
}

std::string Group::to_json() {
    nlohmann::json root;
    
    root["groupName"] = groupName;
    root["UID"] = UID;
    root["groupUID"] = groupUID;
    root["members"] = members;
    root["admins"] = admins;
    return root.dump();

}

void Group::json_parse(const std::string &json) {
    try {
        nlohmann::json root = nlohmann::json::parse(json);

        // 安全地获取字符串字段
        if (root.contains("groupName") && root["groupName"].is_string()) {
            groupName = root["groupName"].get<std::string>();
        }
        if (root.contains("UID") && root["UID"].is_string()) {
            UID = root["UID"].get<std::string>();
        }
        if (root.contains("groupUID") && root["groupUID"].is_string()) {
            groupUID = root["groupUID"].get<std::string>();
        }
        if (root.contains("members") && root["members"].is_string()) {
            members = root["members"].get<std::string>();
        }
        if (root.contains("admins") && root["admins"].is_string()) {
            admins = root["admins"].get<std::string>();
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Group JSON解析失败: " << e.what() << std::endl;
        std::cerr << "[ERROR] JSON内容: " << json << std::endl;
        // 设置默认值
        groupName = "未知群聊";
        UID = "";
        groupUID = "";
        members = "";
        admins = "";
    }
}

const string &Group::getGroupName() const {
    return groupName;
}

const string &Group::getGroupUid() const {
    return groupUID;
}

const string &Group::getMembers() const {
    return members;
}

const string &Group::getAdmins() const {
    return admins;
}

const std::string &Group::getOwnerUid() const {
    return UID;
}


