#include "pch.hpp"

#include <unordered_map>

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/economy.hpp"
#include "tools/staff.hpp"

#include "moderation.hpp"
#include "redeem.hpp"

namespace
{
    constexpr int MAX_FAILS = 5;                 // @note wrong codes allowed...
    constexpr long long FAIL_WINDOW = 10 * 60;   // @note ...before the player is blocked for this many seconds

    std::string normalize(std::string code)
    {
        for (char &c : code) c = std::toupper(static_cast<unsigned char>(c));
        return code;
    }
    bool code_ok(const std::string &code) { return code.size() >= 3 && code.size() <= 24 && isalnum(code); }

    long long affected(::hStmt &hStmt) { return static_cast<long long>(mysql_stmt_affected_rows(hStmt.pStmt)); }
}

/* @brief /redeem <code> - one use per account, limited total uses, optional expiry. gems come from the economy as a faucet (logged). */
void redeem(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');
    if (argv.size() < 2) { on::ConsoleMessage(event.peer, "Usage: /redeem <`$code``>"); return; }

    static std::unordered_map<int, std::pair<int, long long>> fails; // @note user id -> {wrong tries, first wrong try}
    const long long now = static_cast<long long>(std::time(nullptr));
    auto &f = fails[pPeer->user_id];
    if (now - f.second > FAIL_WINDOW) f = {0, now};
    if (f.first >= MAX_FAILS)
    {
        on::ConsoleMessage(event.peer, std::format("`4Too many wrong codes.`` Try again in `w{}``.", fmt_duration(FAIL_WINDOW - (now - f.second))));
        return;
    }

    const std::string code = normalize(argv[1]);
    auto wrong = [&](const std::string &why) { ++f.first; on::ConsoleMessage(event.peer, "`4" + why + "``"); };
    if (!code_ok(code)) { wrong("Invalid code."); return; }

    long long gems{}, expires{};
    signed max_uses{}, used{};
    {
        ::hStmt hStmt{ "SELECT gems, max_uses, used, expires FROM redeem_code WHERE code = ?" };
        MYSQL_BIND param = make_bind_in(code);
        hStmt.bind_param(&param);
        MYSQL_BIND results[4] = { make_bind_out(gems), make_bind_out(max_uses), make_bind_out(used), make_bind_out(expires) };
        mysql_stmt_bind_result(hStmt.pStmt, results);
        hStmt.execute();
        if (mysql_stmt_fetch(hStmt.pStmt) != 0) { wrong("Invalid code."); return; }
    }
    if (expires > 0 && now > expires) { on::ConsoleMessage(event.peer, "`4That code has expired.``"); return; }
    if (used >= max_uses) { on::ConsoleMessage(event.peer, "`4That code has been fully redeemed.``"); return; }

    /* @note 1) one use per account, 2) take one of the limited uses. both are single statements, so two players can't both take the last use. */
    {
        ::hStmt hStmt{ "INSERT IGNORE INTO redeem_used (code, uid) VALUES (?, ?)" };
        MYSQL_BIND params[2] = { make_bind_in(code), make_bind_in(pPeer->user_id) };
        hStmt.bind_param(params);
        hStmt.execute();
        if (affected(hStmt) == 0) { on::ConsoleMessage(event.peer, "`6You already redeemed that code.``"); return; }
    }
    {
        ::hStmt hStmt{ "UPDATE redeem_code SET used = used + 1 WHERE code = ? AND used < max_uses" };
        MYSQL_BIND param = make_bind_in(code);
        hStmt.bind_param(&param);
        hStmt.execute();
        if (affected(hStmt) == 0)
        {
            ::hStmt undo{ "DELETE FROM redeem_used WHERE code = ? AND uid = ?" };
            MYSQL_BIND params[2] = { make_bind_in(code), make_bind_in(pPeer->user_id) };
            undo.bind_param(params);
            undo.execute();
            on::ConsoleMessage(event.peer, "`4That code has been fully redeemed.``");
            return;
        }
    }

    pPeer->gems += gems;
    pPeer->mysql_update("gems", pPeer->gems);
    log_economy("redeem", 0, pPeer->user_id, gems, 0, code);
    on::SetBux(event);
    on::ConsoleMessage(event.peer, std::format("`2Code redeemed!`` You received `w{}`` gems.", format_gems(gems)));
}

/* @brief /addredeem <code> <gems> <max uses> [minutes valid] */
void addredeem(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');
    if (argv.size() < 4)
    {
        on::ConsoleMessage(event.peer, "Usage: /addredeem <`$code``> <`$gems``> <`$max uses``> [`$minutes valid, default: no expiry``]");
        return;
    }
    const std::string code = normalize(argv[1]);
    long long gems = 0, minutes = 0;
    int uses = 0;
    try
    {
        gems = std::stoll(argv[2]);
        uses = std::stoi(argv[3]);
        if (argv.size() > 4) minutes = std::stoll(argv[4]);
    }
    catch (const std::exception&) { gems = 0; }
    if (!code_ok(code) || gems < 1 || gems > 1'000'000'000 || uses < 1 || uses > 100000 || minutes < 0 || minutes > 525600)
    {
        on::ConsoleMessage(event.peer, "`4Code: 3-24 letters/numbers. Gems 1-1,000,000,000. Uses 1-100000. Minutes 0-525600.``");
        return;
    }
    const long long expires = (minutes > 0) ? static_cast<long long>(std::time(nullptr)) + minutes * 60 : 0;

    ::hStmt hStmt{ "INSERT IGNORE INTO redeem_code (code, gems, max_uses, used, expires, created_by) VALUES (?, ?, ?, 0, ?, ?)" };
    MYSQL_BIND params[5] = { make_bind_in(code), make_bind_in(gems), make_bind_in(uses), make_bind_in(expires), make_bind_in(pPeer->growid) };
    hStmt.bind_param(params);
    hStmt.execute();
    if (affected(hStmt) == 0) { on::ConsoleMessage(event.peer, "`4That code already exists.`` Use /delredeem first.``"); return; }

    staff_log("addredeem", code, pPeer->growid, std::format("{} gems x{}", gems, uses));
    on::ConsoleMessage(event.peer, std::format("`2>> Code `w{}`` created: `w{}`` gems, `w{}`` use(s){}.``", code, format_gems(gems), uses,
        minutes > 0 ? std::format(", valid `w{}``", fmt_duration(minutes * 60)) : ""));
}

void delredeem(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');
    if (argv.size() < 2) { on::ConsoleMessage(event.peer, "Usage: /delredeem <`$code``>"); return; }

    const std::string code = normalize(argv[1]);
    ::hStmt hStmt{ "DELETE FROM redeem_code WHERE code = ?" };
    MYSQL_BIND param = make_bind_in(code);
    hStmt.bind_param(&param);
    hStmt.execute();
    if (affected(hStmt) == 0) { on::ConsoleMessage(event.peer, "`4No such code.``"); return; }

    ::hStmt clean{ "DELETE FROM redeem_used WHERE code = ?" };
    MYSQL_BIND p2 = make_bind_in(code);
    clean.bind_param(&p2);
    clean.execute();

    staff_log("delredeem", code, pPeer->growid, "deleted");
    on::ConsoleMessage(event.peer, std::format("`2>> Code `w{}`` deleted.``", code));
}

void redeemlist(ENetEvent& event, const std::string_view text)
{
    ::hStmt hStmt{ "SELECT code, gems, used, max_uses, expires FROM redeem_code ORDER BY code LIMIT 20" };
    std::string code{};
    long long gems{}, expires{};
    signed used{}, max_uses{};
    u_long length{};
    MYSQL_BIND results[5] = { make_bind_out(code), make_bind_out(gems), make_bind_out(used), make_bind_out(max_uses), make_bind_out(expires) };
    results[0].length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, results);
    hStmt.execute();

    const long long now = static_cast<long long>(std::time(nullptr));
    int shown = 0;
    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        ++shown;
        on::ConsoleMessage(event.peer, std::format("`w{}``: {} gems, used {}/{}{}", code.substr(0, length), format_gems(gems), used, max_uses,
            expires > 0 ? (now > expires ? " `4(expired)``" : " (expires in " + fmt_duration(expires - now) + ")") : ""));
    }
    if (shown == 0) on::ConsoleMessage(event.peer, "`6No redeem codes.``");
}
