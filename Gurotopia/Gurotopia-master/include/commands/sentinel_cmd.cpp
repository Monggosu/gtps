#include "pch.hpp"

#include "action/dialog_return/sentinel_ui.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/sentinel.hpp"
#include "tools/staff.hpp"

#include "sentinel_cmd.hpp"

/* @brief /sentinel - the dashboard */
void sentinel_command(ENetEvent& event, const std::string_view text)
{
    sentinel_open(event);
}

/* @brief /why <growid> - what the security system saw about a player, in plain words */
void why(ENetEvent& event, const std::string_view text)
{
    std::vector<std::string> argv = readch(std::string(text), ' ');
    if (argv.size() < 2)
    {
        on::ConsoleMessage(event.peer, "Usage: /why <`$growid``>");
        return;
    }
    const std::vector<sentinel::entry> list = sentinel::recent(8, argv[1]);
    if (list.empty())
    {
        on::ConsoleMessage(event.peer, std::format("`2No records for `w{}``.`` The security system has seen nothing suspicious.", argv[1]));
        return;
    }
    const long long now = static_cast<long long>(std::time(nullptr));
    on::ConsoleMessage(event.peer, std::format("`6What the security system saw about `w{}``:", argv[1]));
    for (const sentinel::entry &e : list)
        on::ConsoleMessage(event.peer, std::format("{}{} ago: {} -> {} ({})``", e.proof ? "`4" : (e.shadow ? "`8" : "`6"),
            fmt_duration(now - e.ts), e.rule, e.action, e.detail));
}
