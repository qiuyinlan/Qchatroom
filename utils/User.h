
#ifndef CHATROOM_USER_H
#define CHATROOM_USER_H

#include <string>
//CMakeLists.txt中没有在client.out中加上User.cc, 导致编译client的时候链接错误
using std::string;

class User {

public:
    User();

    static string get_time();

    void setUID(string uid);

    [[nodiscard]] string getUID() const;

    void setEmail(string email);

    [[nodiscard]] string getEmail() const;

    void setPassword(string password);

    void setUsername(string name);

    [[nodiscard]] string getMyTime() const;

    [[nodiscard]] string getUsername() const;

    [[nodiscard]] string getPassword() const;

    void setIsOnline(bool online);

    [[nodiscard]] bool getIsOnline() const;

    string to_json();

    
    void json_parse(const string &json);

private:
    string UID;    //由注册时间关联，10位
    string email;  //用户邮箱，用于辅助查找
    string username; //用户名
    string password; //密码
    string my_time;  //该用户创建时间
    bool is_online;//是否咋在线;false代表不在线，非0代表在fd
};

#endif //CHATROOM_USER_H
