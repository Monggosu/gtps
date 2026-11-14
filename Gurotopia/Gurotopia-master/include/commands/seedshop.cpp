#include "pch.hpp"

#include "action/dialog_return/seedshop.hpp"

#include "seedshop.hpp"

void seedshop(ENetEvent& event, const std::string_view text)
{
    seedshop_open(event);
}
