#include "pch.hpp"

#include "tools/sentinel.hpp"

#include <ctime>

#include "commands/__command.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/time.hpp"
#include "tools/random.hpp"

#include "input.hpp"

#include <algorithm>

void action::input(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    ::hPipe hPipe{ header };
    std::string text = hPipe["text"];

    if (text.empty() || text.front() == '\r' || std::ranges::all_of(text, ::isspace)) return;
    
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), ::isspace));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), ::isspace).base(), text.end());
    if (text.empty()) return; // @note we recheck if empty since we did trimming.

    // @note mute blocks chat and broadcast commands, other commands still work
    const int now_mute = (int)std::time(nullptr);
    if (pPeer->muted_until > now_mute)
    {
        const std::string_view cmd_peek = text.starts_with('/')
            ? std::string_view(text).substr(0, text.find(' '))
            : std::string_view{};
        if (cmd_peek.empty() || cmd_peek == "/msg" || cmd_peek == "/sb")
        {
            const int mins_left = (pPeer->muted_until - now_mute + 59) / 60;
            send_gameMessage(event.peer, {.message = std::format(
                "action|log\nmsg|`4You are muted for another {} minute(s).``", mins_left)});
            return;
        }
    }

    const u_int now = ticks();
    pPeer->messages.push_back(now);
    if (pPeer->messages.size() > 5) pPeer->messages.pop_front();
    if (pPeer->messages.size() == 5 && now - pPeer->messages.front() <= 5)
    {
        on::ConsoleMessage(event.peer,
            "`6>>`4Spam detected! ``Please wait a bit before typing anything else.  "  
            "Please note, any form of bot/macro/auto-paste will get all your accounts banned, so don't do it!");
        sentinel::spam(event);
    }
    else if (text.starts_with('/')) 
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`6{}``", text)});

        std::string_view command = text;
        command = command.substr(0, command.find(' '));

        if (auto it = cmd_pool.find(command); it != cmd_pool.end())
        {
            if (pPeer->role < it->second.min_role)
                send_gameMessage(event.peer, {.message = std::format(
                    "action|log\nmsg|`4You need role `{}`` to use `{}``. Your role: `{}``",
                    role_name(it->second.min_role), command, role_name(pPeer->role))});
            else
                it->second.fn(event, std::move(text));
        }
        else 
            send_gameMessage(event.peer, {.message = "action|log\nmsg|`4Unknown command.`` Enter `$/?`` for a list of valid commands."});
    }
    else 
    {
        if (pPeer->state & S_DUCT_TAPE)
        {
            for (char &c : text)
                if (c != ' ') c = (RandomRange(1, 3) == 1) ? 'm' : 'f';
        }
        const std::string &player_chat = std::format("CP:0_PL:0_OID:_player_chat={}", text);
        const std::string &message = std::format("CP:0_PL:0_OID:_CT:[W]_ `6<{}>`` `$`${}````", pPeer->display_growid, text);
        peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&event, &pPeer, player_chat, message](ENetPeer *p) 
        {
            send_varlist(p, { "OnTalkBubble", pPeer->netid, player_chat, 0u });
            on::ConsoleMessage(p, message, pPeer->netid);
        });
    }
}
