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
-- uk_friend_pair 唯一约束的作用：双方同时互加好友时，
-- 只会落一行关系，不会产生重复记录
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
-- 说明：不预置示例账号。
-- 口令必须经过服务端的加盐哈希流程，直接 INSERT 明文也能登录
-- （服务端对 salt 为空的旧记录会按明文比对并自动升级），
-- 但推荐直接在客户端注册新账号，流程更真实。
-- ------------------------------------------------------------
