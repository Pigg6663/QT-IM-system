-- ============================================================
--  从旧版本升级到加盐哈希 / 好友唯一约束
--  适用：已经在用旧库（pwd 明文、无 salt 列、friend 无唯一键）
--  用法：mysql -uroot -p mydb2501 < sql/migrate_v2.sql
--
--  注意：如果执行时报 "duplicate entry"，说明 friend 表里已有
--  重复的 (user_id, friend_id)，先执行下面的去重语句再建唯一键。
-- ============================================================

USE `mydb2501`;

-- 1) 增加 salt 列（默认空串，用于识别尚未升级的历史明文记录）
ALTER TABLE `user_info`
    ADD COLUMN `salt` VARCHAR(32) NOT NULL DEFAULT '';

-- 2) 口令长度从 32 扩到 64，容纳 SHA-256 的 hex 结果
ALTER TABLE `user_info`
    MODIFY `pwd` VARCHAR(64) NOT NULL;

-- 3) 补 id 主键列。
--    旧版 friend 表只有 (user_id, friend_id) 两列，没有 id；而下面的去重语句
--    依赖 id 判断"保留哪一行"，缺这一步会直接报 Unknown column 'f1.id'。
--    （若库里已有 id 列，跳过本步）
ALTER TABLE `friend`
    ADD COLUMN `id` INT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;

-- 4) 好友关系去重（保留每组关系里 id 最小的一行）
DELETE f1 FROM `friend` f1
    INNER JOIN `friend` f2
    WHERE f1.`user_id` = f2.`user_id`
      AND f1.`friend_id` = f2.`friend_id`
      AND f1.`id` > f2.`id`;

-- 5) 加唯一约束，防止同一方向重复插入
ALTER TABLE `friend`
    ADD UNIQUE KEY `uk_friend_pair` (`user_id`, `friend_id`);

-- ------------------------------------------------------------
-- 升级后：历史用户第一次登录时，服务端会发现 salt 为空，
-- 按明文比对成功一次后自动改写成 "pwd=哈希, salt=随机盐"，
-- 之后数据库里不再有明文口令。
-- 也可以选择直接清空用户表重新注册：
--   TRUNCATE TABLE friend;
--   DELETE FROM user_info;
-- ------------------------------------------------------------
