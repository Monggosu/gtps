#include "pch.hpp"

#include "tools/sentinel.hpp"

#include "onVariant/ConsoleMessage.hpp"

#include "moderation.hpp"
#include "ban.hpp"

void ban(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 3)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|Usage: /ban <`$growid``> <`$reason (min 10 chars)``> - Permanent until /unban. Mod: 5 bans/day, Admin: unlimited."});
        return;
    }

    const std::string target_id = argv[1];
    const std::string reason = join_args(argv, 2);

    if (!reason_ok(reason, 10))
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4A clear reason of at least 10 characters is required to ban.``"});
        return;
    }

    if (target_id == pPeer->growid)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4You cannot ban yourself.``"});
        return;
    }

    ::account_info account = fetch_account(target_id);
    if (!account.found)
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`4Account `{}`` does not exist.", target_id)});
        return;
    }
    if (pPeer->role < ADMINISTRATOR && account.role >= pPeer->role)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4You cannot ban someone with an equal or higher role.``"});
        return;
    }
    if (!account.ban_reason.empty())
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`6{}`` is already banned.``", target_id)});
        return;
    }

    if (pPeer->role < ADMINISTRATOR)
    {
        const int limit = (pPeer->role >= MODERATOR) ? 5 : 3;
        const int used = mod_bans_today(pPeer->growid);
        if (used >= limit)
        {
            send_gameMessage(event.peer, {.message = std::format(
                "action|log\nmsg|`4Ban limit reached for today ({}/{}).``", used, limit)});
            return;
        }
    }

    ::hStmt hStmt{ "UPDATE peer SET ban_reason = ? WHERE growid = ?" };
    MYSQL_BIND params[2] = { make_bind_in(reason), make_bind_in(target_id) };
    hStmt.bind_param(params);
    hStmt.execute();

    mod_log("ban", target_id, pPeer->growid, reason);

    send_gameMessage(event.peer, {.message = std::format(
        "action|log\nmsg|`2>> Banned `{}``. Reason: `{}```", target_id, reason)});

    ::online_target target = find_online(target_id);
    if (target.enet)
    {
        send_gameMessage(target.enet, {.message = std::format(
            "action|log\nmsg|`4You are banned.`` Reason: `{}``", reason)});
        enet_peer_disconnect_later(target.enet, 0);
    }
}

void unban(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 2)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|Usage: /unban <`$growid``>"});
        return;
    }

    const std::string target_id = argv[1];
    ::account_info account = fetch_account(target_id);
    if (!account.found)
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`4Account `{}`` does not exist.", target_id)});
        return;
    }
    sentinel::forgive(target_id); // @note also clears a temporary ban from the security system
    if (account.ban_reason.empty())
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`6{}`` is not banned.", target_id)});
        return;
    }

    ::hStmt hStmt{ "UPDATE peer SET ban_reason = '' WHERE growid = ?" };
    MYSQL_BIND param = make_bind_in(target_id);
    hStmt.bind_param(&param);
    hStmt.execute();

    ::online_target target = find_online(target_id);
    if (target.data) target.data->ban_reason.clear();

    mod_log("unban", target_id, pPeer->growid, "unbanned");

    send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`2>> Unbanned `{}``.``", target_id)});
}
