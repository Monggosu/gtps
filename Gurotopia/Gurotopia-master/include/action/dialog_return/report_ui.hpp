#pragma once

/* ---- /lapor (players) ---- */
extern void lapor_open(ENetEvent& event, const std::string &target);
extern void lapor_click(ENetEvent& event, const ::hPipe &hPipe);

/* ---- /reports (guardian+) ---- */
extern void reports_open(ENetEvent& event, const std::string &notice = "");
extern void reports_click(ENetEvent& event, const ::hPipe &hPipe);
