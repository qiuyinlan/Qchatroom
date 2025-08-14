-- 创建聊天室数据库
CREATE DATABASE IF NOT EXISTS chatroom CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE chatroom;

-- 群聊消息表
CREATE TABLE IF NOT EXISTS group_messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    group_uid VARCHAR(64) NOT NULL,
    sender_uid VARCHAR(64) NOT NULL,
    content TEXT NOT NULL,
    sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_group (group_uid),
    INDEX idx_time (sent_at)
);

-- 私聊消息表
CREATE TABLE IF NOT EXISTS private_messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    sender_uid VARCHAR(64) NOT NULL,
    receiver_uid VARCHAR(64) NOT NULL,
    content TEXT NOT NULL,
    sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_users (sender_uid, receiver_uid),
    INDEX idx_time (sent_at)
);

-- 好友关系表
CREATE TABLE IF NOT EXISTS friends (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user1 VARCHAR(64) NOT NULL,
    user2 VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY unique_friendship (user1, user2),
    INDEX idx_user1 (user1),
    INDEX idx_user2 (user2)
);

-- 屏蔽关系表
CREATE TABLE IF NOT EXISTS blocks (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user VARCHAR(64) NOT NULL,
    blocked_user VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY unique_block (user, blocked_user),
    INDEX idx_user (user),
    INDEX idx_blocked (blocked_user)
);

-- 好友请求表
CREATE TABLE IF NOT EXISTS friend_requests (
    id INT AUTO_INCREMENT PRIMARY KEY,
    requester VARCHAR(64) NOT NULL,
    target VARCHAR(64) NOT NULL,
    status ENUM('pending', 'accepted', 'rejected') DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_target (target),
    INDEX idx_status (status)
);

-- 创建MySQL用户（可选，如果你想用专门的用户）
-- CREATE USER IF NOT EXISTS 'chatroom'@'localhost' IDENTIFIED BY 'chatroom123';
-- GRANT ALL PRIVILEGES ON chatroom.* TO 'chatroom'@'localhost';
-- FLUSH PRIVILEGES;
