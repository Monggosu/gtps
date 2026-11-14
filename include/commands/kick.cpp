#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"

#include "moderation.hpp"
#include "kick.hpp"

void kick(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 2)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|Usage: /kick <`$growid``> [`$reason``] - Kick a player from the server."});
        return;
    }

    const std::string target_id = argv[1];
    const std::string reason = (argv.size() > 2) ? join_args(argv, 2) : "kicked by staff";

    if (target_id == pPeer->growid)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4You cannot kick yourself.``"});
        return;
    }

    ::online_target target = find_online(target_id);
    if (!target.enet)
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`4{}`` is not online.", target_id)});
        return;
    }

    if (pPeer->role < ADMINISTRATOR && target.data->role >= pPeer->role)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4You cannot kick someone with an equal or higher role.``"});
        return;
    }

    mod_log("kick", target_id, pPeer->growid, reason);

    send_gameMessage(target.enet, {.message = std::format(
        "action|log\nmsg|`4You have been kicked by `{}``. Reason: `{}```", pPeer->display_growid, reason)});
    send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`2>> Kicked `{}``.``", target_id)});

    enet_peer_disconnect_later(target.enet, 0);
}
