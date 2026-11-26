-- ============================================================
--  从 v2 升级到 v3：新增离线消息表
--  适用：已经跑过 sql/init.sql（v2，含 salt 列与好友唯一键）的旧库
--  用法：mysql -uroot -p mydb2501 < sql/migrate_v3.sql
--
--  全新部署只需执行 sql/init.sql，不需要本脚本。
-- ============================================================

USE `mydb2501`;

-- sender / receiver 存登录名而非 user_info.id：
-- 补发时按登录名直接查，省一次关联；本项目没有改名功能，不会失效。
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
-- 投递语义说明（写进 README 的同一段）：
-- 服务端"先查出未读 -> 逐条下发 -> 再标记已读"。
-- 若在"下发完"与"标记已读"之间进程崩溃，该批消息会重复投递一次。
-- 这是有意的取舍：宁可重复，不可丢失。
-- 若要严格一次投递，需要客户端回 ACK + 服务端按消息 id 去重，
-- 那需要改协议，成本更高，本版本没做。
-- ------------------------------------------------------------
