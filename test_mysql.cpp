#include "server/MySQL.h"
#include <iostream>
using namespace std;

int main() {
    cout << "=== MySQL连接测试 ===" << endl;
    
    MySQL mysql;
    if (!mysql.connect()) {
        cout << "❌ MySQL连接失败" << endl;
        return 1;
    }
    
    cout << "✅ MySQL连接成功" << endl;
    
    // 测试插入消息
    cout << "\n=== 测试插入消息 ===" << endl;
    bool result = mysql.insertPrivateMessage("test_user1", "test_user2", "Hello, this is a test message!");
    
    if (result) {
        cout << "✅ 消息插入成功" << endl;
    } else {
        cout << "❌ 消息插入失败" << endl;
        return 1;
    }
    
    // 测试获取历史消息
    cout << "\n=== 测试获取历史消息 ===" << endl;
    vector<string> messages = mysql.getPrivateHistory("test_user1", "test_user2", 10);
    
    cout << "获取到 " << messages.size() << " 条消息:" << endl;
    for (const string& msg : messages) {
        cout << "  - " << msg << endl;
    }
    
    cout << "\n=== 测试完成 ===" << endl;
    return 0;
}
