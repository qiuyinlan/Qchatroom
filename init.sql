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

-- 创建MySQL用户（可选，如果你想用专门的用户）
-- CREATE USER IF NOT EXISTS 'chatroom'@'localhost' IDENTIFIED BY 'chatroom123';
-- GRANT ALL PRIVILEGES ON chatroom.* TO 'chatroom'@'localhost';
-- FLUSH PRIVILEGES;
