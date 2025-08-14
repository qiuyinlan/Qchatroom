# MySQL迁移指南

## 概述
本指南说明如何将messengertest项目从Redis存储迁移到MySQL存储，复用chatroom项目的拉黑删除逻辑。

## 已完成的工作

### 1. 扩展MySQL类
- 在`MySQL.h`中添加了好友管理和拉黑功能的方法声明
- 在`MySQL.cc`中实现了以下核心功能：
  - `isFriend()` - 检查好友关系
  - `isBlocked()` - 检查屏蔽关系
  - `addFriend()` - 添加好友关系
  - `deleteFriend()` - 删除好友关系
  - `blockUser()` - 屏蔽用户
  - `unblockUser()` - 解除屏蔽

### 2. 更新数据库表结构
在`init.sql`中添加了必要的表：
```sql
-- 好友关系表
CREATE TABLE friends (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user1 VARCHAR(64) NOT NULL,
    user2 VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY unique_friendship (user1, user2)
);

-- 屏蔽关系表  
CREATE TABLE blocks (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user VARCHAR(64) NOT NULL,
    blocked_user VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY unique_block (user, blocked_user)
);

-- 好友请求表
CREATE TABLE friend_requests (
    id INT AUTO_INCREMENT PRIMARY KEY,
    requester VARCHAR(64) NOT NULL,
    target VARCHAR(64) NOT NULL,
    status ENUM('pending', 'accepted', 'rejected') DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 3. 创建MySQL版本的业务逻辑
- `MySQLTransaction.cc` - 包含MySQL版本的核心业务逻辑
- `MySQLTransaction.h` - 相应的头文件声明

## 核心逻辑对比

### 原Redis逻辑 vs 新MySQL逻辑

#### 消息发送检查
**Redis版本 (Transaction.cc):**
```cpp
// 检查好友关系
if (!redis.sismember(UID, user.getUID())) {
    // 不是好友，拒绝消息
}

// 检查屏蔽关系
if (redis.sismember("blocked" + UID, user.getUID())) {
    // 被屏蔽，特殊处理
}
```

**MySQL版本 (MySQLTransaction.cc):**
```cpp
// 检查好友关系
if (!mysql.isFriend(user.getUID(), receiver_uid)) {
    // 不是好友，拒绝消息
}

// 检查屏蔽关系
if (mysql.isBlocked(user.getUID(), receiver_uid) || 
    mysql.isBlocked(receiver_uid, user.getUID())) {
    // 存在屏蔽关系，特殊处理
}
```

#### 消息存储
**Redis版本:**
```cpp
string me = message.getUidFrom() + message.getUidTo();
string her = message.getUidTo() + message.getUidFrom();
redis.lpush(me, msg);
redis.lpush(her, msg);
```

**MySQL版本:**
```cpp
mysql.insertPrivateMessage(user.getUID(), receiver_uid, message.getContent());
```

## 迁移步骤

### 1. 数据库准备
```bash
# 1. 执行更新后的init.sql创建新表
mysql -u root -p chatroom < init.sql

# 2. 确认表创建成功
mysql -u root -p -e "USE chatroom; SHOW TABLES;"
```

### 2. 代码集成
在你的主要业务逻辑文件中，替换对应的函数调用：

**原来的调用:**
```cpp
start_chat(fd, user);        // Redis版本
F_history(fd, user);         // Redis版本
add_friend(fd, user);        // Redis版本
```

**替换为:**
```cpp
start_chat_mysql(fd, user);  // MySQL版本
F_history_mysql(fd, user);   // MySQL版本
add_friend_mysql(fd, user);  // MySQL版本
```

### 3. 编译配置
在CMakeLists.txt中添加新文件：
```cmake
add_executable(server 
    server.cc
    MySQL.cc
    MySQLTransaction.cc  # 新增
    # ... 其他文件
)
```

### 4. 混合使用策略
建议采用混合策略：
- **MySQL**: 用于持久化存储（消息历史、好友关系、屏蔽关系）
- **Redis**: 继续用于实时状态管理（在线用户、聊天状态）

## 主要优势

1. **数据持久化**: 消息和关系数据永久保存
2. **事务支持**: MySQL提供ACID事务保证
3. **复杂查询**: 支持复杂的SQL查询和统计
4. **数据一致性**: 更好的数据一致性保证

## 注意事项

1. **性能考虑**: MySQL查询比Redis稍慢，但提供更好的持久化
2. **连接管理**: 注意MySQL连接的创建和释放
3. **错误处理**: 增加了数据库连接失败的错误处理
4. **索引优化**: 确保在高频查询字段上建立适当索引

## 测试建议

1. **功能测试**: 测试好友添加、删除、屏蔽、解除屏蔽功能
2. **消息测试**: 测试正常消息发送和屏蔽状态下的消息处理
3. **历史消息**: 测试历史消息的获取和分页
4. **并发测试**: 测试多用户同时操作的情况

## 后续优化

1. **连接池**: 实现MySQL连接池提高性能
2. **缓存策略**: 对频繁查询的好友关系进行缓存
3. **批量操作**: 对批量消息插入进行优化
4. **监控**: 添加数据库操作的监控和日志
