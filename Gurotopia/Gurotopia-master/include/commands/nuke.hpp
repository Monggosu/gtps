#pragma once

extern void nuke(ENetEvent& event, const std::string_view text);
extern void unnuke(ENetEvent& event, const std::string_view text);
extern void nukelist(ENetEvent& event, const std::string_view text);
