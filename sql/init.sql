-- ============================================================
--  Qt 即时通讯与文件传输系统 - 数据库初始化脚本
--  数据库：MySQL 5.7+ / 8.0
--  用法：mysql -uroot -p < sql/init.sql
-- ============================================================

CREATE DATABASE IF NOT EXISTS `mydb2501`
    DEFAULT CHARACTER SET utf8mb4
    COLLATE utf8mb4_general_ci;

USE `mydb2501`;

-- ------------------------------------------------------------
-- 用户表
-- id      : 用户唯一标识，注册时自增生成（服务端 insert 未显式指定 id）
-- name    : 登录名，作为好友关系与在线列表的检索键
-- pwd     : 口令的加盐哈希，SHA-256 后为 64 位 hex（不再保存明文）
-- salt    : 每个用户独立的 16 字节随机盐，hex 后 32 位
-- online  : 在线状态，0 = 离线，1 = 在线；登录置 1，断线/心跳超时置 0
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `user_info` (
    `id`     INT         NOT NULL AUTO_INCREMENT,
    `name`   VARCHAR(32) NOT NULL,
    `pwd`    VARCHAR(64) NOT NULL,
    `online` TINYINT(1)  NOT NULL DEFAULT 0,
    `salt`   VARCHAR(32) NOT NULL DEFAULT '',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ------------------------------------------------------------
-- 好友关系表（无向关系：A-B 与 B-A 语义相同）
-- 查询好友列表时对 user_id / friend_id 做双向 union 匹配
--
-- uk_friend_pair 唯一约束的作用：阻止同一方向（user_id -> friend_id）
-- 重复插入，例如同一申请被重复同意时不会落两行。
-- 注意它不能阻止 A->B 与 B->A 同时存在——这是两个不同的有序元组；
-- 只是好友列表查询做了双向 union，所以结果里看不出重复。
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `friend` (
    `id`        INT NOT NULL AUTO_INCREMENT,
    `user_id`   INT NOT NULL,
    `friend_id` INT NOT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_friend_pair` (`user_id`, `friend_id`),
    KEY `idx_user_id`   (`user_id`),
    KEY `idx_friend_id` (`friend_id`),
    CONSTRAINT `fk_friend_user`   FOREIGN KEY (`user_id`)   REFERENCES `user_info` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_friend_friend` FOREIGN KEY (`friend_id`) REFERENCES `user_info` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ------------------------------------------------------------
-- 离线消息表
-- 对方不在线时聊天消息先落到这里，等其登录成功后由服务端补发。
--
-- sender / receiver 存登录名（不是 user_info.id）：
-- 补发时按登录名直接查，省一次关联；代价是改名需要同步维护，
-- 本项目没有改名功能，所以这个取舍是划算的。
-- is_read：0 = 未投递，1 = 已补发（或已在线投递）。
-- idx_receiver_read 覆盖 "查某人的未读消息" 这个唯一的高频查询。
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `message` (
    `id`         BIGINT       NOT NULL AUTO_INCREMENT,
    `sender`     VARCHAR(32)  NOT NULL,
    `receiver`   VARCHAR(32)  NOT NULL,
    `content`    TEXT         NOT NULL,
    `is_read`    TINYINT(1)   NOT NULL DEFAULT 0,
    `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_receiver_read` (`receiver`, `is_read`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ------------------------------------------------------------
-- 说明：不预置示例账号。
-- 口令必须经过服务端的加盐哈希流程，直接 INSERT 明文也能登录
-- （服务端对 salt 为空的旧记录会按明文比对并自动升级），
-- 但推荐直接在客户端注册新账号，流程更真实。
-- ------------------------------------------------------------
