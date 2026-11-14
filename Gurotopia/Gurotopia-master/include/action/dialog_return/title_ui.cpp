#include "pch.hpp"

#include "onVariant/SetBux.hpp"
#include "tools/create_dialog.hpp"
#include "tools/economy.hpp"
#include "tools/nametag.hpp"

#include "title_ui.hpp"

namespace
{
    create_dialog themed() // @note purple window instead of the default blue
    {
        create_dialog dialog;
        dialog.set_bg_color(48, 22, 84, 225).set_border_color(196, 150, 255, 255).set_default_color("`o");
        return dialog;
    }

    bool owned(const std::vector<int> &list, int id) { return std::ranges::find(list, id) != list.end(); }

    void open_shop(ENetEvent& event, const std::string &notice = "")
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        const std::vector<int> mine = owned_titles(pPeer->user_id);

        create_dialog dialog = themed();
        dialog.add_label_with_icon("big", "`wTitle Shop``", 1486).add_spacer("small");
        if (!notice.empty()) dialog.add_textbox(notice).add_spacer("small");
        dialog.add_textbox(std::format("You have `w{}`` gems. Titles are cosmetic and stay yours forever.", format_gems(pPeer->gems)));
        dialog.add_spacer("small");

        int shown = 0;
        for (const title_def &title : title_list())
        {
            if (title.price <= 0 || owned(mine, title.id)) continue;
            dialog.add_button(std::format("buy_{}", title.id), std::format("{} - {} gems", title.text, format_gems(title.price)));
            ++shown;
        }
        if (shown == 0) dialog.add_textbox("`2You own every title in the shop!``");
        dialog.add_spacer("small").add_button("main", "`wBack``");

        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("title", "Close", "") });
    }

    void equip(ENetEvent& event, int id)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);

        if (id != 0 && (!title_by_id(id) || !owns_title(pPeer->user_id, id)))
        {
            title_open(event, "`4You don't own that title.``");
            return;
        }
        pPeer->title_id = id;
        pPeer->mysql_update("title_id", pPeer->title_id);
        refresh_name(event);

        title_open(event, (id == 0) ? "`2Title removed.``" : "`2Title equipped!``");
    }

    void buy(ENetEvent& event, int id)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);

        const title_def *title = title_by_id(id);
        if (!title || title->price <= 0) { open_shop(event, "`4That title is not for sale.``"); return; }
        if (owns_title(pPeer->user_id, id)) { open_shop(event, "`6You already own that title.``"); return; }
        if (pPeer->gems < title->price) { open_shop(event, std::format("`4Not enough gems.`` You need `w{}``.", format_gems(title->price))); return; }

        /* @note take the gems first: a crash in between costs the player gems instead of giving a free title */
        pPeer->gems -= title->price;
        pPeer->mysql_update("gems", pPeer->gems);
        give_title(pPeer->user_id, id);
        log_economy("title", pPeer->user_id, 0, title->price, 0, title->tag); // @note gem sink
        on::SetBux(event);

        open_shop(event, std::format("`2Bought {}`` `2for `w{}`` gems! Equip it from the title window.``", title->text, format_gems(title->price)));
    }
}

void title_open(ENetEvent& event, const std::string &notice)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const int unlocked = grant_auto_titles(*pPeer);
    const std::vector<int> mine = owned_titles(pPeer->user_id);

    create_dialog dialog = themed();
    dialog.add_label_with_icon("big", "`wTitles``", 1486).add_spacer("small");
    if (!notice.empty()) dialog.add_textbox(notice).add_spacer("small");
    if (unlocked > 0) dialog.add_textbox(std::format("`2{} new title(s) unlocked!``", unlocked)).add_spacer("small");

    dialog.add_textbox(std::format("You appear as: {}", build_display_name(*pPeer, name_state_in_world(*pPeer))));
    dialog.add_spacer("small");

    for (const title_def &title : title_list())
        if (owned(mine, title.id))
            dialog.add_button(std::format("equip_{}", title.id), title.text + (pPeer->title_id == title.id ? " `2(equipped)``" : ""));
    dialog.add_button("equip_0", "`wNo title``");

    bool header = false;
    for (const title_def &title : title_list())
    {
        if (owned(mine, title.id) || title.rule.empty()) continue;
        if (!header) { dialog.add_spacer("small").add_textbox("`wLocked titles:``"); header = true; }
        dialog.add_smalltext(std::format("{} - {}", title.text, rule_hint(title.rule)));
    }
    dialog.add_spacer("small").add_button("shop", "`6Title Shop``");

    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("title", "Close", "") });
}

void title_click(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::string button = hPipe["buttonClicked"];

    if      (button == "main") title_open(event);
    else if (button == "shop") open_shop(event);
    else if (button.starts_with("equip_")) equip(event, std::atoi(button.c_str() + 6));
    else if (button.starts_with("buy_"))   buy(event, std::atoi(button.c_str() + 4));
}
