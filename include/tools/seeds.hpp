#pragma once

/* @brief seeds that may be used as rewards / NPC stock: normal obtainable seeds, tradeable and not holiday-only. in items.dat order (by id). */
extern std::vector<const ::item*> seed_pool(int min_rarity, int max_rarity);
