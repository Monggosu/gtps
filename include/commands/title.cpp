#include "pch.hpp"

#include "action/dialog_return/title_ui.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/nametag.hpp"
#include "tools/staff.hpp"

#include "moderation.hpp"
#include "title.hpp"

namespace
{
    void say(ENetEvent &event, const std::string &message) { on::ConsoleMessage(event.peer, message); }

    void usage(ENetEvent &event)
    {
        say(event, "`6Title admin:`` /title list | /title create <`$tag`` > <`$prefix|suffix``> <`$price``> <`$text with `` color codes``> | /title give <`$growid``> <`$tag``> | /title take <`$growid``> <`$tag``> | /title delete <`$tag``>");
    }
}

/* @brief /title - everyone: opens the title window. administrators: also manage titles. */
void title(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 2 || argv[1] == "open" || pPeer->role < ADMINISTRATOR)
    {
        title_open(event);
        return;
    }
    const std::string &sub = argv[1];

    if (sub == "list")
    {
        for (const title_def &t : title_list())
            say(event, std::format("`w{}``: {} `o({}, {}{})``", t.tag, t.text, t.suffix ? "suffix" : "prefix",
                t.price > 0 ? std::format("{} gems", t.price) : "not for sale", t.rule.empty() ? "" : ", auto: " + t.rule));
        return;
    }

    if (sub == "create")
    {
        if (argv.size() < 5 || (argv[3] != "prefix" && argv[3] != "suffix")) { usage(event); return; }
        std::string tag = argv[2];
        for (char &c : tag) c = std::tolower(static_cast<unsigned char>(c));
        if (tag.size() < 2 || tag.size() > 16 || !isalnum(tag)) { say(event, "`4Tag must be 2-16 letters or numbers.``"); return; }
        if (title_by_tag(tag)) { say(event, "`4That tag already exists.``"); return; }

        long long price = 0;
        try { price = std::stoll(argv[4]); } catch (const std::exception&) { price = -1; }
        // @note argv[4] is the price, so the text starts at argv[5]
        const std::string label = join_args(argv, 5);
        if (price < 0 || label.empty() || label.size() > 60) { say(event, "`4Price must be 0 or more, and the text 1-60 characters.``"); return; }

        const int suffix = (argv[3] == "suffix") ? 1 : 0;
        ::hStmt hStmt{ "INSERT INTO title (tag, text, suffix, price, rule) VALUES (?, ?, ?, ?, '')" };
        MYSQL_BIND params[4] = { make_bind_in(tag), make_bind_in(label), make_bind_in(suffix), make_bind_in(price) };
        hStmt.bind_param(params);
        hStmt.execute();
        title_reload();

        staff_log("title", tag, pPeer->growid, "create");
        say(event, std::format("`2>> Title `w{}`` created: {}``", tag, label));
        return;
    }

    if (sub == "give" || sub == "take")
    {
        if (argv.size() < 4) { usage(event); return; }
        const title_def *t = title_by_tag(argv[3]);
        const int uid = uid_of(argv[2]);
        if (!t)   { say(event, "`4Unknown title tag.``"); return; }
        if (!uid) { say(event, std::format("`4Account `w{}`` does not exist.``", argv[2])); return; }

        ::online_target online = find_online(argv[2]);
        if (sub == "give")
        {
            give_title(uid, t->id);
            if (online.enet) on::ConsoleMessage(online.enet, std::format("`2You received the title {}``! Type `w/title``.", t->text));
        }
        else
        {
            take_title(uid, t->id);
            if (online.data && online.data->title_id == t->id)
            {
                online.data->title_id = 0;
                online.data->mysql_update("title_id", online.data->title_id);
                ENetEvent ev = event;
                ev.peer = online.enet;
                refresh_name(ev);
            }
        }
        staff_log("title", argv[2], pPeer->growid, sub + " " + t->tag);
        say(event, std::format("`2>> Done: {} {} {}.``", sub, t->tag, argv[2]));
        return;
    }

    if (sub == "delete")
    {
        if (argv.size() < 3) { usage(event); return; }
        const title_def *t = title_by_tag(argv[2]);
        if (!t) { say(event, "`4Unknown title tag.``"); return; }
        const int id = t->id;
        const std::string tag = t->tag;

        {
            ::hStmt a{ "DELETE FROM peer_title WHERE title_id = ?" };
            MYSQL_BIND p = make_bind_in(id); a.bind_param(&p); a.execute();
        }
        {
            ::hStmt b{ "UPDATE peer SET title_id = 0 WHERE title_id = ?" };
            MYSQL_BIND p = make_bind_in(id); b.bind_param(&p); b.execute();
        }
        {
            ::hStmt c{ "DELETE FROM title WHERE id = ?" };
            MYSQL_BIND p = make_bind_in(id); c.bind_param(&p); c.execute();
        }
        peers("", PeerSearch::EVERYONE, [id, &event](ENetPeer *p)
        {
            ::peer *d = static_cast<::peer*>(p->data);
            if (!d || d->title_id != id) return;
            d->title_id = 0;
            ENetEvent ev = event;
            ev.peer = p;
            refresh_name(ev);
        });
        title_reload();

        staff_log("title", tag, pPeer->growid, "delete");
        say(event, std::format("`2>> Title `w{}`` deleted.``", tag));
        return;
    }

    usage(event);
}
