#pragma once

/* @brief open the Seed Shop main menu. notice (optional) is shown on top. */
extern void seedshop_open(ENetEvent& event, const std::string &notice = "");

/* @brief dialog_return handler for the "seedshop" dialog */
extern void seedshop_click(ENetEvent& event, const ::hPipe &hPipe);
