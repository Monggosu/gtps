#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"

#include "clear.hpp"

void clear(ENetEvent& event, const std::string_view text)
{
    for (int i = 0; i < 25; ++i)
        on::ConsoleMessage(event.peer, " ");

    send_gameMessage(event.peer, {.message = "action|log\nmsg|`2>> Chat cleared.``"});
}
