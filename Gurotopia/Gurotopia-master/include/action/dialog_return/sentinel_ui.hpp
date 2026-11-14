#pragma once

/* @brief the /sentinel dashboard (guardian+). notice (optional) is shown on top. */
extern void sentinel_open(ENetEvent& event, const std::string &notice = "");

/* @brief dialog_return handler for the "sentinel" dialog */
extern void sentinel_click(ENetEvent& event, const ::hPipe &hPipe);
