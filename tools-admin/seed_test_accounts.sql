-- Run this ONCE after the server has started at least one time (so the `peer`
-- table exists), then restart the server or have these accounts log in with
-- the credentials below. They will already have the role set.
--
-- Usage:
--   mysql -u <user> -p <database_name> < seed_test_accounts.sql
--
-- Roles: player=0, vip=1, guardian=2, moderator=3, administrator=4
--
-- NOTE (security): Gurotopia stores passwords in plain text (see peer.hpp /
-- database.cpp, column `password VARCHAR(18)`), so these show up as-is in the
-- database. Fine for a local test server; do not reuse these passwords
-- anywhere real, and do not expose this server to the internet with them.

INSERT INTO peer (growid, password, role, gems)
VALUES ('administrator', 'administrator', 4, 1000000)
ON DUPLICATE KEY UPDATE password = VALUES(password), role = VALUES(role);

INSERT INTO peer (growid, password, role, gems)
VALUES ('bot1', 'bot1', 0, 10000)
ON DUPLICATE KEY UPDATE password = VALUES(password), role = VALUES(role);

INSERT INTO peer (growid, password, role, gems)
VALUES ('bot2', 'bot2', 0, 10000)
ON DUPLICATE KEY UPDATE password = VALUES(password), role = VALUES(role);

INSERT INTO peer (growid, password, role, gems)
VALUES ('bot3', 'bot3', 0, 10000)
ON DUPLICATE KEY UPDATE password = VALUES(password), role = VALUES(role);

-- Quick check:
SELECT uid, growid, role, gems FROM peer WHERE growid IN ('administrator','bot1','bot2','bot3');
