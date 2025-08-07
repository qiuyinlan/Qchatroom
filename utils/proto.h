
#ifndef CHATROOM_PROTO_H
#define CHATROOM_PROTO_H
#include <string>

using std::string;

//文件
const string SENDFILE_F = "SENDFILE_F";
const string RECVFILE_F = "RECVFILE_F";
const string SENDFILE_G = "SENDFILE_G";
const string RECVFILE_G = "RECVFILE_G";

const string REMOVE = "REMOVE"; // 踢出群聊

const string LOGIN = "1";
const string NOTIFY = "3";
const string UNIFIED_RECEIVER = "25";  // 统一接收线程协议
//客户端子线程进行实时通知，私聊，群聊
//transaction
const string START_CHAT = "4";
const string LIST_FRIENDS = "6";
const string ADD_FRIEND = "7";
const string FIND_REQUEST = "8";
const string DEL_FRIEND = "9";
const string BLOCKED_LISTS = "10";
const string UNBLOCKED = "11";
const string GROUP = "12";

const string REQUEST_NOTIFICATION = "15";
const string GROUP_REQUEST = "16";
const string SYNC = "17";
const string EXIT = "18";
const string BACK = "19";
const string SYNCGL = "20";  // 获取群聊列表
const string GROUPCHAT = "100";


const string DEACTIVATE_ACCOUNT = "23";  // 注销账户
#define BLOCKED_MESSAGE "BLOCKED_MESSAGE"
#define FRIEND_VERIFICATION_NEEDED "FRIEND_VERIFICATION_NEEDED"
#define MESSAGE_SENT "MESSAGE_SENT"
#define DEACTIVATED_MESSAGE "DEACTIVATED_MESSAGE"
/*\033[30m：黑色
\033[31m：红色
\033[32m：绿色
\033[33m：黄色
\033[34m：蓝色
\033[35m：洋红色
\033[36m：青色
\033[37m：白色*/
// 颜色代码
const std::string RESET = "\033[0m";
const std::string BLACK = "\033[30m";
const std::string RED = "\033[31m";
const std::string EXCLAMATION = "\033[1;31m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";

class LoginRequest {
public:
    LoginRequest();

    LoginRequest(string email, string passwd);

    [[nodiscard]] const string &getEmail() const;

    void setEmail(const string &email);

    [[nodiscard]] const string &getPassword() const;

    void setPassword(const string &password);

    string to_json();

    void json_parse(const string &json);

private:
    string email;
    string password;
};

class Message {
public:
    Message();

    Message(string username, string UID_from, string UID_to, string groupName = "1");
     //名字
    [[nodiscard]] string getUsername() const;

    void setUsername(const string &name);

    [[nodiscard]] const string &getUidFrom() const;

    //来源uid
    void setUidFrom(const string &uidFrom);

    [[nodiscard]] const string &getUidTo() const;

    //去uid
    void setUidTo(const string &uidTo);

    [[nodiscard]] string getContent() const;

    //设置内容
    void setContent(const string &msg);

    [[nodiscard]] const string &getGroupName() const;

    //群聊名字
    void setGroupName(const string &groupName);

    [[nodiscard]] string getTime() const;

   //如果是群聊，那to就是群聊名

  
    //设置时间
    void setTime(const string &t);

    static string get_time();

    string to_json();

    void json_parse(const string &json);

private:
    string timeStamp;
    string username;
    string UID_from;
    string UID_to;
    //实际聊天内容
    string content;
    string group_name;
};

//服务器消息包
struct Response {
    int status;
    string prompt;
};

// 邮箱验证码相关协议
inline const std::string REQUEST_CODE = "20"; // 请求验证码
inline const std::string REGISTER_WITH_CODE = "21"; // 验证码注册
inline const std::string REQUEST_RESET_CODE = "22"; // 找回密码请求验证码
inline const std::string RESET_PASSWORD_WITH_CODE = "23"; // 验证码重置密码
inline const std::string FIND_PASSWORD_WITH_CODE = "24"; // 邮箱+验证码找回密码

#endif

