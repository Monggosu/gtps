#pragma once

#include <ctime>

/* @note percent of gems removed from the game on every /pay. the receiver gets amount minus tax. */
inline constexpr long long PAY_TAX_PERCENT = 3;

/* @note /daily gives seeds (not gems). the reward grows with the streak, up to DAILY_STREAK_CAP days.
*  day N: seed types rolled  = DAILY_SEED_TYPES_BASE + (N - 1) / 2
*         seeds per type     = DAILY_SEED_AMOUNT
*         highest rarity     = DAILY_SEED_RARITY_BASE + DAILY_SEED_RARITY_STEP * (N - 1)
*  the last streak day also gives one bonus rare seed (DAILY_BONUS_RARITY_MIN..MAX). */
inline constexpr int DAILY_STREAK_CAP = 7;
inline constexpr int DAILY_SEED_TYPES_BASE = 2;
inline constexpr int DAILY_SEED_AMOUNT = 3;
inline constexpr int DAILY_SEED_RARITY_BASE = 5;
inline constexpr int DAILY_SEED_RARITY_STEP = 5;
inline constexpr int DAILY_BONUS_RARITY_MIN = 20;
inline constexpr int DAILY_BONUS_RARITY_MAX = 50;
/* @note NPC seed shop. SELLS cheap seeds at a fixed price: new players don't depend on veterans, and nobody can charge more
*  than this for basic seeds (price ceiling + gem sink). */
inline constexpr int SEED_SHOP_MAX_RARITY = 5;
inline constexpr int SEED_SHOP_LIST_SIZE = 12;
inline constexpr int SEED_SHOP_PACK = 10; // @note seeds per purchase
inline constexpr long long SEED_SHOP_PRICE_PER_RARITY = 5; // @note gems per seed = rarity * this
/* @note NPC seed buyer. BUYS a few mid-rarity seeds per day (same list for everyone, changes at the daily reset): veterans have
*  a reason to sell their stock, and prices get a floor. keep buyer prices well below shop prices, the difference is the gem sink.
*  the buyer never wants the seeds the shop sells (rarity > SEED_SHOP_MAX_RARITY), so buying from one and selling to the other can't be farmed. */
inline constexpr int SEED_BUYER_RARITY_MIN = 6;
inline constexpr int SEED_BUYER_RARITY_MAX = 35;
inline constexpr int SEED_BUYER_WANTED = 3; // @note different seeds wanted per day
inline constexpr int SEED_BUYER_QUOTA = 15; // @note how many of each seed ONE player can sell per day
inline constexpr long long SEED_BUYER_PRICE_PER_RARITY = 3; // @note gems per seed = rarity * this

/* @note when a "day" starts, in seconds after 00:00 UTC. 25200 = 07:00 UTC = midnight in WIB (UTC+7). set 0 for UTC midnight. */
inline constexpr long long DAILY_RESET_OFFSET = 7 * 3600;

/* @return the current game day number, changes once per day at the reset time */
inline long long game_day() { return (static_cast<long long>(std::time(nullptr)) + DAILY_RESET_OFFSET) / 86400; }

/*
* @brief write one row to the economy_log table (who moved what, when).
* @param kind short label: "pay", "store_buy", "daily", "give", ...
* @param from_uid user id that lost gems (0 = the game / nobody)
* @param to_uid user id that received gems (0 = the game / nobody, e.g. a shop or tax)
* @param tax gems destroyed by this transaction (gem sink)
*/
extern void log_economy(const std::string &kind, int from_uid, int to_uid, long long amount, long long tax = 0, const std::string &note = "");

/* @return 1234567 -> "1,234,567" */
extern std::string format_gems(long long value);
