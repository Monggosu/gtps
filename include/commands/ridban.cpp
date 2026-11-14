#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/staff.hpp"

#include "moderation.hpp"
#include "ridban.hpp"

/* @brief /banrid <growid> <reason> - bans the device (RID) of an account, and every account that was ever used on that device. */
void banrid(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 3)
    {
        on::ConsoleMessage(event.peer, "Usage: /banrid <`$growid``> <`$reason (min 10 chars)``> - bans the device and all accounts on it.");
        return;
    }
    const std::string target_id = argv[1];
    const std::string reason = join_args(argv, 2);
    if (!reason_ok(reason, 10))
    {
        on::ConsoleMessage(event.peer, "`4A clear reason of at least 10 characters is required.``");
        return;
    }

    ::online_target online = find_online(target_id);
    const std::string rid = (online.data && !online.data->rid.empty()) ? online.data->rid : rid_of(target_id);
    if (rid.empty())
    {
        on::ConsoleMessage(event.peer, std::format("`4No device id known for `w{}``. They must log in once with a client that sends it.``", target_id));
        return;
    }

    const std::vector<std::string> accounts = accounts_by_rid(rid);
    for (const std::string &name : accounts)
    {
        const ::account_info info = fetch_account(name);
        if (pPeer->role < ADMINISTRATOR && info.found && info.role >= pPeer->role)
        {
            on::ConsoleMessage(event.peer, std::format("`4Account `w{}`` on this device has an equal or higher role. Ask an administrator.``", name));
            return;
        }
    }

    set_rid_banned(rid, true, pPeer->growid, reason);

    std::string listed;
    for (const std::string &name : accounts)
    {
        ::hStmt hStmt{ "UPDATE peer SET ban_reason = ? WHERE growid = ? AND ban_reason = ''" };
        MYSQL_BIND params[2] = { make_bind_in(reason), make_bind_in(name) };
        hStmt.bind_param(params);
        hStmt.execute();
        staff_log("banrid", name, pPeer->growid, reason);
        listed += (listed.empty() ? "" : ", ") + name;
    }

    peers("", PeerSearch::EVERYONE, [&rid, &reason](ENetPeer *p)
    {
        const ::peer *d = static_cast<::peer*>(p->data);
        if (!d || d->rid != rid) return;
        send_gameMessage(p, {.message = std::format("action|log\nmsg|`4You are banned.`` Reason: `{}``", reason)});
        enet_peer_disconnect_later(p, 0);
    });

    on::ConsoleMessage(event.peer, std::format("`2>> Device banned. {} account(s): `w{}``", accounts.size(), listed.empty() ? target_id : listed));
}

/* @brief /unbanrid <growid> - lifts the device ban and unbans that one account. Other accounts on the device stay banned until /unban. */
void unbanrid(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');
    if (argv.size() < 2)
    {
        on::ConsoleMessage(event.peer, "Usage: /unbanrid <`$growid``>");
        return;
    }
    const std::string rid = rid_of(argv[1]);
    if (rid.empty() || !is_rid_banned(rid))
    {
        on::ConsoleMessage(event.peer, std::format("`6{}'s device is not banned.``", argv[1]));
        return;
    }
    set_rid_banned(rid, false, pPeer->growid, "");

    ::hStmt hStmt{ "UPDATE peer SET ban_reason = '' WHERE growid = ?" };
    MYSQL_BIND param = make_bind_in(argv[1]);
    hStmt.bind_param(&param);
    hStmt.execute();

    staff_log("unbanrid", argv[1], pPeer->growid, "device unbanned");
    on::ConsoleMessage(event.peer, std::format("`2>> Device unbanned and `w{}`` unbanned. Other accounts on it stay banned until /unban.``", argv[1]));
}

/* @brief /alts <growid> - other accounts used on the same device */
void alts(ENetEvent& event, const std::string_view text)
{
    std::vector<std::string> argv = readch(std::string(text), ' ');
    if (argv.size() < 2)
    {
        on::ConsoleMessage(event.peer, "Usage: /alts <`$growid``>");
        return;
    }
    ::online_target online = find_online(argv[1]);
    const std::string rid = (online.data && !online.data->rid.empty()) ? online.data->rid : rid_of(argv[1]);
    if (rid.empty())
    {
        on::ConsoleMessage(event.peer, std::format("`4No device id known for `w{}``.``", argv[1]));
        return;
    }
    std::string listed;
    int count = 0;
    for (const std::string &name : accounts_by_rid(rid))
    {
        if (name == argv[1]) continue;
        if (++count > 25) break;
        listed += (listed.empty() ? "" : ", ") + name;
    }
    on::ConsoleMessage(event.peer, listed.empty() ?
        std::format("`2{} has no other accounts on this device.``", argv[1]) :
        std::format("`6Accounts on the same device as `w{}``:`` {}{}", argv[1], listed, is_rid_banned(rid) ? " `4(device is banned)``" : ""));
}
