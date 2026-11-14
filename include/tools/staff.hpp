#pragma once

#include <string>
#include <vector>

/* ---------- settings you may want to change ---------- */
inline constexpr const char *HUB_WORLD = "TRADE";   // @note /hub warps here. Never nuked.
inline constexpr float STAFF_CAMERA_ZOOM = 0.0f;     // @note camera zoom for guardian+ on world entry. 0 = off. find the right number in-game with /zoom <n>.
inline constexpr const char *SERVER_DISCORD = "";    // @note shown by /discord (empty = "not set")
inline constexpr const char *const SERVER_RULES[] = {
    "1. No cheating: scripts, bots, auto-clickers and mod menus are not allowed.",
    "2. Don't scam or trade for real money.",
    "3. Be respectful. No spam, no harassment.",
    "4. Don't abuse bugs. Report them to the staff with /lapor or on Discord.",
    "Breaking the rules gets your world nuked and your account banned."
};

/* camera zoom currently used for staff on world entry (starts at STAFF_CAMERA_ZOOM, /zoom <n> save changes it until restart) */
extern float g_staff_zoom;

/* ---------- nuke ---------- */
/* nuked worlds can't be entered by normal players (guardian+ can). stored in the nuked_world table, cached in memory. */
extern bool is_nuked(const std::string &world);
extern bool set_nuked(const std::string &world, bool nuked, const std::string &actor); // @return true if the state changed
extern std::vector<std::string> nuked_list(std::size_t limit);
/* send everyone below guardian out of a world, back to the world menu */
extern void evict_world(ENetEvent &event, const std::string &world, const std::string &message);

/* ---------- staff helpers ---------- */
extern void staff_notify(const std::string &message, u_char min_role = GUARDIAN); // @note console message to every online peer with role >= min_role
extern void staff_log(const std::string &action, const std::string &target, const std::string &actor, const std::string &reason); // @note moderation table (target is cut to 18 chars)
extern std::string fmt_duration(long long seconds); // @note 3725 -> "1h 2m"

/* ---------- device (RID) ---------- */
extern void remember_rid(int uid, const std::string &rid);
extern bool is_rid_banned(const std::string &rid);
extern void set_rid_banned(const std::string &rid, bool banned, const std::string &actor, const std::string &reason);
extern std::string rid_ban_reason(const std::string &rid);
extern std::string rid_of(const std::string &growid);                  // @note "" if unknown
extern std::vector<std::string> accounts_by_rid(const std::string &rid);
