#include "pch.hpp"

#include <algorithm>

#include "onVariant/ConsoleMessage.hpp"
#include "tools/staff.hpp"

#include "nuke.hpp"

namespace
{
    constexpr std::size_t MAX_WORLDS_PER_COMMAND = 20;
    constexpr std::size_t MAX_LINES_SHOWN = 10; // @note chat lines per batch, the rest is summarised

    bool valid_world(const std::string &name) { return !name.empty() && name.size() <= 24 && isalnum(name); }
    bool protected_world(const std::string &name) { return name == "START" || name == HUB_WORLD; }

    std::vector<std::string> world_args(const std::vector<std::string> &argv)
    {
        std::vector<std::string> out;
        for (std::size_t i = 1; i < argv.size(); ++i)
        {
            if (argv[i].empty()) continue;
            std::string name = argv[i];
            for (char &c : name) c = std::toupper(static_cast<unsigned char>(c));
            if (std::ranges::find(out, name) == out.end()) out.push_back(name);
        }
        return out;
    }
}

/* @brief /nuke [world ...] - no argument = the world you are in. guardian: one world, moderator: up to 20 at once. */
void nuke(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> targets = world_args(readch(std::string(text), ' '));

    if (targets.empty())
    {
        if (pPeer->netid == 0 || pPeer->recent_worlds.back().empty())
        {
            on::ConsoleMessage(event.peer, "Usage: /nuke [`$world`` ...] - nukes the world you are in when no world is given.");
            return;
        }
        targets.push_back(pPeer->recent_worlds.back());
    }
    if (targets.size() > 1 && pPeer->role < MODERATOR)
    {
        on::ConsoleMessage(event.peer, "`4Only moderators can nuke several worlds at once.``");
        return;
    }
    if (targets.size() > MAX_WORLDS_PER_COMMAND)
    {
        on::ConsoleMessage(event.peer, std::format("`4Too many worlds. Max {} per command.``", MAX_WORLDS_PER_COMMAND));
        return;
    }

    std::vector<std::string> done;
    for (const std::string &world : targets)
    {
        if (!valid_world(world)) { on::ConsoleMessage(event.peer, std::format("`4{}: invalid world name.``", world)); continue; }
        if (protected_world(world)) { on::ConsoleMessage(event.peer, std::format("`4{} is protected and can't be nuked.``", world)); continue; }
        if (!set_nuked(world, true, pPeer->growid)) { on::ConsoleMessage(event.peer, std::format("`6{} is already nuked.``", world)); continue; }

        evict_world(event, world, "`4This world was nuked by the staff.``");
        staff_log("nuke", world, pPeer->growid, "nuke");
        done.push_back(world);
    }
    if (done.empty()) return;

    /* @note one line per world (first 10), then a summary: same look as the console of a big server */
    peers("", PeerSearch::EVERYONE, [&done](ENetPeer *p)
    {
        if (!p->data) return;
        for (std::size_t i = 0; i < done.size() && i < MAX_LINES_SHOWN; ++i)
            on::ConsoleMessage(p, std::format("`4{}`` was nuked by the staff. Play fair and keep it clean, everyone!", done[i]));
        if (done.size() > MAX_LINES_SHOWN)
            on::ConsoleMessage(p, std::format("`4{}`` more worlds were nuked by the staff. Play fair and keep it clean, everyone!", done.size() - MAX_LINES_SHOWN));
    });
}

void unnuke(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> targets = world_args(readch(std::string(text), ' '));
    if (targets.empty())
    {
        on::ConsoleMessage(event.peer, "Usage: /unnuke <`$world`` ...>");
        return;
    }
    if (targets.size() > MAX_WORLDS_PER_COMMAND) targets.resize(MAX_WORLDS_PER_COMMAND);

    for (const std::string &world : targets)
    {
        if (!set_nuked(world, false, pPeer->growid))
        {
            on::ConsoleMessage(event.peer, std::format("`6{} is not nuked.``", world));
            continue;
        }
        staff_log("unnuke", world, pPeer->growid, "unnuke");
        on::ConsoleMessage(event.peer, std::format("`2>> {} is open again. Nothing inside was removed.``", world));
    }
}

void nukelist(ENetEvent& event, const std::string_view text)
{
    const std::vector<std::string> list = nuked_list(30);
    if (list.empty())
    {
        on::ConsoleMessage(event.peer, "`2No nuked worlds.``");
        return;
    }
    std::string line;
    for (const std::string &name : list) line += (line.empty() ? "" : ", ") + name;
    on::ConsoleMessage(event.peer, std::format("`6Nuked worlds (latest {}):`` {}", list.size(), line));
}
