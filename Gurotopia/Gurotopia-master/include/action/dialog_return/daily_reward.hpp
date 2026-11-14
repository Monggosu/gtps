#pragma once

/* @brief show the daily reward window. notice (optional) is shown on top, e.g. the result of a claim. */
extern void daily_open(ENetEvent& event, const std::string &notice = "");

/* @brief dialog_return handler for the "daily" dialog (Claim button) */
extern void daily_claim(ENetEvent& event, const ::hPipe &hPipe);
