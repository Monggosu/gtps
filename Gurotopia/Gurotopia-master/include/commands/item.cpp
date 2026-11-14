#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"

#include "item.hpp"

void item(ENetEvent& event, const std::string_view text)
{
    std::vector<std::string> argv = readch(std::string(text), ' ');

    if (argv.size() < 2)
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|Usage: /item <`$item_id``> [`$count`] (1-200, default 1) - Add an item to your inventory."});
        return;
    }

    const int id = std::atoi(argv[1].c_str());
    int count = (argv.size() > 2) ? std::atoi(argv[2].c_str()) : 1;

    if (id <= 0 || id >= (int)items.size())
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`4Invalid item id `{}``.`", id)});
        return;
    }
    if (count <= 0) count = 1;
    if (count > 200) count = 200;

    modify_item_inventory(event, ::slot{(short)id, (short)count});

    send_gameMessage(event.peer, {.message = std::format(
        "action|log\nmsg|`2>> Added `{}`` x{} to your inventory.", id, count)});
}
