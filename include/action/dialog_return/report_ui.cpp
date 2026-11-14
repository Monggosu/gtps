#include "pch.hpp"

#include <algorithm>
#include <unordered_map>

#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "commands/moderation.hpp"
#include "commands/nuke.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "tools/staff.hpp"

#include "report_ui.hpp"

namespace
{
    constexpr int REPORTS_PER_DAY = 5;
    constexpr long long REPORT_COOLDOWN = 60;   // @note seconds between two reports of the same player
    constexpr int MUTE_MINUTES = 30;            // @note the "Mute" button in /reports
    enum status : int { OPEN = 0, HANDLED = 1, DISMISSED = 2 };

    create_dialog themed() // @note dark red window instead of the default blue
    {
        create_dialog dialog;
        dialog.set_bg_color(74, 22, 28, 228).set_border_color(255, 128, 128, 255).set_default_color("`o");
        return dialog;
    }

    std::string category_label(const std::string &c)
    {
        if (c == "bot")   return "Bot / auto script";
        if (c == "cheat") return "Cheat / hack";
        if (c == "scam")  return "Scam";
        if (c == "chat")  return "Abusive chat";
        return "Other";
    }

    bool name_ok(const std::string &s) { return !s.empty() && s.size() <= 18 && isalnum(s); }

    long long count_query(const std::string &query, const std::string &a, const std::string &b = "")
    {
        ::hStmt hStmt{ query };
        MYSQL_BIND params[2] = { make_bind_in(a), make_bind_in(b) };
        hStmt.bind_param(params);

        long long count{};
        u_long length{};
        MYSQL_BIND result = make_bind_out(count);
        result.length = &length;
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        hStmt.fetch();
        return count;
    }

    std::string clean_note(std::string note)
    {
        std::erase_if(note, [](unsigned char c) { return c < 32 || c == '|' || c == '`'; });
        if (note.size() > 100) note.resize(100);
        return note;
    }

    /* @return the world a player is in right now, "" if offline / not in a world */
    std::string world_of(const ::online_target &t)
    {
        return (t.data && t.data->netid != 0) ? t.data->recent_worlds.back() : "";
    }

    bool can_act_on(const ::peer *actor, const std::string &target)
    {
        if (actor->role >= ADMINISTRATOR) return true;
        const ::account_info info = fetch_account(target);
        return info.found && info.role < actor->role;
    }

    void close_reports(const std::string &target, int new_status, const std::string &actor)
    {
        ::hStmt hStmt{ "UPDATE report SET status = ?, handled_by = ?, handled_at = NOW() WHERE status = 0 AND target = ?" };
        MYSQL_BIND params[3] = { make_bind_in(new_status), make_bind_in(actor), make_bind_in(target) };
        hStmt.bind_param(params);
        hStmt.execute();
    }

    /* @brief tell the players who reported `target` (and are online) that staff looked at it */
    void thank_reporters(const std::string &target)
    {
        std::vector<std::string> names;
        {
            ::hStmt hStmt{ "SELECT DISTINCT reporter FROM report WHERE target = ? AND status = 1 AND handled_at >= NOW() - INTERVAL 1 MINUTE" };
            MYSQL_BIND param = make_bind_in(target);
            hStmt.bind_param(&param);
            std::string name{};
            u_long length{};
            MYSQL_BIND result = make_bind_out(name);
            result.length = &length;
            mysql_stmt_bind_result(hStmt.pStmt, &result);
            hStmt.execute();
            while (mysql_stmt_fetch(hStmt.pStmt) == 0) names.push_back(name.substr(0, length));
        }
        for (const std::string &name : names)
        {
            ::online_target t = find_online(name);
            if (t.enet) on::ConsoleMessage(t.enet, std::format("`2Thanks!`` Staff reviewed your report about `w{}``.", target));
        }
    }

    struct row
    {
        int id{};
        std::string reporter{}, category{}, note{}, target_world{}, reporter_world{};
        long long created{};
    };

    std::vector<row> open_rows(const std::string &target, int limit)
    {
        std::vector<row> rows;
        ::hStmt hStmt{ "SELECT id, reporter, category, note, target_world, reporter_world, CAST(UNIX_TIMESTAMP(created_at) AS SIGNED) FROM report WHERE status = 0 AND target = ? ORDER BY id DESC LIMIT 8" };
        MYSQL_BIND param = make_bind_in(target);
        hStmt.bind_param(&param);

        signed id{};
        long long created{};
        std::string reporter{}, category{}, note{}, tworld{}, rworld{};
        u_long l[5]{};
        MYSQL_BIND results[7] = {
            make_bind_out(id), make_bind_out(reporter), make_bind_out(category), make_bind_out(note),
            make_bind_out(tworld), make_bind_out(rworld), make_bind_out(created)
        };
        for (int i = 0; i < 5; ++i) results[i + 1].length = &l[i];
        mysql_stmt_bind_result(hStmt.pStmt, results);
        hStmt.execute();

        while (mysql_stmt_fetch(hStmt.pStmt) == 0 && static_cast<int>(rows.size()) < limit)
            rows.push_back({ id, reporter.substr(0, l[0]), category.substr(0, l[1]), note.substr(0, l[2]), tworld.substr(0, l[3]), rworld.substr(0, l[4]), created });
        return rows;
    }

    void detail(ENetEvent& event, const std::string &target, const std::string &notice = "")
    {
        const std::vector<row> rows = open_rows(target, 8);
        if (rows.empty()) { reports_open(event, notice.empty() ? "`2No open reports left for that player.``" : notice); return; }

        const ::online_target online = find_online(target);
        const std::string world = world_of(online);

        create_dialog dialog = themed();
        dialog.add_label_with_icon("big", std::format("`w{}``", target), 32).add_spacer("small");
        if (!notice.empty()) dialog.add_textbox(notice).add_spacer("small");
        dialog.add_textbox(std::format("`w{}`` open report(s). Player is {}.", rows.size(),
            online.enet ? (world.empty() ? "`2online`` (in the menu)" : "`2online`` in world `w" + world + "``") : "`4offline``"));
        dialog.add_spacer("small");

        const long long now = static_cast<long long>(std::time(nullptr));
        for (const row &r : rows)
        {
            dialog.add_smalltext(std::format("`w#{} {}`` - {} - {} ago - from world `w{}``",
                r.id, r.reporter, category_label(r.category), fmt_duration(now - r.created), r.reporter_world.empty() ? "?" : r.reporter_world));
            if (!r.note.empty()) dialog.add_smalltext(std::format("`8\"{}\"``", r.note));
        }
        dialog.add_spacer("small")
              .add_button("goto_" + target, "`wGo to world``")
              .add_button("mute_" + target, std::format("`6Mute {} min``", MUTE_MINUTES))
              .add_button("kick_" + target, "`6Kick``")
              .add_button("nuke_" + target, "`4Nuke their world``")
              .add_button("done_" + target, "`2Mark handled``")
              .add_button("dismiss_" + target, "`8Dismiss (false report)``")
              .add_button("back", "`wBack``");

        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("reports", "Close", "") });
    }
}

/* ===================== /lapor ===================== */

void lapor_open(ENetEvent& event, const std::string &target)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (!name_ok(target)) { on::ConsoleMessage(event.peer, "Usage: /lapor <`$growid``>"); return; }
    if (target == pPeer->growid) { on::ConsoleMessage(event.peer, "`4You can't report yourself.``"); return; }
    if (!fetch_account(target).found) { on::ConsoleMessage(event.peer, std::format("`4No player named `w{}``.``", target)); return; }

    create_dialog dialog = themed();
    dialog.add_label_with_icon("big", "`wReport a player``", 32).add_spacer("small")
          .add_textbox(std::format("You are reporting `w{}``. Staff will see your name, the time and the world you are in. False reports can get you muted.", target))
          .add_spacer("small")
          .add_text_input("note", "Details (optional):", "", 100)
          .embed_data("target", target)
          .add_spacer("small")
          .add_button("cat_bot", "Bot / auto script")
          .add_button("cat_cheat", "Cheat / hack")
          .add_button("cat_scam", "Scam")
          .add_button("cat_chat", "Abusive chat")
          .add_button("cat_other", "Other");

    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("lapor", "Cancel", "") });
}

void lapor_click(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::string button = hPipe["buttonClicked"];
    if (!button.starts_with("cat_")) return;

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string target = hPipe["target"];
    const std::string category = button.substr(4);
    const std::string note = clean_note(hPipe["note"]);

    if (!name_ok(target) || target == pPeer->growid || !fetch_account(target).found) return;
    if (category != "bot" && category != "cheat" && category != "scam" && category != "chat" && category != "other") return;

    static std::unordered_map<int, long long> last_report; // @note user id -> time of the last report
    const long long now = static_cast<long long>(std::time(nullptr));
    if (auto it = last_report.find(pPeer->user_id); it != last_report.end() && now - it->second < REPORT_COOLDOWN)
    {
        on::ConsoleMessage(event.peer, std::format("`4Please wait `w{}s`` before sending another report.``", REPORT_COOLDOWN - (now - it->second)));
        return;
    }
    if (count_query("SELECT COUNT(*) FROM report WHERE reporter = ? AND created_at >= CURDATE()", pPeer->growid) >= REPORTS_PER_DAY)
    {
        on::ConsoleMessage(event.peer, std::format("`4You reached the limit of `w{}`` reports for today.``", REPORTS_PER_DAY));
        return;
    }
    if (count_query("SELECT COUNT(*) FROM report WHERE reporter = ? AND target = ? AND status = 0", pPeer->growid, target) > 0)
    {
        on::ConsoleMessage(event.peer, "`6You already have an open report about that player. Staff will get to it.``");
        return;
    }
    last_report[pPeer->user_id] = now;

    const std::string target_world = world_of(find_online(target));
    const std::string my_world = (pPeer->netid != 0) ? pPeer->recent_worlds.back() : "";

    ::hStmt hStmt{ "INSERT INTO report (reporter, target, category, note, target_world, reporter_world) VALUES (?, ?, ?, ?, ?, ?)" };
    MYSQL_BIND params[6] = {
        make_bind_in(pPeer->growid), make_bind_in(target), make_bind_in(category),
        make_bind_in(note), make_bind_in(target_world), make_bind_in(my_world)
    };
    hStmt.bind_param(params);
    hStmt.execute();
    const long long id = static_cast<long long>(mysql_stmt_insert_id(hStmt.pStmt));

    staff_notify(std::format("`6[REPORT #{}]`` `w{}`` reported `w{}`` ({}) from world `w{}``. Type /reports.",
        id, pPeer->growid, target, category_label(category), my_world.empty() ? "?" : my_world), GUARDIAN);
    on::ConsoleMessage(event.peer, std::format("`2Report sent about `w{}``.`` Thank you, staff will look at it.", target));
}

/* ===================== /reports ===================== */

void reports_open(ENetEvent& event, const std::string &notice)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < GUARDIAN) return;

    std::vector<std::pair<std::string, long long>> list;
    {
        ::hStmt hStmt{ "SELECT target, COUNT(*) FROM report WHERE status = 0 GROUP BY target ORDER BY COUNT(*) DESC, MAX(id) DESC LIMIT 12" };
        std::string target{};
        long long count{};
        u_long length{};
        MYSQL_BIND results[2] = { make_bind_out(target), make_bind_out(count) };
        results[0].length = &length;
        mysql_stmt_bind_result(hStmt.pStmt, results);
        hStmt.execute();
        while (mysql_stmt_fetch(hStmt.pStmt) == 0) list.emplace_back(target.substr(0, length), count);
    }

    create_dialog dialog = themed();
    dialog.add_label_with_icon("big", "`wReports``", 32).add_spacer("small");
    if (!notice.empty()) dialog.add_textbox(notice).add_spacer("small");
    if (list.empty()) dialog.add_textbox("`2No open reports.``");
    else
    {
        dialog.add_textbox(std::format("`w{}`` player(s) with open reports. Most reported first:", list.size()));
        for (const auto &[target, count] : list)
            dialog.add_button("rep_" + target, std::format("{}{} - {} report(s)``", count >= 3 ? "`4" : "`w", target, count));
    }
    dialog.add_spacer("small").add_button("refresh", "`wRefresh``");

    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("reports", "Close", "") });
}

void reports_click(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < GUARDIAN) return; // @note never trust the dialog, check again

    const std::string button = hPipe["buttonClicked"];
    if (button == "refresh" || button == "back") { reports_open(event); return; }

    const std::size_t us = button.find('_');
    if (us == std::string::npos) return;
    const std::string action = button.substr(0, us);
    const std::string target = button.substr(us + 1);
    if (!name_ok(target)) return;

    if (action == "rep") { detail(event, target); return; }

    ::online_target online = find_online(target);

    if (action == "goto")
    {
        std::string world = world_of(online);
        if (world.empty())
        {
            const std::vector<row> rows = open_rows(target, 8);
            for (const row &r : rows) if (!r.target_world.empty()) { world = r.target_world; break; }
        }
        if (world.empty()) { detail(event, target, "`4No known world for that player.``"); return; }

        staff_log("report", target, pPeer->growid, "goto " + world);
        action::quit_to_exit(event, "", true);
        action::join_request(event, "", world);
        return;
    }
    if (action == "mute")
    {
        if (!can_act_on(pPeer, target)) { detail(event, target, "`4You can't act on someone with an equal or higher role.``"); return; }
        const int until = static_cast<int>(std::time(nullptr)) + MUTE_MINUTES * 60;
        ::hStmt hStmt{ "UPDATE peer SET muted_until = ? WHERE growid = ?" };
        MYSQL_BIND params[2] = { make_bind_in(until), make_bind_in(target) };
        hStmt.bind_param(params);
        hStmt.execute();
        if (online.data) online.data->muted_until = until;

        staff_log("mute", target, pPeer->growid, std::format("report: {} min", MUTE_MINUTES));
        detail(event, target, std::format("`2Muted for {} minutes.``", MUTE_MINUTES));
        return;
    }
    if (action == "kick")
    {
        if (!online.enet) { detail(event, target, "`4Player is not online.``"); return; }
        if (!can_act_on(pPeer, target)) { detail(event, target, "`4You can't act on someone with an equal or higher role.``"); return; }

        staff_log("kick", target, pPeer->growid, "report");
        send_gameMessage(online.enet, {.message = std::format("action|log\nmsg|`4You have been kicked by staff.`` Reason: `reported``")});
        enet_peer_disconnect_later(online.enet, 0);
        detail(event, target, "`2Kicked.``");
        return;
    }
    if (action == "nuke")
    {
        const std::string world = world_of(online);
        if (world.empty()) { detail(event, target, "`4Player must be online and inside a world.``"); return; }

        /* @note never nuke somebody else's world because a bot walked into it: only the reported player's own world, or a world nobody owns */
        auto it = std::ranges::find(worlds, world, &::world::name);
        if (it != worlds.end() && it->owner > 0 && it->owner != online.data->user_id)
        {
            detail(event, target, std::format("`4{} belongs to someone else.`` Kick the player instead, don't nuke an innocent owner's world.", world));
            return;
        }
        ::nuke(event, "/nuke " + world); // @note same rules, broadcast and log as /nuke
        detail(event, target, std::format("`2{} was nuked.``", world));
        return;
    }
    if (action == "done" || action == "dismiss")
    {
        const int new_status = (action == "done") ? HANDLED : DISMISSED;
        close_reports(target, new_status, pPeer->growid);
        staff_log("report", target, pPeer->growid, (action == "done") ? "handled" : "dismissed");
        if (new_status == HANDLED) thank_reporters(target);
        reports_open(event, std::format("`2Closed all reports about `w{}``.``", target));
        return;
    }
}
