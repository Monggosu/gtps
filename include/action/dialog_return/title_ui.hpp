#pragma once

/* @brief open the title window (equipped title, owned titles, locked achievements, shop) */
extern void title_open(ENetEvent& event, const std::string &notice = "");

/* @brief dialog_return handler for the "title" dialog */
extern void title_click(ENetEvent& event, const ::hPipe &hPipe);
