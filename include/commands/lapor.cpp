#include "pch.hpp"

#include "action/dialog_return/report_ui.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "lapor.hpp"

void lapor(ENetEvent& event, const std::string_view text)
{
    std::vector<std::string> argv = readch(std::string(text), ' ');
    if (argv.size() < 2)
    {
        on::ConsoleMessage(event.peer, "Usage: /lapor <`$growid``> - report a bot, cheater, scammer or rule breaker to the staff.");
        return;
    }
    lapor_open(event, argv[1]);
}

void reports(ENetEvent& event, const std::string_view text)
{
    reports_open(event);
}
