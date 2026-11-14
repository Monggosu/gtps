#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/economy.hpp"

#include "top.hpp"

void top(ENetEvent& event, const std::string_view text)
{
    const bool by_level = text.find("level") != std::string_view::npos;

    if (by_level) // @note level/xp are only saved on logout, so save online players first to keep the board accurate
        peers("", PeerSearch::EVERYONE, [](ENetPeer *p)
        {
            if (p->data) static_cast<::peer*>(p->data)->mysql_update_all();
        });

    // @note gems are written to the database on every change (pay, buy, pickup), so no flush is needed for gems
    ::hStmt hStmt{ by_level ?
        "SELECT growid, level FROM peer ORDER BY level DESC, xp DESC LIMIT 10" :
        "SELECT growid, gems FROM peer ORDER BY gems DESC LIMIT 10" };

    std::string name{};
    long long value{};
    u_long length = 0;
    MYSQL_BIND results[2] = { make_bind_out(name), make_bind_out(value) };
    results[0].length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, results);
    hStmt.execute();

    on::ConsoleMessage(event.peer, by_level ? "`6Top 10 by level:``" : "`6Top 10 richest players:``");
    for (int rank = 1; mysql_stmt_fetch(hStmt.pStmt) == 0; ++rank)
    {
        std::string growid = name.substr(0, std::min<std::size_t>(length, name.size()));
        on::ConsoleMessage(event.peer, std::format("`w{}.`` {} - `2{}``", rank, growid, by_level ? std::to_string(value) : format_gems(value)));
    }
}
