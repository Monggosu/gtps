#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/sentinel.hpp"

#include "moderation.hpp"
#include "record.hpp"

/* @brief /record start <growid> <label> | /record stop <growid> | /record status <growid>
* Writes every recorded action (move, punch, place, collect) of one account to a CSV file under
* recordings/, so human and cheat sessions can be compared later and used to tune Sentinel's thresholds.
* Labels are just filenames, e.g. "human_relaxed", "cheat_autofarm", "cheat_itemmagnet". */
void record(ENetEvent& event, const std::string_view text)
{
    std::vector<std::string> argv = readch(std::string(text), ' ');
    if (argv.size() < 3)
    {
        on::ConsoleMessage(event.peer, "Usage: /record start <`$growid``> <`$label``> | /record stop <`$growid``> | /record status <`$growid``>");
        on::ConsoleMessage(event.peer, "`oExample: /record start bot1 cheat_autofarm`` - then turn the cheat on with that account and play for a few minutes.``");
        return;
    }
    const std::string &sub = argv[1];
    const std::string &growid = argv[2];

    if (sub == "start")
    {
        if (argv.size() < 4) { on::ConsoleMessage(event.peer, "Usage: /record start <`$growid``> <`$label``>"); return; }
        const std::string label = join_args(argv, 3);
        std::string error;
        if (!sentinel::record_start(growid, label, error)) { on::ConsoleMessage(event.peer, "`4" + error + "``"); return; }
        on::ConsoleMessage(event.peer, std::format("`2>> Recording \"{}\" for `w{}``.`` Have them play now, then /record stop {} when done.", label, growid, growid));
    }
    else if (sub == "stop")
    {
        std::string message;
        const bool ok = sentinel::record_stop(growid, message);
        on::ConsoleMessage(event.peer, (ok ? "`2>> " : "`4") + message + "``");
    }
    else if (sub == "status")
    {
        const std::string status = sentinel::record_status(growid);
        on::ConsoleMessage(event.peer, status.empty() ?
            std::format("`6Nothing is being recorded for `w{}``.``", growid) :
            std::format("`2{}`` is being recorded: {}.``", growid, status));
    }
    else on::ConsoleMessage(event.peer, "Usage: /record start <`$growid``> <`$label``> | /record stop <`$growid``> | /record status <`$growid``>");
}
