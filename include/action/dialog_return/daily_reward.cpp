#include "pch.hpp"

#include "tools/create_dialog.hpp"
#include "tools/economy.hpp"
#include "tools/random.hpp"
#include "tools/seeds.hpp"

#include "daily_reward.hpp"

namespace
{
    struct plan
    {
        int types;       // @note how many different seeds are rolled
        int amount;      // @note how many of each
        int max_rarity;  // @note highest seed rarity that can be rolled
        bool bonus;      // @note one extra rare seed
    };

    plan plan_for(int day) // @note day starts at 1
    {
        const int d = std::min(day, DAILY_STREAK_CAP);
        return {
            DAILY_SEED_TYPES_BASE + (d - 1) / 2,
            DAILY_SEED_AMOUNT,
            DAILY_SEED_RARITY_BASE + DAILY_SEED_RARITY_STEP * (d - 1),
            d == DAILY_STREAK_CAP
        };
    }

    /* @brief pick one seed, common (low rarity) seeds are more likely than rare ones */
    const ::item *pick(const std::vector<const ::item*> &pool, int max_rarity)
    {
        int total = 0;
        for (const ::item *item : pool) total += std::max(1, max_rarity - item->rarity + 1);

        int roll = RandomRange(0, total); // @note upper bound is exclusive
        for (const ::item *item : pool)
        {
            roll -= std::max(1, max_rarity - item->rarity + 1);
            if (roll < 0) return item;
        }
        return pool.back();
    }

    /* @return {seed id, amount}, merged so the same seed is never listed twice */
    std::vector<std::pair<u_short, int>> roll_rewards(int streak)
    {
        const plan p = plan_for(streak);
        std::vector<std::pair<u_short, int>> rewards;

        auto add = [&rewards](u_short id, int amount)
        {
            auto it = std::ranges::find(rewards, id, &std::pair<u_short, int>::first);
            if (it != rewards.end()) it->second += amount;
            else rewards.emplace_back(id, amount);
        };

        std::vector<const ::item*> pool = seed_pool(1, p.max_rarity);
        if (!pool.empty())
            for (int i = 0; i < p.types; ++i) add(pick(pool, p.max_rarity)->id, p.amount);

        if (p.bonus)
        {
            std::vector<const ::item*> rare = seed_pool(DAILY_BONUS_RARITY_MIN, DAILY_BONUS_RARITY_MAX);
            if (!rare.empty()) add(pick(rare, DAILY_BONUS_RARITY_MAX)->id, 1);
        }
        return rewards;
    }
}

void daily_open(ENetEvent& event, const std::string &notice)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const long long now = static_cast<long long>(std::time(nullptr)) + DAILY_RESET_OFFSET;
    const long long today = now / 86400;
    const bool claimed = pPeer->daily_last >= today;
    const int streak = (pPeer->daily_last >= today - 1) ? pPeer->daily_streak : 0; // @note 0 = streak was broken
    const int today_row = std::min(streak + 1, DAILY_STREAK_CAP); // @note the day that can be claimed right now

    create_dialog dialog;
    dialog.set_default_color("`o")
          .add_label_with_icon("big", "`wDaily Seed Supply``", 3/*dirt seed*/)
          .add_spacer("small");
    if (!notice.empty())
        dialog.add_textbox(notice).add_spacer("small");

    dialog.add_textbox(std::format("Current streak: `w{}`` day(s). Claim every day for better seeds, a missed day resets it.", streak));
    dialog.add_spacer("small");
    for (int i = 1; i <= DAILY_STREAK_CAP; ++i)
    {
        const plan p = plan_for(i);
        std::string text = std::format("Day {}: {} kinds x{} seeds (up to rarity {}){}",
            i, p.types, p.amount, p.max_rarity, p.bonus ? " + 1 rare seed" : "");

        if (!claimed && i == today_row)    dialog.add_textbox(std::format("`5{}  <- today``", text));
        else if (i <= std::min(streak, DAILY_STREAK_CAP)) dialog.add_textbox(std::format("`2{} (claimed)``", text));
        else dialog.add_textbox(std::format("`w{}``", text));
    }
    dialog.add_spacer("small");

    if (!claimed)
        dialog.add_button("claim", "`2Claim Seeds``");
    else
    {
        const long long left = 86400 - (now % 86400);
        dialog.add_textbox(std::format("`4Already claimed today.`` Next supply in `w{}h {}m``.", left / 3600, (left % 3600) / 60));
    }

    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("daily", "Close", "") });
}

void daily_claim(ENetEvent& event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] != "claim") return;

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const long long today = (static_cast<long long>(std::time(nullptr)) + DAILY_RESET_OFFSET) / 86400;

    if (pPeer->daily_last >= today) // @note already claimed (double click, old dialog, ...)
    {
        daily_open(event);
        return;
    }

    /* @note claimed yesterday: streak continues. missed a day (or first time): back to 1. */
    const int streak = (pPeer->daily_last == today - 1) ? pPeer->daily_streak + 1 : 1;
    const std::vector<std::pair<u_short, int>> rewards = roll_rewards(streak);
    if (rewards.empty())
    {
        daily_open(event, "`4No seeds available right now.`` Please tell an admin.");
        return;
    }

    /* @note the backpack must have room, otherwise the claim is not used up */
    int new_slots = 0;
    for (const auto &[id, amount] : rewards)
        if (pPeer->have_slot(::slot(id, 1)) == 0) ++new_slots;
    if (static_cast<int>(pPeer->slots.size()) + new_slots > pPeer->slot_size)
    {
        daily_open(event, std::format("`4Your backpack is full.`` Free up `w{}`` slot(s), then claim again.", new_slots));
        return;
    }

    /* @note save the claim day first: if the server dies midway, the player loses a reward instead of getting it twice */
    pPeer->daily_last = today;
    pPeer->daily_streak = streak;
    pPeer->mysql_update("daily_last", pPeer->daily_last);
    pPeer->mysql_update("daily_streak", pPeer->daily_streak);

    std::string received;
    int total = 0;
    for (const auto &[id, amount] : rewards)
    {
        modify_item_inventory(event, ::slot(id, amount));
        received += std::format("{}`w{}x {}``", received.empty() ? "" : ", ", amount, id_to_item(id).raw_name);
        total += amount;
    }
    pPeer->mysql_update_all();
    log_economy("daily_seed", 0, pPeer->user_id, total, 0, std::format("streak {}", streak));

    daily_open(event, std::format("`2You received:`` {}", received));
}
