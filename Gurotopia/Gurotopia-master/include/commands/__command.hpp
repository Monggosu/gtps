#pragma once

#include <unordered_map>

struct cmd_entry
{
    void(*fn)(ENetEvent&, std::string_view);
    u_char min_role; // @note role::
};

extern const std::unordered_map<std::string_view, cmd_entry> cmd_pool;
