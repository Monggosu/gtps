#include "pch.hpp"

#include "seeds.hpp"

std::vector<const ::item*> seed_pool(int min_rarity, int max_rarity)
{
    std::vector<const ::item*> pool;
    for (const ::item &item : items)
    {
        if (item.type != type::SEED) continue;
        if (item.rarity < min_rarity || item.rarity > max_rarity || item.rarity >= 999) continue;
        if (item.cat & (CAT_UNTRADEABLE | CAT_HOLIDAY)) continue;
        if (u_char(item.property) & 04) continue; // @note not a normal seed (Bedrock, Main Door, Blank, Rollback Plaque, ...)
        if (item.id == 0 || (u_char(id_to_item(item.id - 1).property) & 04)) continue; // @note its block "never drops any seeds"
        pool.push_back(&item);
    }
    return pool;
}
