#include "pch.hpp"

#include "onVariant/Action.hpp"

#include "msg.hpp"
#include "sb.hpp"
#include "time.hpp"
#include "who.hpp"
#include "me.hpp"
#include "find.hpp"
#include "warp.hpp"
#include "punch.hpp"
#include "skin.hpp"
#include "news.hpp"
#include "weather.hpp"
#include "ghost.hpp"
#include "clear.hpp"
#include "kick.hpp"
#include "mute.hpp"
#include "ban.hpp"
#include "setrank.hpp"
#include "item.hpp"
#include "pay.hpp"
#include "top.hpp"
#include "daily.hpp"
#include "seedshop.hpp"
#include "nuke.hpp"
#include "ridban.hpp"
#include "title.hpp"
#include "lapor.hpp"
#include "basic.hpp"
#include "events.hpp"
#include "redeem.hpp"
#include "sentinel_cmd.hpp"
#include "record.hpp"

#include "__command.hpp"

/* if you plan to use this outside of this file, please include in __command.hpp (^-^) - and just make it a void. */
void help_return(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto line = [&event](const std::string &message) { send_gameMessage(event.peer, {.message = "action|log\nmsg|" + message}); };

    line(std::format(">> Commands (your role: `{}``):", role_name(pPeer->role)));
    line("`wEveryone: /help /me /msg /time /who /warp /back /hub /punch /skin /clear /news /ping /online /worldinfo /status /rules /discord /afk /title /lapor /daily /pay /top /seedshop /redeem /boost + emotes``");
    if (pPeer->role >= VIP)           line("`cVIP+: /find /sb /weather``");
    if (pPeer->role >= GUARDIAN)      line("`bGuardian+: /ghost /mute /unmute /kick /reports /nuke (one world) /zoom /sentinel /why``");
    if (pPeer->role >= MODERATOR)     line("`2Mod+: /ban /unban /nuke (many worlds) /nukelist /banrid /unbanrid /alts``");
    if (pPeer->role >= ADMINISTRATOR) line("`4Admin: /unnuke /setrank /item /record /boostgem /boostxp /addredeem /delredeem /redeemlist /title <create|give|take|delete|list>``");
    line("`6Emotes: /wave /dance /love /sleep /facepalm /fp /smh /yes /no /omg /idk /shrug /furious /rolleyes /foldarms /fa /stubborn /fold /dab /sassy /dance2 /march /grumpy /shy``");
}

const std::unordered_map<std::string_view, cmd_entry> cmd_pool
{
    {"/help",    {help_return, PLAYER}},
    {"/?",       {help_return, PLAYER}},
    {"/msg",     {msg, PLAYER}},
    {"/time",    {command::time, PLAYER}}, // @note namespace is to prevent mismatching C time
    {"/who",     {who, PLAYER}},
    {"/me",      {me, PLAYER}},
    {"/warp",    {warp, PLAYER}},
    {"/punch",   {punch, PLAYER}},
    {"/skin",    {skin, PLAYER}},
    {"/news",    {news, PLAYER}},
    {"/clear",   {clear, PLAYER}},
    {"/daily",   {daily, PLAYER}},
    {"/pay",     {pay, PLAYER}},
    {"/top",     {top, PLAYER}},
    {"/seedshop",{seedshop, PLAYER}},
    {"/seeds",   {seedshop, PLAYER}},
    {"/ping",    {ping, PLAYER}},
    {"/online",  {online, PLAYER}},
    {"/worldinfo",{worldinfo, PLAYER}},
    {"/status",  {status, PLAYER}},
    {"/rules",   {rules, PLAYER}},
    {"/discord", {discord, PLAYER}},
    {"/afk",     {afk, PLAYER}},
    {"/back",    {back, PLAYER}},
    {"/hub",     {hub, PLAYER}},
    {"/title",   {title, PLAYER}}, // @note players get the window, administrators also get create/give/take/delete (checked inside)
    {"/lapor",   {lapor, PLAYER}},
    {"/redeem",  {redeem, PLAYER}},
    {"/boost",   {boost, PLAYER}},
    {"/find",    {find, VIP}},
    {"/sb",      {sb, VIP}},
    {"/weather", {weather, VIP}},
    /* guardian: keeps the server stable (chat, reports, single-world nuke) */
    {"/ghost",   {ghost, GUARDIAN}},
    {"/mute",    {mute, GUARDIAN}},
    {"/unmute",  {unmute, GUARDIAN}},
    {"/kick",    {kick, GUARDIAN}},
    {"/reports", {reports, GUARDIAN}},
    {"/nuke",    {nuke, GUARDIAN}}, // @note guardian: one world. several worlds at once need MODERATOR (checked inside)
    {"/zoom",    {zoom, GUARDIAN}},
    {"/sentinel",{sentinel_command, GUARDIAN}},
    {"/why",     {why, GUARDIAN}},
    {"/record",  {record, ADMINISTRATOR}},
    /* moderator: punishments */
    {"/ban",     {ban, MODERATOR}},
    {"/unban",   {unban, MODERATOR}},
    {"/nukelist",{nukelist, MODERATOR}},
    {"/banrid",  {banrid, MODERATOR}},
    {"/unbanrid",{unbanrid, MODERATOR}},
    {"/alts",    {alts, MODERATOR}},
    /* administrator */
    {"/unnuke",  {unnuke, ADMINISTRATOR}},
    {"/setrank", {setrank, ADMINISTRATOR}},
    {"/item",    {item, ADMINISTRATOR}},
    {"/boostgem",{boostgem, ADMINISTRATOR}},
    {"/boostxp", {boostxp, ADMINISTRATOR}},
    {"/addredeem",{addredeem, ADMINISTRATOR}},
    {"/delredeem",{delredeem, ADMINISTRATOR}},
    {"/redeemlist",{redeemlist, ADMINISTRATOR}},
    {"/wave", {on::Action, PLAYER}}, {"/dance", {on::Action, PLAYER}}, {"/love", {on::Action, PLAYER}}, {"/sleep", {on::Action, PLAYER}}, {"/facepalm", {on::Action, PLAYER}}, {"/fp", {on::Action, PLAYER}}, 
    {"/smh", {on::Action, PLAYER}}, {"/yes", {on::Action, PLAYER}}, {"/no", {on::Action, PLAYER}}, {"/omg", {on::Action, PLAYER}}, {"/idk", {on::Action, PLAYER}}, {"/shrug", {on::Action, PLAYER}}, 
    {"/furious", {on::Action, PLAYER}}, {"/rolleyes", {on::Action, PLAYER}}, {"/foldarms", {on::Action, PLAYER}}, {"/fa", {on::Action, PLAYER}}, {"/stubborn", {on::Action, PLAYER}}, {"/fold", {on::Action, PLAYER}}, 
    {"/dab", {on::Action, PLAYER}}, {"/sassy", {on::Action, PLAYER}}, {"/dance2", {on::Action, PLAYER}}, {"/march", {on::Action, PLAYER}}, {"/grumpy", {on::Action, PLAYER}}, {"/shy", {on::Action, PLAYER}}
};
