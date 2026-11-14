#include "pch.hpp"

#include <algorithm>

#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/economy.hpp"
#include "tools/nametag.hpp"
#include "tools/staff.hpp"

#include "moderation.hpp"
#include "basic.hpp"

namespace
{
    void say(ENetEvent &event, const std::string &message) { on::ConsoleMessage(event.peer, message); }

    void go_to(ENetEvent &event, const std::string &world_name)
    {
        send_varlist(event.peer, { "OnSetFreezeState", 1 });
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|Magically warping to world `5{}``...", world_name)});
        action::quit_to_exit(event, "", true);
        action::join_request(event, "", world_name);
    }
}

void ping(ENetEvent& event, const std::string_view text)
{
    say(event, std::format("`2Ping: `w{} ms``", event.peer->roundTripTime));
}

void online(ENetEvent& event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    int count = 0;
    std::string names;
    peers("", PeerSearch::EVERYONE, [&](ENetPeer *p)
    {
        const ::peer *d = static_cast<::peer*>(p->data);
        if (!d || d->growid.empty()) return;
        ++count;
        if (pPeer->role >= GUARDIAN && count <= 40) names += (names.empty() ? "" : ", ") + d->growid;
    });
    say(event, std::format("`2There {} `w{}`` player{} online.``", count == 1 ? "is" : "are", count, count == 1 ? "" : "s"));
    if (!names.empty()) say(event, std::format("`w{}``", names));
}

void worldinfo(ENetEvent& event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto it = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid == 0 || it == worlds.end())
    {
        say(event, "`4You are not in a world.``");
        return;
    }
    const ::world &world = *it;

    std::string owner = "nobody";
    if (world.owner > 0)
    {
        ::peer o{.user_id = world.owner};
        owner = o.mysql_select<std::string>("growid", "", true);
    }
    say(event, std::format("`6World `w{}``:`` owner `w{}``, `w{}`` here, {}", world.name, owner, world.visitors,
        world.is_public ? "`2public``" : (world.owner > 0 ? "`4locked``" : "`2unlocked``")));
    if (pPeer->role >= GUARDIAN) say(event, std::format("`o(staff) nuked: {}``", is_nuked(world.name) ? "yes" : "no"));
}

void status(ENetEvent& event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const title_def *title = pPeer->title_id > 0 ? title_by_id(pPeer->title_id) : nullptr;

    say(event, std::format("`6Status of `w{}``:``", pPeer->growid));
    say(event, std::format("Level `w{}`` (xp `w{}``) | Gems `w{}`` | Role `w{}``", pPeer->level.front(), pPeer->level.back(), format_gems(pPeer->gems), role_name(pPeer->role)));
    say(event, std::format("Daily streak `w{}`` day(s) | Daily reward: {} | Title: {}",
        (pPeer->daily_last >= game_day() - 1) ? pPeer->daily_streak : 0,
        pPeer->daily_last >= game_day() ? "`4claimed``" : "`2ready!`` /daily", title ? title->text : "none"));
}

void rules(ENetEvent& event, const std::string_view text)
{
    say(event, "`6Server rules:``");
    for (const char *line : SERVER_RULES) say(event, std::format("`w{}``", line));
}

void discord(ENetEvent& event, const std::string_view text)
{
    const std::string link = SERVER_DISCORD;
    say(event, link.empty() ? "`6The Discord link is not set yet.``" : std::format("`2Join our Discord: `w{}``", link));
}

void afk(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    pPeer->afk = !pPeer->afk;

    say(event, pPeer->afk ? "`6You are now AFK.`` Type /afk again when you are back." : "`2Welcome back!``");
    if (pPeer->netid == 0) return;

    const std::string bubble = pPeer->afk ? "`5[AFK]``" : "`2[Back]``";
    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&](ENetPeer *p)
    {
        send_varlist(p, { "OnTalkBubble", static_cast<u_int>(pPeer->netid), bubble, 1u });
    });
}

void back(ENetEvent& event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string &previous = pPeer->recent_worlds[pPeer->recent_worlds.size() - 2]; // @note back() is the world you are in now
    if (previous.empty() || previous == pPeer->recent_worlds.back())
    {
        say(event, "`6There is no previous world to go back to.``");
        return;
    }
    go_to(event, previous);
}

void hub(ENetEvent& event, const std::string_view text)
{
    go_to(event, HUB_WORLD);
}

/* @brief /zoom <value> [save] - guardian+: try camera values. administrators can "save" the value for staff on world entry (until restart). */
void zoom(ENetEvent& event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    float value = 0.0f;
    try { if (argv.size() > 1) value = std::stof(argv[1]); } catch (const std::exception&) { value = 0.0f; }
    if (argv.size() < 2 || value <= 0.0f)
    {
        say(event, "Usage: /zoom <`$value``> [save] - try different numbers to find a full-world camera. Current staff default: " + std::to_string(g_staff_zoom));
        return;
    }
    send_varlist(event.peer, { "OnZoomCamera", value, 500u });

    if (argv.size() > 2 && argv[2] == "save" && pPeer->role >= ADMINISTRATOR)
    {
        g_staff_zoom = value;
        say(event, std::format("`2>> Staff camera set to {} until restart.`` To keep it, set STAFF_CAMERA_ZOOM in tools/staff.hpp.", value));
    }
}
