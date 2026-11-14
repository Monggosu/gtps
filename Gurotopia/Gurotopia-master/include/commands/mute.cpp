#include "pch.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

#include "onVariant/ConsoleMessage.hpp"

#include "moderation.hpp"
#include "mute.hpp"

static bool all_digits(const std::string &s)
{
    if (s.empty()) return false;
    return std::ranges::all_of(s, [](unsigned char c) { return std::isdigit(c); });
}

void mute(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 3)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|Usage: /mute <`$growid``> [`$minutes``] <`$reason``> - Default mute is 10 minutes. Reason is required."});
        return;
    }

    const std::string target_id = argv[1];
    int minutes = 10;
    std::size_t reason_from = 2;
    if (argv.size() > 2 && all_digits(argv[2]))
    {
        minutes = std::atoi(argv[2].c_str());
        reason_from = 3;
    }
    if (minutes <= 0) minutes = 10;
    if (minutes > 60 * 24 * 30) minutes = 60 * 24 * 30; // @note cap 30 days

    const std::string reason = join_args(argv, reason_from);
    if (!reason_ok(reason, 5))
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4A clear reason (at least 5 characters) is required to mute.``"});
        return;
    }

    if (target_id == pPeer->growid)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4You cannot mute yourself.``"});
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
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4You cannot mute someone with an equal or higher role.``"});
        return;
    }

    const int until = (int)(std::time(nullptr)) + minutes * 60;

    ::hStmt hStmt{ "UPDATE peer SET muted_until = ? WHERE growid = ?" };
    MYSQL_BIND params[2] = { make_bind_in(until), make_bind_in(target_id) };
    hStmt.bind_param(params);
    hStmt.execute();

    ::online_target target = find_online(target_id);
    if (target.data) target.data->muted_until = until;

    mod_log("mute", target_id, pPeer->growid, std::format("{} min - {}", minutes, reason));

    send_gameMessage(event.peer, {.message = std::format(
        "action|log\nmsg|`2>> Muted `{}`` for {} minute(s). Reason: `{}```", target_id, minutes, reason)});

    if (target.enet)
    {
        send_gameMessage(target.enet, {.message = std::format(
            "action|log\nmsg|`4You have been muted for {} minute(s) by `{}``. Reason: `{}```",
            minutes, pPeer->display_growid, reason)});
    }
}

void unmute(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 2)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|Usage: /unmute <`$growid``>"});
        return;
    }

    const std::string target_id = argv[1];
    ::account_info account = fetch_account(target_id);
    if (!account.found)
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`4Account `{}`` does not exist.", target_id)});
        return;
    }
    if (account.muted_until <= (int)std::time(nullptr))
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`6{}`` is not muted.", target_id)});
        return;
    }

    ::hStmt hStmt{ "UPDATE peer SET muted_until = 0 WHERE growid = ?" };
    MYSQL_BIND param = make_bind_in(target_id);
    hStmt.bind_param(&param);
    hStmt.execute();

    ::online_target target = find_online(target_id);
    if (target.data) target.data->muted_until = 0;

    mod_log("unmute", target_id, pPeer->growid, "unmuted");

    send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`2>> Unmuted `{}``.``", target_id)});

    if (target.enet)
        send_gameMessage(target.enet, {.message = "action|log\nmsg|`2You are no longer muted.``"});
}
