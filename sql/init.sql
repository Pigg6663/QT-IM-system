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
-- pwd     : 密码（注意：当前为明文存储，改进方向见 README）
-- online  : 在线状态，0 = 离线，1 = 在线；登录置 1，断线置 0
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `user_info` (
    `id`     INT         NOT NULL AUTO_INCREMENT,
    `name`   VARCHAR(32) NOT NULL,
    `pwd`    VARCHAR(32) NOT NULL,
    `online` INT         NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ------------------------------------------------------------
-- 好友关系表（无向关系：A-B 与 B-A 语义相同）
-- 查询好友列表时对 user_id / friend_id 做双向 union 匹配
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `friend` (
    `user_id`   INT NOT NULL,
    `friend_id` INT NOT NULL,
    KEY `idx_user_id`   (`user_id`),
    KEY `idx_friend_id` (`friend_id`),
    CONSTRAINT `fk_friend_user`   FOREIGN KEY (`user_id`)   REFERENCES `user_info` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_friend_friend` FOREIGN KEY (`friend_id`) REFERENCES `user_info` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ------------------------------------------------------------
-- 示例数据（可选，便于快速验证登录流程）
-- 密码与 operatedb.cpp 中一致，为明文 '123456'
-- ------------------------------------------------------------
INSERT IGNORE INTO `user_info` (`id`, `name`, `pwd`, `online`) VALUES
    (1, 'alice', '123456', 0),
    (2, 'bob',   '123456', 0);
