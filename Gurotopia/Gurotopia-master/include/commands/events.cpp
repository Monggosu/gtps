#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/events.hpp"
#include "tools/staff.hpp"

#include "events.hpp"

namespace
{
    constexpr double MAX_MULTIPLIER = 10.0;
    constexpr int MAX_MINUTES = 24 * 60;

    void start_or_stop(ENetEvent& event, const std::string_view text, bool xp)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        std::vector<std::string> argv = readch(std::string(text), ' ');
        const char *what = xp ? "XP" : "gems";

        if (argv.size() < 2)
        {
            on::ConsoleMessage(event.peer, std::format("Usage: /boost{} <`$multiplier 2-10`` | off> [`$minutes 1-1440, default 60``]", xp ? "xp" : "gem"));
            return;
        }
        if (argv[1] == "off" || argv[1] == "0" || argv[1] == "1")
        {
            set_boost(xp, 1.0, 0);
            staff_log("boost", what, pPeer->growid, "off");
            on::ConsoleMessage(event.peer, std::format("`2>> The {} boost is off.``", what));
            return;
        }

        double multiplier = 0.0;
        int minutes = 60;
        try
        {
            multiplier = std::stod(argv[1]);
            if (argv.size() > 2) minutes = std::stoi(argv[2]);
        }
        catch (const std::exception&) { multiplier = 0.0; }
        if (multiplier < 2.0 || multiplier > MAX_MULTIPLIER || minutes < 1 || minutes > MAX_MINUTES)
        {
            on::ConsoleMessage(event.peer, std::format("`4Multiplier must be 2-{:g} and minutes 1-{}.``", MAX_MULTIPLIER, MAX_MINUTES));
            return;
        }

        set_boost(xp, multiplier, minutes);
        staff_log("boost", what, pPeer->growid, std::format("x{:g} for {}m", multiplier, minutes));

        const std::string message = std::format("`4SYSTEM: `w{}`` started a `5x{:g} {}`` event for `w{}`` minutes!", pPeer->growid, multiplier, what, minutes);
        peers("", PeerSearch::EVERYONE, [&message](ENetPeer *p) { if (p->data) on::ConsoleMessage(p, message); });
    }
}

void boostgem(ENetEvent& event, const std::string_view text) { start_or_stop(event, text, false); }
void boostxp(ENetEvent& event, const std::string_view text)  { start_or_stop(event, text, true); }

void boost(ENetEvent& event, const std::string_view text)
{
    const long long gem_left = boost_seconds_left(false), xp_left = boost_seconds_left(true);
    if (gem_left == 0 && xp_left == 0)
    {
        on::ConsoleMessage(event.peer, "`6No boost event is running right now.``");
        return;
    }
    if (gem_left > 0) on::ConsoleMessage(event.peer, std::format("`2Gems x{:g}`` - `w{}`` left", gem_multiplier(), fmt_duration(gem_left)));
    if (xp_left > 0)  on::ConsoleMessage(event.peer, std::format("`2XP x{:g}`` - `w{}`` left", xp_multiplier(), fmt_duration(xp_left)));
}
