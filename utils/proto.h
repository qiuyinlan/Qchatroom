#ifndef CHATROOM_PROTO_H
#define CHATROOM_PROTO_H

#include <string>

// =================================================================
//         Ⅰ. 新协议 (基于JSON和整数Flag)
// =================================================================

// --- 客户端到服务器的请求 ---
#define C2S_HEARTBEAT 1001
#define C2S_LOGIN_REQUEST 1101
#define C2S_GET_CHAT_LISTS 1201           // 获取聊天列表 (好友+群组)
#define C2S_LOGOUT_REQUEST 1301             // 退出登录请求
#define C2S_DEACTIVATE_REQUEST 1302         // 注销账户请求
#define C2S_START_CHAT_REQUEST 1401           // 开始私聊请求
#define C2S_START_GROUP_CHAT_REQUEST 1501     // 开始群聊请求
#define C2S_PRIVATE_MESSAGE 1601              // 发送私聊消息
#define C2S_GROUP_MESSAGE 1701                // 发送群聊消息
#define C2S_EXIT_CHAT_REQUEST 1801              // 退出聊天请求
#define C2S_ADD_FRIEND_REQUEST 1901             // 添加好友请求
#define C2S_GET_FRIEND_REQUESTS 2001          // 获取好友请求列表
#define C2S_RESPOND_TO_FRIEND_REQUEST 2101    // 响应好友请求
#define C2S_DELETE_FRIEND_REQUEST 2201        // 删除好友请求
#define C2S_BLOCK_FRIEND_REQUEST 2301         // 屏蔽好友请求
#define C2S_GET_BLOCKED_LIST_REQUEST 2401     // 获取屏蔽列表请求
#define C2S_UNBLOCK_FRIEND_REQUEST 2501       // 解除屏蔽请求
#define C2S_DEACTIVATE_ACCOUNT_REQUEST 2601   // 注销账户请求

// 群聊功能
#define C2S_CREATE_GROUP_REQUEST 2701         // 创建群聊请求
#define C2S_JOIN_GROUP_REQUEST 2801           // 加入群聊请求
#define C2S_GET_MANAGED_GROUPS_REQUEST 2901   // 获取可管理的群聊列表
#define C2S_GET_GROUP_JOIN_REQUESTS 3001    // 获取群聊入群申请列表
#define C2S_RESPOND_TO_GROUP_JOIN_REQUEST 3101// 响应入群申请
#define C2S_GET_GROUP_MEMBERS_REQUEST 3201    // 获取群成员列表
#define C2S_KICK_GROUP_MEMBER_REQUEST 3301    // 踢出群成员
#define C2S_APPOINT_ADMIN_REQUEST 3401        // 设置管理员
#define C2S_GET_GROUP_ADMINS_REQUEST 3501     // 获取群管理员列表
#define C2S_REVOKE_ADMIN_REQUEST 3601         // 撤销管理员
#define C2S_DELETE_GROUP_REQUEST 3701         // 解散群聊
#define C2S_GET_JOINED_GROUPS_REQUEST 3801    // 获取已加入的群聊列表
#define C2S_QUIT_GROUP_REQUEST 3901           // 退出群聊
#define C2S_INVITE_TO_GROUP_REQUEST 4001      // 邀请好友入群

// 历史记录
#define C2S_GET_HISTORY_REQUEST 4101          // 获取历史记录请求

// 文件传输
#define C2S_SEND_FILE_REQUEST 4201            // 发送文件请求
#define C2S_RECV_FILE_REQUEST 4301            // 接收文件请求
#define C2S_GET_GROUP_JOIN_REQUESTS 3001    // 获取群聊入群申请列表
#define C2S_RESPOND_TO_GROUP_JOIN_REQUEST 3101// 响应入群申请

// 文件传输协议
#define C2S_FILE_TRANSFER_REQUEST 1901          // 客户端发起文件传输请求 (metadata)
#define C2S_FILE_CHUNK 1902                     // 客户端发送文件数据块
#define C2S_FILE_TRANSFER_END 1903              // 客户端发送完成

// --- 服务器到客户端的响应 ---
#define S2C_LOGIN_SUCCESS 2101
#define S2C_LOGIN_FAILURE 2102
// ... 其他新协议定义 ...
#define S2C_CHAT_LISTS_RESPONSE 2301      // 聊天列表响应
#define S2C_START_CHAT_RESPONSE 2401        // 开始私聊响应 (包含历史消息)
#define S2C_START_GROUP_CHAT_RESPONSE 2501  // 开始群聊响应 (包含历史消息)
#define S2C_PRIVATE_MESSAGE 2601              // 收到私聊消息
#define S2C_GROUP_MESSAGE 2701                // 收到群聊消息
#define S2C_ADD_FRIEND_RESPONSE 2801            // 添加好友响应
#define S2C_FRIEND_REQUESTS_RESPONSE 2901     // 服务器发送好友请求列表
#define S2C_RESPOND_TO_FRIEND_RESPONSE 3001   // 服务器确认好友请求响应
#define S2C_DELETE_FRIEND_RESPONSE 3101       // 删除好友响应
#define S2C_BLOCK_FRIEND_RESPONSE 3201        // 屏蔽好友响应
#define S2C_BLOCKED_LIST_RESPONSE 3301      // 屏蔽列表响应
#define S2C_UNBLOCK_FRIEND_RESPONSE 3401      // 解除屏蔽响应
#define S2C_DEACTIVATE_ACCOUNT_RESPONSE 3501  // 注销账户响应

// 群聊功能
#define S2C_CREATE_GROUP_RESPONSE 3601        // 创建群聊响应
#define S2C_JOIN_GROUP_RESPONSE 3701          // 加入群聊响应
#define S2C_MANAGED_GROUPS_RESPONSE 3801      // 可管理的群聊列表响应
#define S2C_GROUP_JOIN_REQUESTS_RESPONSE 3901 // 入群申请列表响应
#define S2C_RESPOND_TO_GROUP_JOIN_RESPONSE 4001// 响应入群申请的响应
#define S2C_GROUP_MEMBERS_RESPONSE 4101       // 群成员列表响应
#define S2C_KICK_GROUP_MEMBER_RESPONSE 4201   // 踢出群成员响应
#define S2C_APPOINT_ADMIN_RESPONSE 4301       // 设置管理员响应
#define S2C_GROUP_ADMINS_RESPONSE 4401        // 群管理员列表响应
#define S2C_REVOKE_ADMIN_RESPONSE 4501        // 撤销管理员响应
#define S2C_DELETE_GROUP_RESPONSE 4601        // 解散群聊响应
#define S2C_JOINED_GROUPS_RESPONSE 4701       // 已加入的群聊列表响应
#define S2C_QUIT_GROUP_RESPONSE 4801          // 退出群聊响应
#define S2C_INVITE_TO_GROUP_RESPONSE 4901     // 邀请好友入群响应

// 历史记录
#define S2C_HISTORY_RESPONSE 5001             // 历史记录响应

// 文件传输
#define S2C_SEND_FILE_RESPONSE 5101           // 发送文件响应
#define S2C_RECV_FILE_RESPONSE 5201           // 接收文件响应
#define S2C_FILE_NOTIFICATION 5301            // 新文件通知
#define S2C_FRIEND_STATUS_CHANGE 5302        // 好友上下线通知
#define S2C_GROUP_JOIN_REQUESTS_RESPONSE 3901 // 入群申请列表响应
#define S2C_RESPOND_TO_GROUP_JOIN_RESPONSE 4001// 响应入群申请的响应

// 文件传输协议
#define S2C_FILE_TRANSFER_NOTIFY 2901           // 服务器通知接收方
#define S2C_FILE_CHUNK 2902                     // 服务器转发文件数据块
#define S2C_FILE_TRANSFER_END 2903              // 服务器通知传输完成


// =================================================================
//         Ⅱ. 旧协议 (基于字符串) - [已弃用]
// =================================================================
// 注: 这些定义仅为了在重构过程中保持兼容性
// 同步所有群组列表 (创建、管理、加入)
#define C2S_SYNC_GROUPS_REQUEST 4300
#define S2C_SYNC_GROUPS_RESPONSE 4301

// 待功能完全迁移后, 应将其删除

const std::string LOGIN = "1";
const std::string NOTIFY = "3";
const std::string UNIFIED_RECEIVER = "25";
const std::string START_CHAT = "4";
const std::string LIST_FRIENDS = "6";
const std::string ADD_FRIEND = "7";
const std::string FIND_REQUEST = "8";
const std::string DEL_FRIEND = "9";
const std::string BLOCKED_LISTS = "10";
const std::string UNBLOCKED = "11";
const std::string GROUP = "12";
const std::string REQUEST_NOTIFICATION = "15";
const std::string GROUP_REQUEST = "16";
const std::string SYNC = "17";
const std::string EXIT = "18";
const std::string BACK = "19";
const std::string SYNCGL = "20";
const std::string GROUPCHAT = "100";
const std::string DEACTIVATE_ACCOUNT = "23";
const std::string REMOVE = "REMOVE";
const std::string SENDFILE_F = "SENDFILE_F";
const std::string RECVFILE_F = "RECVFILE_F";
const std::string SENDFILE_G = "SENDFILE_G";
const std::string RECVFILE_G = "RECVFILE_G";
const std::string REQUEST_CODE = "20";
const std::string REGISTER_WITH_CODE = "21";
const std::string REQUEST_RESET_CODE = "22";
const std::string RESET_PASSWORD_WITH_CODE = "23";
const std::string FIND_PASSWORD_WITH_CODE = "24";


// =================================================================
//         Ⅲ. 数据结构类
// =================================================================

class LoginRequest {
public:
    LoginRequest();
    LoginRequest(std::string email, std::string passwd);
    [[nodiscard]] const std::string &getEmail() const;
    void setEmail(const std::string &email);
    [[nodiscard]] const std::string &getPassword() const;
    void setPassword(const std::string &password);
    std::string to_json();
    void json_parse(const std::string &json);
private:
    std::string email;
    std::string password;
};

class Message {
public:
    Message();
    Message(std::string username, std::string UID_from, std::string UID_to, std::string groupName = "1");
    [[nodiscard]] std::string getUsername() const;
    void setUsername(const std::string &name);
    [[nodiscard]] const std::string &getUidFrom() const;
    void setUidFrom(const std::string &uidFrom);
    [[nodiscard]] const std::string &getUidTo() const;
    void setUidTo(const std::string &uidTo);
    [[nodiscard]] std::string getContent() const;
    void setContent(const std::string &msg);
    [[nodiscard]] const std::string &getGroupName() const;
    void setGroupName(const std::string &groupName);
    [[nodiscard]] std::string getTime() const;
    void setTime(const std::string &t);
    static std::string get_time();
    std::string to_json();
    void json_parse(const std::string &json);
private:
    std::string timeStamp;
    std::string username;
    std::string UID_from;
    std::string UID_to;
    std::string content;
    std::string group_name;
};

//服务器消息包
struct Response {
    int status;
    std::string prompt;
};
const std::string RESET = "\033[0m";
const std::string BLACK = "\033[30m";
const std::string RED = "\033[31m";
const std::string EXCLAMATION = "\033[1;31m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";


#endif