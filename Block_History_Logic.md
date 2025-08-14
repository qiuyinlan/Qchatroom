# 屏蔽历史记录逻辑说明

## 🎯 核心需求
当用户被拉黑/删除后：
- ✅ **发送者能看到自己发送的消息** - 我的历史记录中显示我发过的消息
- ❌ **接收者看不到被拉黑用户的消息** - 对方的历史记录中不显示我发的消息

## 🔄 实现逻辑

### 1. 消息发送时 (`start_chat_mysql`)
```cpp
// 重要：无论什么情况，都要保存消息到MySQL
mysql.insertPrivateMessage(user.getUID(), UID, message.getContent());

// 然后再检查是否被屏蔽/删除
if (!redis.sismember(UID, user.getUID())) {
    // 被删除：消息已保存，但不发送给对方
    sendMsg(receiver_fd, "FRIEND_VERIFICATION_NEEDED");
    continue; 
}

if (redis.sismember("blocked" + UID, user.getUID())) {
    // 被屏蔽：消息已保存，但不发送给对方
    sendMsg(receiver_fd, "BLOCKED_MESSAGE");
    continue;
}
```

**关键点**：
- 📝 **先保存消息**，再检查屏蔽状态
- 💾 **所有消息都存储到MySQL**，包括被屏蔽的消息
- 🚫 **被屏蔽的消息不实时发送**给对方

### 2. 历史记录查询时 (`F_history_mysql`)
```cpp
// 从MySQL获取所有历史消息
vector<string> all_messages = mysql.getPrivateHistory(user.getUID(), records_index, 100);

// 智能过滤消息
bool i_blocked_him = redis.sismember("blocked" + user.getUID(), records_index);
bool he_blocked_me = redis.sismember("blocked" + records_index, user.getUID());

for (const string& msg : all_messages) {
    string sender = 解析消息发送者;
    
    if (sender == user.getUID()) {
        // 我发送的消息：总是显示
        filtered_messages.push_back(msg);
    } else {
        // 对方发送的消息：只有在我没有屏蔽对方时才显示
        if (!i_blocked_him) {
            filtered_messages.push_back(msg);
        }
    }
}
```

**关键点**：
- 📖 **查询时动态过滤**，而不是存储时过滤
- 👀 **我的消息总是可见**：我能看到我发过的所有消息
- 🚫 **屏蔽对方的消息不可见**：如果我屏蔽了对方，不显示对方的消息

## 📊 场景分析

### 场景1：A发消息给B，然后B把A拉黑了
1. **A发送消息时**：
   - ✅ 消息保存到MySQL
   - ✅ 实时发送给B（此时还没被拉黑）

2. **B拉黑A后**：
   - ✅ A查看历史记录：能看到自己发给B的所有消息
   - ✅ B查看历史记录：能看到A之前发的消息，但看不到A被拉黑后发的新消息

3. **A被拉黑后继续发消息**：
   - ✅ 消息保存到MySQL
   - ❌ 不实时发送给B
   - ✅ A能在历史记录中看到这条消息
   - ❌ B在历史记录中看不到这条消息

### 场景2：A把B拉黑了
1. **A查看与B的历史记录**：
   - ✅ 显示A发给B的所有消息
   - ❌ 不显示B发给A的消息（因为A屏蔽了B）

2. **B查看与A的历史记录**：
   - ✅ 显示B发给A的所有消息
   - ✅ 显示A发给B的消息（B没有屏蔽A）

## 🔧 技术实现细节

### 消息格式
假设MySQL中存储的消息格式为：`"发送者UID|接收者UID|消息内容|时间戳"`

### 过滤算法
```cpp
for (const string& msg : all_messages) {
    size_t pos1 = msg.find('|');
    string sender = msg.substr(0, pos1);
    
    if (sender == current_user) {
        // 我的消息：总是显示
        show_message(msg);
    } else if (!i_blocked_sender) {
        // 对方的消息：只有在我没屏蔽对方时显示
        show_message(msg);
    }
    // 如果我屏蔽了发送者，就跳过这条消息
}
```

## 🎯 优势

1. **用户体验好**：
   - 发送者能看到自己的消息历史
   - 被屏蔽者不会收到骚扰消息

2. **数据完整性**：
   - 所有消息都被保存
   - 解除屏蔽后可以看到完整历史

3. **灵活性**：
   - 屏蔽状态改变时，历史记录显示会相应调整
   - 不需要删除或修改已存储的消息

## 🚀 使用方法

1. **创建MySQL表**：
```sql
CREATE TABLE private_messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    sender_uid VARCHAR(64) NOT NULL,
    receiver_uid VARCHAR(64) NOT NULL,
    content TEXT NOT NULL,
    sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_users (sender_uid, receiver_uid),
    INDEX idx_time (sent_at)
);
```

2. **编译运行**：
```bash
cd 111111chatroom/messengertest
mkdir -p build && cd build
cmake .. && make -j4
./server.out 8080
```

3. **测试场景**：
   - 用户A和B互相添加好友
   - A发消息给B
   - B把A拉黑
   - A继续发消息给B
   - 分别查看A和B的历史记录，验证过滤逻辑

这样就实现了既保存完整消息历史，又能根据当前屏蔽状态智能显示的功能！
