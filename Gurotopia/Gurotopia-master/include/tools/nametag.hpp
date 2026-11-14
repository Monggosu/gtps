#pragma once

#include <string>
#include <vector>

/* names and titles. every place that sets peer::display_growid should call build_display_name(). */

enum class name_state : u_char { NORMAL, OWNER, ACCESS }; // @note white, green (world owner), `^ (has access)

struct title_def
{
    int id{};
    std::string tag{};   // @note short name used in commands: "rookie"
    std::string text{};  // @note what is shown next to the name, with color codes: "`2[Rookie]``"
    bool suffix{};       // @note false = before the name, true = after the name
    long long price{};   // @note 0 = can't be bought
    std::string rule{};  // @note auto unlock: "level:N" | "streak:N" | "role:N", empty = none
};

/* @return "`2[Rookie]`` `wName``" (color follows state, title is the equipped one) */
extern std::string build_display_name(const ::peer &peer, ::name_state state = ::name_state::NORMAL);
/* @return OWNER/ACCESS if peer owns / has access to the world they are in */
extern ::name_state name_state_in_world(const ::peer &peer);
/* rebuild event.peer's name and tell everybody in the world */
extern void refresh_name(ENetEvent &event);

extern const std::vector<title_def> &title_list();
extern const title_def *title_by_id(int id);
extern const title_def *title_by_tag(const std::string &tag);
extern void title_reload(); // @note call after create / delete

extern int  uid_of(const std::string &growid); // @return 0 if the account doesn't exist
extern std::vector<int> owned_titles(int uid);
extern bool owns_title(int uid, int title_id);
extern void give_title(int uid, int title_id);
extern void take_title(int uid, int title_id);
/* give every auto-unlock title the peer qualifies for. @return how many are new */
extern int  grant_auto_titles(const ::peer &peer);
/* "reach level 5" */
extern std::string rule_hint(const std::string &rule);
