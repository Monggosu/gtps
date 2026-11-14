#include "pch.hpp"

#include "action/dialog_return/daily_reward.hpp"

#include "daily.hpp"

void daily(ENetEvent& event, const std::string_view text)
{
    daily_open(event); // @note the claim itself happens in the dialog (action/dialog_return/daily_reward.cpp)
}
