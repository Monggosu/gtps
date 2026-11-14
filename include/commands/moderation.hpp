#pragma once

#include <string>

/* moderation helpers shared by mute/ban/kick/setrank */

struct account_info
{
    bool found{};
    u_char role{};
    std::string ban_reason{};
    int muted_until{};
};

extern ::account_info fetch_account(const std::string &growid);
extern void mod_log(const std::string &action, const std::string &target, const std::string &actor, const std::string &reason);
extern int  mod_bans_today(const std::string &actor);

struct online_target
{
    ENetPeer *enet{};
    ::peer *data{};
};

extern ::online_target find_online(const std::string &growid);

/* parse player|vip|guardian|mod|moderator|admin|administrator|0-4. returns -1 if invalid */
extern int parse_role(const std::string &s);

/* true if reason is usable (>= min_len, not only spaces) */
extern bool reason_ok(const std::string &reason, std::size_t min_len);

/* join argv[from..end] with single spaces */
extern std::string join_args(const std::vector<std::string> &argv, std::size_t from);
