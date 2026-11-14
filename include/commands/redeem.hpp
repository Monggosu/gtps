#pragma once

extern void redeem(ENetEvent& event, const std::string_view text);
extern void addredeem(ENetEvent& event, const std::string_view text);
extern void delredeem(ENetEvent& event, const std::string_view text);
extern void redeemlist(ENetEvent& event, const std::string_view text);
