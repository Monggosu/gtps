#include "pch.hpp"

#include <algorithm>
#include <unordered_set>

#include "action/quit_to_exit.hpp"
#include "commands/moderation.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "staff.hpp"

float g_staff_zoom = STAFF_CAMERA_ZOOM;

namespace
{
    /* @brief run a SELECT that returns one string column. arg (optional) is bound to the single '?'. */
    std::vector<std::string> select_col(const std::string &query, const std::string &arg = "")
    {
        std::vector<std::string> out;
        ::hStmt hStmt{ query };
        MYSQL_BIND param = make_bind_in(arg);
        if (!arg.empty()) hStmt.bind_param(&param);

        std::string value{};
        u_long length{};
        MYSQL_BIND result = make_bind_out(value);
        result.length = &length;
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        while (mysql_stmt_fetch(hStmt.pStmt) == 0)
            out.push_back(value.substr(0, std::min<std::size_t>(length, value.size())));
        return out;
    }

    std::unordered_set<std::string> &nuked_set()
    {
        static std::unordered_set<std::string> set;
        static bool loaded = false;
        if (!loaded)
        {
            loaded = true;
            for (const std::string &name : select_col("SELECT name FROM nuked_world")) set.insert(name);
        }
        return set;
    }

    std::unordered_set<std::string> &rid_set()
    {
        static std::unordered_set<std::string> set;
        static bool loaded = false;
        if (!loaded)
        {
            loaded = true;
            for (const std::string &rid : select_col("SELECT rid FROM rid_ban")) set.insert(rid);
        }
        return set;
    }
}

bool is_nuked(const std::string &world) { return nuked_set().contains(world); }

bool set_nuked(const std::string &world, bool nuked, const std::string &actor)
{
    std::unordered_set<std::string> &set = nuked_set();
    if (nuked)
    {
        if (!set.insert(world).second) return false;
        ::hStmt hStmt{ "INSERT INTO nuked_world (name, actor) VALUES (?, ?) ON DUPLICATE KEY UPDATE actor = VALUES(actor)" };
        MYSQL_BIND params[2] = { make_bind_in(world), make_bind_in(actor) };
        hStmt.bind_param(params);
        hStmt.execute();
        return true;
    }
    if (set.erase(world) == 0) return false;
    ::hStmt hStmt{ "DELETE FROM nuked_world WHERE name = ?" };
    MYSQL_BIND param = make_bind_in(world);
    hStmt.bind_param(&param);
    hStmt.execute();
    return true;
}

std::vector<std::string> nuked_list(std::size_t limit)
{
    std::vector<std::string> list = select_col("SELECT name FROM nuked_world ORDER BY created_at DESC");
    if (list.size() > limit) list.resize(limit);
    return list;
}

void evict_world(ENetEvent &event, const std::string &world, const std::string &message)
{
    std::vector<ENetPeer*> inside;
    peers(world, PeerSearch::WORLD, [&inside](ENetPeer *p)
    {
        const ::peer *pPeer = static_cast<::peer*>(p->data);
        if (pPeer && pPeer->role < GUARDIAN) inside.push_back(p);
    });
    for (ENetPeer *p : inside)
    {
        ENetEvent ev = event;
        ev.peer = p;
        action::quit_to_exit(ev, "", false); // @note false = the player lands on the world menu
        on::ConsoleMessage(p, message);
    }
}

void staff_notify(const std::string &message, u_char min_role)
{
    peers("", PeerSearch::EVERYONE, [&message, min_role](ENetPeer *p)
    {
        const ::peer *pPeer = static_cast<::peer*>(p->data);
        if (pPeer && pPeer->role >= min_role) on::ConsoleMessage(p, message);
    });
}

void staff_log(const std::string &action, const std::string &target, const std::string &actor, const std::string &reason)
{
    mod_log(action.substr(0, 10), target.substr(0, 18), actor, reason.substr(0, 255));
}

std::string fmt_duration(long long seconds)
{
    if (seconds < 0) seconds = 0;
    if (seconds >= 86400) return std::format("{}d {}h", seconds / 86400, (seconds % 86400) / 3600);
    if (seconds >= 3600)  return std::format("{}h {}m", seconds / 3600, (seconds % 3600) / 60);
    if (seconds >= 60)    return std::format("{}m", seconds / 60);
    return std::format("{}s", seconds);
}

void remember_rid(int uid, const std::string &rid)
{
    if (uid <= 0 || rid.empty()) return;
    ::hStmt hStmt{ "UPDATE peer SET rid = ? WHERE uid = ?" };
    MYSQL_BIND params[2] = { make_bind_in(rid), make_bind_in(uid) };
    hStmt.bind_param(params);
    hStmt.execute();
}

bool is_rid_banned(const std::string &rid) { return !rid.empty() && rid_set().contains(rid); }

void set_rid_banned(const std::string &rid, bool banned, const std::string &actor, const std::string &reason)
{
    if (rid.empty()) return;
    std::unordered_set<std::string> &set = rid_set();
    if (banned)
    {
        set.insert(rid);
        ::hStmt hStmt{ "INSERT INTO rid_ban (rid, reason, actor) VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE reason = VALUES(reason), actor = VALUES(actor)" };
        const std::string cut = reason.substr(0, 120);
        MYSQL_BIND params[3] = { make_bind_in(rid), make_bind_in(cut), make_bind_in(actor) };
        hStmt.bind_param(params);
        hStmt.execute();
        return;
    }
    set.erase(rid);
    ::hStmt hStmt{ "DELETE FROM rid_ban WHERE rid = ?" };
    MYSQL_BIND param = make_bind_in(rid);
    hStmt.bind_param(&param);
    hStmt.execute();
}

std::string rid_ban_reason(const std::string &rid)
{
    std::vector<std::string> r = select_col("SELECT reason FROM rid_ban WHERE rid = ?", rid);
    return r.empty() ? "" : r.front();
}

std::string rid_of(const std::string &growid)
{
    std::vector<std::string> r = select_col("SELECT rid FROM peer WHERE growid = ?", growid);
    return r.empty() ? "" : r.front();
}

std::vector<std::string> accounts_by_rid(const std::string &rid)
{
    if (rid.empty()) return {};
    return select_col("SELECT growid FROM peer WHERE rid = ?", rid);
}
