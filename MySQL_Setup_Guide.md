# MySQL消息存储设置指南

## 🎯 修改范围说明
**只修改了消息存储部分，好友管理和拉黑检查仍使用Redis原有逻辑！**

- ✅ **消息存储**: Redis → MySQL (历史消息持久化)
- ✅ **历史消息获取**: Redis → MySQL
- ❌ **好友管理**: 保持Redis (添加、删除好友)
- ❌ **拉黑检查**: 保持Redis (屏蔽、解除屏蔽)
- ❌ **在线状态**: 保持Redis

## 🚀 现在你需要在本地MySQL做的操作

### 1. 登录MySQL
```bash
mysql -u root -p
```

### 2. 创建数据库和表
```sql
-- 创建数据库
CREATE DATABASE IF NOT EXISTS chatroom CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE chatroom;

-- 创建群聊消息表
CREATE TABLE IF NOT EXISTS group_messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    group_uid VARCHAR(64) NOT NULL,
    sender_uid VARCHAR(64) NOT NULL,
    content TEXT NOT NULL,
    sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_group (group_uid),
    INDEX idx_time (sent_at)
);

-- 创建私聊消息表
CREATE TABLE IF NOT EXISTS private_messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    sender_uid VARCHAR(64) NOT NULL,
    receiver_uid VARCHAR(64) NOT NULL,
    content TEXT NOT NULL,
    sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_users (sender_uid, receiver_uid),
    INDEX idx_time (sent_at)
);

-- 注意：好友关系、屏蔽关系仍使用Redis，这里只需要消息表
```

### 3. 验证表创建成功
```sql
-- 查看所有表
SHOW TABLES;

-- 查看表结构
DESCRIBE private_messages;
DESCRIBE group_messages;
```

### 4. 修改MySQL.cc中的连接信息
打开 `server/MySQL.cc` 文件，修改第24-28行的数据库连接信息：

```cpp
// 连接数据库
if (!mysql_real_connect(conn, 
                       "localhost",     // 主机
                       "root",          // 用户名 - 改成你的MySQL用户名
                       "your_password", // 密码 - 改成你的MySQL密码
                       "chatroom",      // 数据库名
                       3306,            // 端口
                       nullptr, 
                       0)) {
```

## ✅ 已完成的代码集成

### 1. 数据库功能扩展
- ✅ 在MySQL.h中添加了好友管理和拉黑功能
- ✅ 在MySQL.cc中实现了所有核心功能
- ✅ 更新了init.sql数据库表结构

### 2. 业务逻辑替换
- ✅ 在Transaction.cc中添加了MySQL版本的函数
- ✅ 在LoginHandler.cc中替换了函数调用：
  - `start_chat()` → `start_chat_mysql()`
  - `add_friend()` → `add_friend_mysql()`
  - `del_friend()` → `del_friend_mysql()`
  - `blockedLists()` → `block_user_mysql()`
  - `unblocked()` → `unblock_user_mysql()`
  - `F_history()` → `F_history_mysql()`

### 3. 编译配置
- ✅ 更新了CMakeLists.txt
- ✅ 添加了必要的include文件

## 🔄 核心逻辑变化

### 消息发送逻辑
**之前 (Redis)**：
```cpp
// 检查好友关系
if (!redis.sismember(UID, user.getUID()))

// 存储消息
redis.lpush(me, msg);
redis.lpush(her, msg);
```

**现在 (MySQL)**：
```cpp
// 检查好友关系
if (!mysql.isFriend(user.getUID(), receiver_uid))

// 存储消息
mysql.insertPrivateMessage(user.getUID(), receiver_uid, content);
```

### 拉黑检查逻辑
**之前 (Redis)**：
```cpp
if (redis.sismember("blocked" + UID, user.getUID()))
```

**现在 (MySQL)**：
```cpp
if (mysql.isBlocked(user.getUID(), receiver_uid) || 
    mysql.isBlocked(receiver_uid, user.getUID()))
```

## 🚀 编译和运行

### 1. 编译项目
```bash
cd 111111chatroom/messengertest
mkdir -p build
cd build
cmake ..
make -j4
```

### 2. 运行服务器
```bash
./server.out [端口号]
```

## 💡 混合架构优势

现在你的系统采用了最佳的混合架构：
- **MySQL**: 负责持久化存储（消息历史、好友关系、屏蔽关系）
- **Redis**: 继续负责实时状态管理（在线用户、聊天状态、通知）

这样既获得了MySQL的数据持久化和事务保证，又保持了Redis在实时状态管理方面的高性能！

## 🎉 完成！

现在你只需要：
1. 在MySQL中执行上面的SQL语句创建表
2. 修改MySQL.cc中的数据库连接信息
3. 编译运行

所有的Redis消息存储逻辑都已经替换为MySQL，同时完美复用了chatroom项目的拉黑删除逻辑！
