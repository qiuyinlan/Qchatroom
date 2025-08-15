# 一个多人聊天室项目

一个基于C++17开发的高性能、多功能聊天系统，支持私聊、群聊、文件传输等功能。采用现代化的架构设计，使用Redis和MySQL双存储策略，支持高并发连接。

### 编译和运行

#### 1. 克隆项目
```bash
git clone https://github.com/qiuyinlan/Qchatroom
cd Qchatroom
```

#### 2. 编译项目
```bash
mkdir build
cd build
cmake ..
make
```

#### 3.启动服务器
```bash
# 在build目录下
./server.out ip port
```

#### 4.启动客户端
```bash
# 在另一个终端，在build目录下
./client.out ip port
```

## 🏗️ 系统架构

### 整体架构
```
客户端层 ↔ 网络层(TCP/Epoll) ↔ 服务器核心(线程池) ↔ 业务逻辑层 ↔ 存储层(Redis/MySQL)
```

### 存储策略
- **Redis**: 用户状态、好友关系、群聊管理、实时状态
- **MySQL**: 私聊消息、群聊消息历史记录

## 📁 项目结构

```
MessengerRebuild/
├── CMakeLists.txt              # CMake构建配置
├── README.md                   # 项目说明文档
├── architecture_diagrams.md   # 架构图文档
├── migrate_group_messages.sql # 数据迁移脚本
├── build/                      # 构建目录
│   ├── server.out             # 服务器可执行文件
│   ├── client.out             # 客户端可执行文件
│   └── ...                    # 其他构建文件
├── server/                     # 服务器端代码
│   ├── server.cc              # 主服务器程序
│   ├── IO.cc/IO.h             # IO处理层(ET模式)
│   ├── Transaction.cc/.h      # 业务逻辑处理
│   ├── group_chat.cc/.h       # 群聊功能模块
│   ├── LoginHandler.cc/.h     # 登录处理模块
│   ├── Redis.cc/.h            # Redis数据库接口
│   ├── MySQL.cc/.h            # MySQL数据库接口
│   ├── ThreadPool.hpp         # 线程池实现
│   └── Threadpool.cpp         # 线程池实现
├── client/                     # 客户端代码
│   ├── client.cc/.h           # 客户端主程序
│   ├── controller/            # 控制层
│   │   ├── StartMenu.cc/.h    # 启动菜单
│   │   └── OperationMenu.cc/.h # 操作菜单
│   ├── social/                # 社交功能层
│   │   ├── FriendManager.cc/.h # 好友管理
│   │   ├── chat.cc/.h         # 私聊功能
│   │   └── G_chat.cc/.h       # 群聊功能
│   └── service/               # 服务层
│       ├── FileTransfer.cc/.h # 文件传输
│       └── Notifications.cc/.h # 通知服务
└── utils/                      # 工具类
    ├── User.cc/.h             # 用户模型
    ├── Group.cc/.h            # 群组模型
    ├── TCP.cc/.h              # TCP网络工具
    ├── IO.cc/.h               # IO工具函数
    └── proto.cc/.h            # 协议处理
```

## 🛠️ 技术栈

### 核心技术
- **语言**: C++17
- **网络编程**: TCP Socket + Epoll ET模式
- **并发处理**: 线程池 + 互斥锁
- **数据库**: Redis + MySQL
- **JSON处理**: nlohmann/json
- **构建工具**: CMake

### 依赖库
- `nlohmann/json`: JSON数据处理
- `hiredis`: Redis客户端库
- `mysqlclient`: MySQL客户端库
- `curl`: HTTP请求库

## 🚀 快速开始

### 环境要求`mysqlclient`: MySQL客户端库
- Linux操作系统
- GCC 7.0+ (支持C++17)
- CMake 3.10+
- Redis服务器
- MySQL服务器

### 安装依赖
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential cmake
sudo apt install libhiredis-dev libmysqlclient-dev libcurl4-openssl-dev
sudo apt install nlohmann-json3-dev

# CentOS/RHEL
sudo yum groupinstall "Development Tools"
sudo yum install cmake hiredis-devel mysql-devel libcurl-devel
```

### 数据库配置

#### Redis配置
```bash
# 启动Redis服务
sudo systemctl start redis
sudo systemctl enable redis

# 配置Redis持久化(推荐)
redis-cli CONFIG SET appendonly yes
redis-cli CONFIG SET appendfsync everysec
```

#### MySQL配置
```sql
-- 创建数据库和表
CREATE DATABASE chatroom CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE chatroom;

-- 私聊消息表
CREATE TABLE private_messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    sender_uid VARCHAR(64) NOT NULL,
    receiver_uid VARCHAR(64) NOT NULL,
    content TEXT NOT NULL,
    sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_users (sender_uid, receiver_uid),
    INDEX idx_time (sent_at)
);

-- 群聊消息表
CREATE TABLE group_messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    group_uid VARCHAR(64) NOT NULL,
    sender_uid VARCHAR(64) NOT NULL,
    content TEXT NOT NULL,
    sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_group (group_uid),
    INDEX idx_time (sent_at)
);
```



## 📖 功能说明

### 🔐 用户系统
- **用户注册/登录**: 支持用户注册和登录验证
- **在线状态管理**: 实时显示用户在线状态
- **用户信息管理**: 用户基本信息的存储和管理

### 👥 好友系统
- **添加好友**: 发送好友请求，等待对方确认
- **删除好友**: 删除好友关系
- **屏蔽功能**: 屏蔽/取消屏蔽用户
- **好友列表**: 查看在线好友列表
- **好友请求管理**: 查看和处理好友请求

### 💬 聊天功能
- **私聊**: 与好友进行一对一聊天
- **群聊**: 创建群聊，邀请好友加入
- **历史消息**: 查看聊天历史记录
- **离线消息**: 离线时接收消息通知
- **消息过滤**: 基于好友关系的消息过滤

### 🏢 群聊管理
- **创建群聊**: 创建新的群聊
- **加入群聊**: 申请加入已有群聊
- **群聊管理**: 群主和管理员权限管理
- **成员管理**: 添加/移除群成员
- **群聊解散**: 群主解散群聊

### 📁 文件传输
- **好友文件传输**: 向好友发送文件
- **群聊文件传输**: 在群聊中分享文件
- **文件接收**: 接收并保存文件

## 🔧 核心技术实现

### 网络架构
- **Epoll ET模式**: 高效的事件驱动网络编程
- **线程池**: 多线程处理客户端请求
- **非阻塞IO**: recvMsgET/sendMsgET实现
- **连接管理**: 统一的连接状态管理

### 数据存储
- **Redis存储结构**:
  ```
  is_online: {UID -> 连接信息}
  {UID}: {好友UID集合}
  blocked{UID}: {屏蔽用户集合}
  group_info: {群聊UID -> 群聊信息JSON}
  {群聊UID}_members: {成员UID集合}
  ```

- **MySQL存储结构**:
  ```
  private_messages: 私聊消息历史
  group_messages: 群聊消息历史
  ```

### 消息协议
- **协议格式**: [4字节长度头] + [消息内容]
- **JSON消息**: 统一的JSON格式消息
- **分片处理**: 支持大消息的分片传输

## 🎯 性能特点

### 高并发支持
- **Epoll ET模式**: 支持大量并发连接
- **线程池**: 避免频繁创建销毁线程
- **非阻塞IO**: 提高网络IO效率
- **连接复用**: 统一的连接管理

### 存储优化
- **Redis缓存**: 热数据快速访问
- **MySQL持久化**: 可靠的数据存储
- **索引优化**: 高效的数据查询
- **分页加载**: 大量数据的分页处理

### 内存管理
- **缓冲区复用**: 减少内存分配
- **对象池**: 复用频繁创建的对象
