#include "pch.hpp"

#include <algorithm>
#include <cctype>

#include "moderation.hpp"

::account_info fetch_account(const std::string &growid)
{
    ::account_info info{};

    ::hStmt hStmt{ "SELECT role, ban_reason, muted_until FROM peer WHERE growid = ? LIMIT 1" };
    MYSQL_BIND param = make_bind_in(growid);
    hStmt.bind_param(&param);

    signed role_v{};
    std::string ban_v(1024, '\0');
    signed muted_v{};
    u_long len_role{}, len_ban{}, len_muted{};

    MYSQL_BIND results[3] = {
        make_bind_out(role_v),
        make_bind_out(ban_v),
        make_bind_out(muted_v)
    };
    results[0].length = &len_role;
    results[1].length = &len_ban;
    results[2].length = &len_muted;
    mysql_stmt_bind_result(hStmt.pStmt, results);

    hStmt.execute();
    if (mysql_stmt_store_result(hStmt.pStmt) || mysql_stmt_num_rows(hStmt.pStmt) == 0)
        return info;

    mysql_stmt_fetch(hStmt.pStmt);
    info.found = true;
    info.role = (u_char)role_v;
    ban_v.resize(len_ban);
    info.ban_reason = ban_v;
    info.muted_until = muted_v;
    return info;
}

void mod_log(const std::string &action, const std::string &target, const std::string &actor, const std::string &reason)
{
    ::hStmt hStmt{ "INSERT INTO moderation (action, target, actor, reason) VALUES (?, ?, ?, ?)" };
    MYSQL_BIND params[4] = {
        make_bind_in(action),
        make_bind_in(target),
        make_bind_in(actor),
        make_bind_in(reason)
    };
    hStmt.bind_param(params);
    hStmt.execute();
}

int mod_bans_today(const std::string &actor)
{
    ::hStmt hStmt{ "SELECT COUNT(*) FROM moderation WHERE action = 'ban' AND actor = ? AND created_at >= CURDATE()" };
    MYSQL_BIND param = make_bind_in(actor);
    hStmt.bind_param(&param);

    long long count{};
    u_long length{};
    MYSQL_BIND result = make_bind_out(count);
    result.length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, &result);

    hStmt.execute();
    hStmt.fetch();
    return (int)count;
}

::online_target find_online(const std::string &growid)
{
    ::online_target target{};
    peers("", PeerSearch::EVERYONE, [&target, &growid](ENetPeer *peer)
    {
        if (target.enet) return;
        ::peer *p = static_cast<::peer*>(peer->data);
        if (p && p->growid == growid)
        {
            target.enet = peer;
            target.data = p;
        }
    });
    return target;
}

int parse_role(const std::string &s)
{
    if (s == "player")     return PLAYER;
    if (s == "vip")        return VIP;
    if (s == "guardian")   return GUARDIAN;
    if (s == "mod" || s == "moderator")      return MODERATOR;
    if (s == "admin" || s == "administrator") return ADMINISTRATOR;
    if (s == "0") return PLAYER;
    if (s == "1") return VIP;
    if (s == "2") return GUARDIAN;
    if (s == "3") return MODERATOR;
    if (s == "4") return ADMINISTRATOR;
    return -1;
}

bool reason_ok(const std::string &reason, std::size_t min_len)
{
    if (reason.size() < min_len) return false;
    return !std::ranges::all_of(reason, [](unsigned char c) { return std::isspace(c); });
}

std::string join_args(const std::vector<std::string> &argv, std::size_t from)
{
    std::string out;
    for (std::size_t i = from; i < argv.size(); ++i)
    {
        out += argv[i];
        if (i + 1 < argv.size()) out += ' ';
    }
    return out;
}
