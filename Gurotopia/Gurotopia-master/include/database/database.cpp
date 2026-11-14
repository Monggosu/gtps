#include "pch.hpp"

#include "tools/terminal.hpp"
#include "tools/economy.hpp"

#include <fstream>

#include "database.hpp"

::mysql_gateway db;

void mysql_gateway::create_table_if_not_exist()
{
    int error;
    error = mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS peer ("
        "uid INT AUTO_INCREMENT PRIMARY KEY,"
        "growid VARCHAR(18) UNIQUE,"
        "password VARCHAR(18),"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "inventory BLOB,"
        "gems BIGINT DEFAULT 0,"
        "wl_storage INT DEFAULT 0,"
        "daily_last BIGINT DEFAULT 0,"
        "daily_streak INT DEFAULT 0,"
        "rid VARCHAR(64) DEFAULT '',"
        "title_id INT DEFAULT 0,"
        "ban_until BIGINT DEFAULT 0,"
        "level INT DEFAULT 1,"
        "xp INT DEFAULT 0,"
        "role TINYINT DEFAULT 0,"
        "muted_until INT DEFAULT 0,"
        "ban_reason VARCHAR(255) DEFAULT ''"
    ")");
    error = mysql_query(mysql, 
        "CREATE TABLE IF NOT EXISTS world ("
        "name VARCHAR(24) NOT NULL PRIMARY KEY,"
        "owner INT DEFAULT 0,"
        "blocks BLOB,"
        "objects BLOB"
    ")");
    error = mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS moderation ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "action VARCHAR(10) NOT NULL,"
        "target VARCHAR(18) NOT NULL,"
        "actor VARCHAR(18) NOT NULL,"
        "reason VARCHAR(255) NOT NULL,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "INDEX idx_actor_date (actor, created_at)"
    ")");
    error = mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS economy_log ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "ts TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "kind VARCHAR(16) NOT NULL,"
        "from_uid INT DEFAULT 0,"
        "to_uid INT DEFAULT 0,"
        "amount BIGINT DEFAULT 0,"
        "tax BIGINT DEFAULT 0,"
        "note VARCHAR(64) DEFAULT '',"
        "INDEX (from_uid), INDEX (to_uid), INDEX (ts)"
    ")");
    error = mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS npc_quota ("
        "uid INT NOT NULL,"
        "day BIGINT NOT NULL,"
        "seed INT NOT NULL,"
        "sold INT DEFAULT 0,"
        "PRIMARY KEY (uid, day, seed)"
    ")");
    mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS sentinel_event ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "ts TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "uid INT NOT NULL DEFAULT 0,"
        "growid VARCHAR(24) NOT NULL DEFAULT '',"
        "rule VARCHAR(12) NOT NULL,"
        "sev TINYINT NOT NULL DEFAULT 0,"
        "detail VARCHAR(160) NOT NULL DEFAULT '',"
        "action VARCHAR(24) NOT NULL DEFAULT '',"
        "shadow TINYINT NOT NULL DEFAULT 0,"
        "INDEX idx_uid_ts (uid, ts), INDEX idx_ts (ts)"
    ")");
    mysql_query(mysql, "CREATE TABLE IF NOT EXISTS sentinel_cfg (k VARCHAR(16) NOT NULL PRIMARY KEY, v BIGINT NOT NULL DEFAULT 0)");
    mysql_query(mysql, "CREATE TABLE IF NOT EXISTS nuked_world (name VARCHAR(24) NOT NULL PRIMARY KEY, actor VARCHAR(18) NOT NULL, created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");
    mysql_query(mysql, "CREATE TABLE IF NOT EXISTS rid_ban (rid VARCHAR(64) NOT NULL PRIMARY KEY, reason VARCHAR(120) NOT NULL, actor VARCHAR(18) NOT NULL, created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");
    mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS report ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "reporter VARCHAR(18) NOT NULL,"
        "target VARCHAR(18) NOT NULL,"
        "category VARCHAR(12) NOT NULL,"
        "note VARCHAR(120) NOT NULL DEFAULT '',"
        "target_world VARCHAR(24) NOT NULL DEFAULT '',"
        "reporter_world VARCHAR(24) NOT NULL DEFAULT '',"
        "status TINYINT NOT NULL DEFAULT 0,"
        "handled_by VARCHAR(18) NOT NULL DEFAULT '',"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "handled_at TIMESTAMP NULL DEFAULT NULL,"
        "INDEX idx_status_target (status, target),"
        "INDEX idx_reporter_date (reporter, created_at)"
    ")");
    mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS title ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "tag VARCHAR(16) NOT NULL UNIQUE,"
        "text VARCHAR(64) NOT NULL,"
        "suffix TINYINT NOT NULL DEFAULT 0,"
        "price BIGINT NOT NULL DEFAULT 0,"
        "rule VARCHAR(16) NOT NULL DEFAULT ''"
    ")");
    mysql_query(mysql, "CREATE TABLE IF NOT EXISTS peer_title (uid INT NOT NULL, title_id INT NOT NULL, PRIMARY KEY (uid, title_id))");
    mysql_query(mysql,
        "INSERT IGNORE INTO title (tag, text, suffix, price, rule) VALUES "
        "('rookie',  '`2[Rookie]``', 0, 0, 'level:5'),"
        "('farmer',  '`3[Farmer]``', 0, 0, 'level:15'),"
        "('pro',     '`5[Pro]``',    0, 0, 'level:30'),"
        "('elite',   '`6[Elite]``',  0, 0, 'level:50'),"
        "('legend',  '`9of Legend``',1, 0, 'level:100'),"
        "('loyal',   '`b[Loyal]``',  0, 0, 'streak:7'),"
        "('vip',     '`c[VIP]``',    0, 0, 'role:1'),"
        "('rainbow', '`4R`6a`2i`3n`1b`5o`bw``', 0, 50000, ''),"
        "('crown',   '`6<`^Crown`6>``', 0, 250000, ''),"
        "('boss',    '`4[Boss]``',   0, 1000000, '')");
    mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS redeem_code ("
        "code VARCHAR(24) NOT NULL PRIMARY KEY,"
        "gems BIGINT NOT NULL DEFAULT 0,"
        "max_uses INT NOT NULL DEFAULT 1,"
        "used INT NOT NULL DEFAULT 0,"
        "expires BIGINT NOT NULL DEFAULT 0,"
        "created_by VARCHAR(18) NOT NULL DEFAULT ''"
    ")");
    mysql_query(mysql, "CREATE TABLE IF NOT EXISTS redeem_used (code VARCHAR(24) NOT NULL, uid INT NOT NULL, PRIMARY KEY (code, uid))");
    // @note old quota rows are useless after a few days
    mysql_query(mysql, std::format("DELETE FROM npc_quota WHERE day < ((UNIX_TIMESTAMP() + {}) DIV 86400) - 3", DAILY_RESET_OFFSET).c_str());
    
    if (error) print(mysql_error(mysql));
}

bool mysql_gateway::column_found(const std::string &table, const std::string &column)
{
    mysql_query(mysql, std::format(
        "SHOW COLUMNS FROM {} WHERE Field = '{}'", 
        table, column).c_str()
    );
    MYSQL_RES *res = mysql_store_result(mysql);

    my_ulonglong rows = mysql_num_rows(res);
    mysql_free_result(res);
    return (rows > 0);
}

int mysql_gateway::alter_table(const std::string &table, const std::string &column)
{
    return mysql_query(mysql, std::format("ALTER TABLE {} ADD COLUMN {}", table, column).c_str());
}

/* @note i use this for new columns. this could be removed once i 100% copy rgt. */
void mysql_gateway::integrity()
{
    if (!column_found("peer", "gems"))  alter_table("peer", "gems BIGINT DEFAULT 0");
    if (!column_found("peer", "level")) alter_table("peer", "level INT DEFAULT 1");
    if (!column_found("peer", "xp"))    alter_table("peer", "xp INT DEFAULT 0");
    if (!column_found("peer", "wl_storage")) alter_table("peer", "wl_storage INT DEFAULT 0");
    if (!column_found("peer", "role"))         alter_table("peer", "role TINYINT DEFAULT 0");
    if (!column_found("peer", "muted_until"))  alter_table("peer", "muted_until INT DEFAULT 0");
    if (!column_found("peer", "ban_reason"))   alter_table("peer", "ban_reason VARCHAR(255) DEFAULT ''");
    if (!column_found("peer", "daily_last"))   alter_table("peer", "daily_last BIGINT DEFAULT 0");
    if (!column_found("peer", "daily_streak")) alter_table("peer", "daily_streak INT DEFAULT 0");
    if (!column_found("peer", "rid"))          alter_table("peer", "rid VARCHAR(64) DEFAULT ''");
    if (!column_found("peer", "title_id"))     alter_table("peer", "title_id INT DEFAULT 0");
    if (!column_found("peer", "ban_until"))    alter_table("peer", "ban_until BIGINT DEFAULT 0");
}

void mysql_gateway::ensure_admin()
{
    long long admins = 0;
    {
        ::hStmt hStmt{ "SELECT COUNT(*) FROM peer WHERE role >= 4" };
        u_long length{};
        MYSQL_BIND result = make_bind_out(admins);
        result.length = &length;
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        hStmt.fetch();
    }
    if (admins > 0) return;

    long long users = 0;
    {
        ::hStmt hStmt{ "SELECT COUNT(*) FROM peer" };
        u_long length{};
        MYSQL_BIND result = make_bind_out(users);
        result.length = &length;
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        hStmt.fetch();
    }
    if (users == 0) return; // @note first register will self-promote in create_account

    if (mysql_query(mysql, "UPDATE peer SET role = 4 ORDER BY uid ASC LIMIT 1"))
        print(mysql_error(mysql));
    else
        print("no administrator found, promoted oldest account to administrator");
}

void mysql_gateway::mysql_connect()
{
    if (!mysql_real_connect(mysql, this->host.c_str(), this->user.c_str(), this->passwd.c_str(), NULL, 3306u, NULL, 0ul)) 
    {
        print(mysql_error(mysql));
    }
    else print(std::format("connected to MariaDB server on {}:{}", mysql->host, mysql->port).c_str());

    mysql_query(mysql, "CREATE DATABASE IF NOT EXISTS gurotopia");
    mysql_select_db(mysql, "gurotopia");

    create_table_if_not_exist();
    integrity();
    ensure_admin();
}

void mysql_gateway::load_login()
{
    std::ifstream istrm("mysql_login.txt");
    if (!istrm.is_open())
    {
        std::ofstream ostrm("mysql_login.txt");
        ostrm << 
            std::format(
                "host|{}\n"
                "user|{}\n"
                "password|{}",
                this->host, this->user, this->passwd
            );
    } // @note close ostrm
    else
    {
        for (std::string line; std::getline(istrm, line); ) 
        {
            ::hPipe hPipe{ line };

            if (!hPipe["host"].empty()) this->host = hPipe["host"];
            else if (!hPipe["user"].empty()) this->user = hPipe["user"];
            else if (!hPipe["password"].empty()) this->passwd = hPipe["password"];
        }
    } // @note delete pipes
}


hStmt::hStmt(const std::string &query)
{
    this->pStmt = mysql_stmt_init(db.mysql);
    if (!pStmt) 
    {
        log_stmt_error();
    }
    if (mysql_stmt_prepare(pStmt, query.c_str(), (u_long)query.size()))
    {
        log_stmt_error();
    }
}
hStmt::~hStmt() 
{
    if (mysql_stmt_close(pStmt))
    {
        log_stmt_error();
    }
}

void hStmt::bind_param(MYSQL_BIND *param)
{
    if (mysql_stmt_bind_param(pStmt, param)) log_stmt_error();
}
void hStmt::execute()
{
    if (mysql_stmt_execute(pStmt)) log_stmt_error();
}
void hStmt::fetch()
{
    int value = mysql_stmt_fetch(pStmt);
    if (value == 1 || value == MYSQL_DATA_TRUNCATED) log_stmt_error();
    else if (value == 0 || value == MYSQL_NO_DATA) /*@todo do something later...*/; // @note success
}
void hStmt::log_stmt_error() 
{
    print(mysql_stmt_error(pStmt));
}
/* ~ hStmt ~ */



MYSQL_BIND make_bind_in(const signed &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_in(const unsigned &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG, .is_unsigned = true };
}
MYSQL_BIND make_bind_in(const long &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_in(const long long &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONGLONG };
}
MYSQL_BIND make_bind_in(const float &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_FLOAT };
}
MYSQL_BIND make_bind_in(const std::string &buffer)
{
    return { .buffer = (void*)buffer.c_str(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_STRING };
}
MYSQL_BIND make_bind_in(const std::vector<u_char> &buffer)
{
    return { .buffer = (void*)buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
MYSQL_BIND make_bind_in(const ::blob &buffer)
{
    return { .buffer = (void*)buffer.data().data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}

MYSQL_BIND make_bind_out(signed &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_out(unsigned &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG, .is_unsigned = true };
}
MYSQL_BIND make_bind_out(long &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_out(long long &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONGLONG };
}
MYSQL_BIND make_bind_out(float &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_FLOAT };
}
MYSQL_BIND make_bind_out(std::string &buffer)
{
    buffer.resize(1024, '\0');

    return { .buffer = buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_STRING };
}
MYSQL_BIND make_bind_out(std::vector<u_char> &buffer)
{
    buffer.resize(cord(0, 60)* sizeof(::block));

    return { .buffer = buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
MYSQL_BIND make_bind_out(::blob &buffer)
{
    buffer.resize(cord(0, 60)* sizeof(::block));

    return { .buffer = buffer.data().data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
