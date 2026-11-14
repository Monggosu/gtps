#pragma once

extern void ping(ENetEvent& event, const std::string_view text);
extern void online(ENetEvent& event, const std::string_view text);
extern void worldinfo(ENetEvent& event, const std::string_view text);
extern void status(ENetEvent& event, const std::string_view text);
extern void rules(ENetEvent& event, const std::string_view text);
extern void discord(ENetEvent& event, const std::string_view text);
extern void afk(ENetEvent& event, const std::string_view text);
extern void back(ENetEvent& event, const std::string_view text);
extern void hub(ENetEvent& event, const std::string_view text);
extern void zoom(ENetEvent& event, const std::string_view text);
