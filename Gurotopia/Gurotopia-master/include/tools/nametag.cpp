#include "pch.hpp"

#include <algorithm>

#include "nametag.hpp"

namespace
{
    void load_titles(std::vector<title_def> &out)
    {
        out.clear();

        ::hStmt hStmt{ "SELECT id, tag, text, suffix, price, rule FROM title ORDER BY id" };
        signed id{}, suffix{};
        long long price{};
        std::string tag{}, text{}, rule{};
        u_long len_tag{}, len_text{}, len_rule{};

        MYSQL_BIND results[6] = {
            make_bind_out(id), make_bind_out(tag), make_bind_out(text),
            make_bind_out(suffix), make_bind_out(price), make_bind_out(rule)
        };
        results[1].length = &len_tag;
        results[2].length = &len_text;
        results[5].length = &len_rule;
        mysql_stmt_bind_result(hStmt.pStmt, results);
        hStmt.execute();

        while (mysql_stmt_fetch(hStmt.pStmt) == 0)
            out.push_back({ id, tag.substr(0, len_tag), text.substr(0, len_text), suffix != 0, price, rule.substr(0, len_rule) });
    }

    std::vector<title_def> &cache()
    {
        static std::vector<title_def> titles;
        static bool loaded = false;
        if (!loaded)
        {
            loaded = true;
            load_titles(titles);
        }
        return titles;
    }
}

const std::vector<title_def> &title_list() { return cache(); }

void title_reload() { load_titles(cache()); }

const title_def *title_by_id(int id)
{
    for (const title_def &t : cache()) if (t.id == id) return &t;
    return nullptr;
}

const title_def *title_by_tag(const std::string &tag)
{
    for (const title_def &t : cache()) if (t.tag == tag) return &t;
    return nullptr;
}

std::string build_display_name(const ::peer &peer, ::name_state state)
{
    const char color = (state == ::name_state::OWNER) ? '2' : (state == ::name_state::ACCESS) ? '^' : 'w';
    const std::string name = std::format("`{}{}``", color, peer.growid);

    const title_def *title = (peer.title_id > 0) ? title_by_id(peer.title_id) : nullptr;
    if (!title) return name;
    return title->suffix ? name + " " + title->text : title->text + " " + name;
}

::name_state name_state_in_world(const ::peer &peer)
{
    auto it = std::ranges::find(worlds, peer.recent_worlds.back(), &::world::name);
    if (it == worlds.end()) return ::name_state::NORMAL;
    if (it->owner > 0 && it->owner == peer.user_id) return ::name_state::OWNER;
    if (std::ranges::find(it->access, peer.user_id) != it->access.end()) return ::name_state::ACCESS;
    return ::name_state::NORMAL;
}

void refresh_name(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    pPeer->display_growid = build_display_name(*pPeer, name_state_in_world(*pPeer));

    if (pPeer->netid == 0) return; // @note not in a world
    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [pPeer](ENetPeer *p)
    {
        send_varlist(p, { "OnNameChanged", pPeer->display_growid }, pPeer->netid);
    });
}

int uid_of(const std::string &growid)
{
    ::hStmt hStmt{ "SELECT uid FROM peer WHERE growid = ? LIMIT 1" };
    MYSQL_BIND param = make_bind_in(growid);
    hStmt.bind_param(&param);

    signed uid{};
    MYSQL_BIND result = make_bind_out(uid);
    mysql_stmt_bind_result(hStmt.pStmt, &result);
    hStmt.execute();
    return (mysql_stmt_fetch(hStmt.pStmt) == 0) ? uid : 0;
}

std::vector<int> owned_titles(int uid)
{
    std::vector<int> out;
    ::hStmt hStmt{ "SELECT title_id FROM peer_title WHERE uid = ?" };
    MYSQL_BIND param = make_bind_in(uid);
    hStmt.bind_param(&param);

    signed id{};
    MYSQL_BIND result = make_bind_out(id);
    mysql_stmt_bind_result(hStmt.pStmt, &result);
    hStmt.execute();
    while (mysql_stmt_fetch(hStmt.pStmt) == 0) out.push_back(id);
    return out;
}

bool owns_title(int uid, int title_id)
{
    const std::vector<int> owned = owned_titles(uid);
    return std::ranges::find(owned, title_id) != owned.end();
}

void give_title(int uid, int title_id)
{
    ::hStmt hStmt{ "INSERT IGNORE INTO peer_title (uid, title_id) VALUES (?, ?)" };
    MYSQL_BIND params[2] = { make_bind_in(uid), make_bind_in(title_id) };
    hStmt.bind_param(params);
    hStmt.execute();
}

void take_title(int uid, int title_id)
{
    ::hStmt hStmt{ "DELETE FROM peer_title WHERE uid = ? AND title_id = ?" };
    MYSQL_BIND params[2] = { make_bind_in(uid), make_bind_in(title_id) };
    hStmt.bind_param(params);
    hStmt.execute();
}

int grant_auto_titles(const ::peer &peer)
{
    if (peer.user_id <= 0) return 0;

    const std::vector<int> owned = owned_titles(peer.user_id);
    int granted = 0;
    for (const title_def &title : cache())
    {
        const std::size_t colon = title.rule.find(':');
        if (colon == std::string::npos) continue;
        if (std::ranges::find(owned, title.id) != owned.end()) continue;

        const std::string kind = title.rule.substr(0, colon);
        const int need = std::atoi(title.rule.c_str() + colon + 1);

        bool ok = false;
        if      (kind == "level")  ok = peer.level.front() >= need;
        else if (kind == "streak") ok = peer.daily_streak >= need;
        else if (kind == "role")   ok = peer.role >= need;

        if (ok) { give_title(peer.user_id, title.id); ++granted; }
    }
    return granted;
}

std::string rule_hint(const std::string &rule)
{
    const std::size_t colon = rule.find(':');
    if (colon == std::string::npos) return "";
    const std::string kind = rule.substr(0, colon);
    const std::string need = rule.substr(colon + 1);
    if (kind == "level")  return "reach level " + need;
    if (kind == "streak") return "claim /daily " + need + " days in a row";
    if (kind == "role")   return "VIP and above";
    return "";
}
