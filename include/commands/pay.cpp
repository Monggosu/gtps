#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/economy.hpp"

#include <charconv>

#include "pay.hpp"

namespace
{
    std::string lower(std::string s)
    {
        for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
}

void pay(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    std::vector<std::string> args;
    for (const std::string &token : readch(std::string{text}, ' '))
        if (!token.empty()) args.push_back(token);

    constexpr const char *usage = "`6Usage: /pay <name> <gems>`` (`w{}%`` tax is removed from every payment)";
    if (args.size() != 3)
    {
        on::ConsoleMessage(event.peer, std::vformat(usage, std::make_format_args(PAY_TAX_PERCENT)));
        return;
    }

    long long amount{};
    const std::string &raw = args[2];
    auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), amount);
    if (ec != std::errc{} || ptr != raw.data() + raw.size() || amount < 1)
    {
        on::ConsoleMessage(event.peer, "`4Amount must be a whole number of at least 1.``");
        return;
    }
    if (amount > pPeer->gems)
    {
        on::ConsoleMessage(event.peer, std::format("`4You only have `w{}`` gems.``", format_gems(pPeer->gems)));
        return;
    }

    /* @note exact name only (case-insensitive). guessing a partial name with money is how people pay the wrong person. */
    ENetPeer *target = nullptr;
    peers("", PeerSearch::EVERYONE, [&](ENetPeer *p)
    {
        ::peer *pOthers = static_cast<::peer*>(p->data);
        if (pOthers && !target && lower(pOthers->growid) == lower(args[1])) target = p;
    });
    if (!target)
    {
        on::ConsoleMessage(event.peer, std::format("`4{} is not online.`` You can only pay players who are online.", args[1]));
        return;
    }
    ::peer *pTarget = static_cast<::peer*>(target->data);
    if (pTarget->user_id == pPeer->user_id)
    {
        on::ConsoleMessage(event.peer, "`4You can't pay yourself.``");
        return;
    }

    /* @note split first so amount * percent can never overflow */
    const long long tax = amount / 100 * PAY_TAX_PERCENT + (amount % 100) * PAY_TAX_PERCENT / 100;
    const long long net = amount - tax;
    if (pTarget->gems > std::numeric_limits<long long>::max() - net)
    {
        on::ConsoleMessage(event.peer, "`4That player can't hold that many gems.``");
        return;
    }

    /* @note take first, give second: if the server dies in between, gems vanish instead of being duplicated */
    pPeer->gems -= amount;
    pPeer->mysql_update("gems", pPeer->gems);
    pTarget->gems += net;
    pTarget->mysql_update("gems", pTarget->gems);

    log_economy("pay", pPeer->user_id, pTarget->user_id, amount, tax);

    ENetEvent targetEvent = event;
    targetEvent.peer = target;
    on::SetBux(event);
    on::SetBux(targetEvent);

    on::ConsoleMessage(event.peer, std::format("`2You paid `w{}`` gems to `w{}``. (tax: `w{}``)", format_gems(amount), pTarget->growid, format_gems(tax)));
    on::ConsoleMessage(target, std::format("`2You received `w{}`` gems from `w{}``.", format_gems(net), pPeer->growid));
}
