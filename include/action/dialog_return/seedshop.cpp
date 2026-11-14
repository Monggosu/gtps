#include "pch.hpp"

#include <algorithm>
#include <random>
#include <tuple>

#include "onVariant/SetBux.hpp"
#include "tools/create_dialog.hpp"
#include "tools/economy.hpp"
#include "tools/seeds.hpp"

#include "seedshop.hpp"

namespace
{
    long long shop_price(const ::item &item) { return item.rarity * SEED_SHOP_PRICE_PER_RARITY; }   // @note per seed
    long long buyer_price(const ::item &item) { return item.rarity * SEED_BUYER_PRICE_PER_RARITY; } // @note per seed

    /* @brief what the shop sells: the cheapest seeds */
    std::vector<const ::item*> shop_list()
    {
        std::vector<const ::item*> pool = seed_pool(1, SEED_SHOP_MAX_RARITY);
        std::ranges::sort(pool, [](const ::item *a, const ::item *b) { return std::tie(a->rarity, a->id) < std::tie(b->rarity, b->id); });
        if (pool.size() > static_cast<std::size_t>(SEED_SHOP_LIST_SIZE)) pool.resize(SEED_SHOP_LIST_SIZE);
        return pool;
    }

    /* @brief what the buyer wants today. same for every player, derived from the day number so it needs no storage. */
    std::vector<const ::item*> wanted_today()
    {
        std::vector<const ::item*> pool = seed_pool(SEED_BUYER_RARITY_MIN, SEED_BUYER_RARITY_MAX);
        std::mt19937 rng(static_cast<unsigned>(game_day()));
        std::ranges::shuffle(pool, rng);
        if (pool.size() > static_cast<std::size_t>(SEED_BUYER_WANTED)) pool.resize(SEED_BUYER_WANTED);
        return pool;
    }

    int sold_today(int uid, int seed)
    {
        const long long day = game_day();
        ::hStmt hStmt{ "SELECT sold FROM npc_quota WHERE uid = ? AND day = ? AND seed = ?" };
        MYSQL_BIND params[3] = { make_bind_in(uid), make_bind_in(day), make_bind_in(seed) };
        hStmt.bind_param(params);

        signed sold{};
        MYSQL_BIND result = make_bind_out(sold);
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        hStmt.fetch(); // @note no row = nothing sold yet (sold stays 0)
        return sold;
    }

    void add_sold(int uid, int seed, int amount)
    {
        const long long day = game_day();
        ::hStmt hStmt{ "INSERT INTO npc_quota (uid, day, seed, sold) VALUES (?, ?, ?, ?) ON DUPLICATE KEY UPDATE sold = sold + VALUES(sold)" };
        MYSQL_BIND params[4] = { make_bind_in(uid), make_bind_in(day), make_bind_in(seed), make_bind_in(amount) };
        hStmt.bind_param(params);
        hStmt.execute();
    }

    void open_buy(ENetEvent& event, const std::string &notice = "")
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);

        create_dialog dialog;
        dialog.set_default_color("`o")
              .add_label_with_icon("big", "`wSeed Shop: Buy``", 3)
              .add_spacer("small");
        if (!notice.empty()) dialog.add_textbox(notice).add_spacer("small");
        dialog.add_textbox(std::format("Fixed prices, always in stock. Each button buys `w{}`` seeds. You have `w{}`` gems.",
            SEED_SHOP_PACK, format_gems(pPeer->gems)));
        dialog.add_spacer("small");
        for (const ::item *item : shop_list())
            dialog.add_button(std::format("buy_{}", item->id),
                std::format("{} x{} - {} gems", item->raw_name, SEED_SHOP_PACK, format_gems(shop_price(*item) * SEED_SHOP_PACK)));
        dialog.add_spacer("small").add_button("menu_main", "`wBack``");

        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("seedshop", "Close", "") });
    }

    void open_sell(ENetEvent& event, const std::string &notice = "")
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);

        create_dialog dialog;
        dialog.set_default_color("`o")
              .add_label_with_icon("big", "`wSeed Shop: Sell``", 3)
              .add_spacer("small");
        if (!notice.empty()) dialog.add_textbox(notice).add_spacer("small");
        dialog.add_textbox(std::format("The buyer wants these seeds `wtoday`` (list changes every day). Max `w{}`` of each per player.", SEED_BUYER_QUOTA));
        dialog.add_spacer("small");

        const std::vector<const ::item*> wanted = wanted_today();
        if (wanted.empty()) dialog.add_textbox("`4The buyer isn't buying anything right now.``");
        for (const ::item *item : wanted)
        {
            const int owned = pPeer->have_slot(::slot(item->id, 1));
            const int left = std::max(0, SEED_BUYER_QUOTA - sold_today(pPeer->user_id, item->id));
            const int n = std::min(owned, left);

            dialog.add_label_with_icon("small", std::format("`w{}``: `2{}`` gems each", item->raw_name, format_gems(buyer_price(*item))), item->id);
            dialog.add_textbox(std::format("You own `w{}``. Can still sell `w{}`` today.", owned, left));
            if (n > 0)
                dialog.add_button(std::format("sell_{}", item->id), std::format("Sell {} - get {} gems", n, format_gems(n * buyer_price(*item))));
            dialog.add_spacer("small");
        }
        dialog.add_button("menu_main", "`wBack``");

        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("seedshop", "Close", "") });
    }

    void do_buy(ENetEvent& event, int id)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);

        const std::vector<const ::item*> list = shop_list();
        auto it = std::ranges::find(list, id, [](const ::item *item) { return static_cast<int>(item->id); });
        if (it == list.end()) { open_buy(event, "`4That seed is not sold right now.``"); return; }
        const ::item &item = **it;

        const long long cost = shop_price(item) * SEED_SHOP_PACK;
        if (pPeer->gems < cost) { open_buy(event, std::format("`4Not enough gems.`` You need `w{}``.", format_gems(cost))); return; }

        const int have = pPeer->have_slot(::slot(item.id, 1));
        if (have + SEED_SHOP_PACK > 200) { open_buy(event, "`4You can't carry more than 200 of one item.``"); return; }
        if (have == 0 && static_cast<int>(pPeer->slots.size()) + 1 > pPeer->slot_size) { open_buy(event, "`4Your backpack is full.``"); return; }

        /* @note take the gems first: a crash in between costs the player gems instead of duplicating seeds */
        pPeer->gems -= cost;
        pPeer->mysql_update("gems", pPeer->gems);
        modify_item_inventory(event, ::slot(item.id, SEED_SHOP_PACK));
        pPeer->mysql_update_all();
        log_economy("npc_buy", pPeer->user_id, 0, cost, 0, std::format("{}x seed {}", SEED_SHOP_PACK, item.id)); // @note gem sink
        on::SetBux(event);

        open_buy(event, std::format("`2Bought `w{}x {}`` for `w{}`` gems.``", SEED_SHOP_PACK, item.raw_name, format_gems(cost)));
    }

    void do_sell(ENetEvent& event, int id)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);

        /* @note never trust the button: check against today's list and the real inventory/quota */
        const std::vector<const ::item*> wanted = wanted_today();
        auto it = std::ranges::find(wanted, id, [](const ::item *item) { return static_cast<int>(item->id); });
        if (it == wanted.end()) { open_sell(event, "`4The buyer doesn't want that seed today.``"); return; }
        const ::item &item = **it;

        const int owned = pPeer->have_slot(::slot(item.id, 1));
        const int left = std::max(0, SEED_BUYER_QUOTA - sold_today(pPeer->user_id, item.id));
        const int n = std::min(owned, left);
        if (n <= 0) { open_sell(event, "`4Nothing to sell.``"); return; }

        const long long total = n * buyer_price(item);

        /* @note quota first, seeds second, gems last: a crash in between never gives free gems */
        add_sold(pPeer->user_id, item.id, n);
        modify_item_inventory(event, ::slot(item.id, -n));
        pPeer->mysql_update_all();

        pPeer->gems += total;
        pPeer->mysql_update("gems", pPeer->gems);
        log_economy("npc_sell", 0, pPeer->user_id, total, 0, std::format("{}x seed {}", n, item.id)); // @note gem faucet
        on::SetBux(event);

        open_sell(event, std::format("`2Sold `w{}x {}`` for `w{}`` gems.``", n, item.raw_name, format_gems(total)));
    }
}

void seedshop_open(ENetEvent& event, const std::string &notice)
{
    create_dialog dialog;
    dialog.set_default_color("`o")
          .add_label_with_icon("big", "`wSeed Shop``", 3)
          .add_spacer("small");
    if (!notice.empty()) dialog.add_textbox(notice).add_spacer("small");
    dialog.add_textbox("New here? Buy basic seeds at a fixed price. Got spare mid-level seeds? The buyer takes a few kinds every day.")
          .add_spacer("small")
          .add_button("menu_buy", "`2Buy Seeds``")
          .add_button("menu_sell", "`5Sell Seeds``");

    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("seedshop", "Close", "") });
}

void seedshop_click(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::string button = hPipe["buttonClicked"];

    if (button == "menu_main") seedshop_open(event);
    else if (button == "menu_buy") open_buy(event);
    else if (button == "menu_sell") open_sell(event);
    else if (button.starts_with("buy_")) do_buy(event, std::atoi(button.c_str() + 4));
    else if (button.starts_with("sell_")) do_sell(event, std::atoi(button.c_str() + 5));
}
