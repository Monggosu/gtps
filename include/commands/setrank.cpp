#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"

#include "moderation.hpp"
#include "setrank.hpp"

void setrank(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 3)
    {
        send_gameMessage(event.peer, {.message =
            "action|log\nmsg|Usage: /setrank <`$growid``> <`$player|vip|guardian|mod|admin``> - "
            "Roles: player(0) < vip(1) < guardian(2) < mod(3) < admin(4)."});
        return;
    }

    const std::string target_id = argv[1];
    const int new_role = parse_role(argv[2]);
    if (new_role < 0)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4Unknown role. Use: player, vip, guardian, mod, or admin.``"});
        return;
    }

    ::account_info account = fetch_account(target_id);
    if (!account.found)
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`4Account `{}`` does not exist.", target_id)});
        return;
    }
    if ((u_char)new_role == account.role)
    {
        send_gameMessage(event.peer, {.message = std::format(
            "action|log\nmsg|`6{}`` is already `{}``.", target_id, role_name((u_char)new_role))});
        return;
    }

    const int role_val = new_role;
    ::hStmt hStmt{ "UPDATE peer SET role = ? WHERE growid = ?" };
    MYSQL_BIND params[2] = { make_bind_in(role_val), make_bind_in(target_id) };
    hStmt.bind_param(params);
    hStmt.execute();

    ::online_target target = find_online(target_id);
    if (target.data) target.data->role = (u_char)new_role;

    mod_log("setrank", target_id, pPeer->growid,
        std::format("{} -> {}", role_name(account.role), role_name((u_char)new_role)));

    send_gameMessage(event.peer, {.message = std::format(
        "action|log\nmsg|`2>> Set `{}`` role to `{}`` (was `{}``).",
        target_id, role_name((u_char)new_role), role_name(account.role))});

    if (target.enet)
    {
        send_gameMessage(target.enet, {.message = std::format(
            "action|log\nmsg|`2Your role is now `{}``.`", role_name((u_char)new_role))});
    }
}
