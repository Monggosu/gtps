#pragma once

#include <string>
#include <vector>

/*
* SENTINEL: the automatic security system.
*   1. packet firewall   - drops malformed packets, limits packets per second            (admit, malformed, exception)
*   2. server authority  - the server decides what is possible                           (valid_*, movement_ok, interact_ok, collect_ok, owns)
*   3. flight recorder   - remembers the last actions of every player                    (record)
*   4. verdicts          - proof => punished at once, weak signals => two needed         (proof, suspicion)
*   5. self protection   - emergency brake, crash report, health                         (tick, startup)
* staff are never punished. everything is written to the sentinel_event table and shown in /sentinel.
*/
namespace sentinel
{
    enum class rule : u_char { BOUNDS, OWNERSHIP, RANGE, TELEPORT, FLOOD, MALFORMED, EXCEPTION, RATE, REGULAR, NONSTOP, SWEEP, SPAM, CRASH };
    enum class lvl : u_char { CAREFUL, FIERCE, MERCILESS }; // @note FIERCE ("Galak") is the default
    enum kind : u_char { MOVE = 1, PUNCH, PLACE, COLLECT, EQUIP, CHAT };

    extern lvl g_level;

    void startup();                 // @note once, after the database is connected
    void tick();                    // @note called about once a second from the main loop
    void install_crash_handler();   // @note writes crash_report.txt if the server dies
    void remember_packet(const ::peer *p, const char *what, int a, int b, int c); // @note what the server was doing, for the crash report

    /* ---- firewall ---- */
    bool admit(ENetEvent &event, int klass);                          // @param klass 0 movement, 1 tile actions, 2 text, 3 other. @return false = drop the packet
    void malformed(ENetEvent &event, const std::string &why);
    void exception(ENetEvent &event, const std::string &what);

    /* ---- server authority: every check returns false when the action must be ignored ---- */
    bool valid_tile(ENetEvent &event, const ::pos &tile);             // @note tile coordinates inside the 100x60 world
    bool valid_position(ENetEvent &event, const ::pos &pixels);
    bool movement_ok(ENetEvent &event, const ::pos &pixels);          // @note speed / teleport check, also updates the last known position
    bool interact_ok(ENetEvent &event, const ::pos &tile);            // @note punch / place distance
    bool collect_ok(ENetEvent &event, const ::pos &object_pixels);    // @note picking up an item from far away (item magnet)
    bool owns(ENetEvent &event, int item_id, kind what);              // @note place / equip only items the player has
    void grace(::peer &p);                                            // @note the server moved the player (join, respawn, door): reset speed tracking

    /* ---- memory + verdicts ---- */
    void record(ENetEvent &event, kind k, int x, int y, int arg = 0);

    /* ---- dataset recorder: writes every recorded action to a CSV file, for training/testing thresholds later ----
    * One CSV per growid per recording session, under recordings/<label>_<growid>_<timestamp>.csv
    * Columns: ms_since_start,kind,x,y,arg   (kind: 1=MOVE 2=PUNCH 3=PLACE 4=COLLECT 5=EQUIP 6=CHAT)
    * label is whatever you pass to record_start, e.g. "human" or "cheat_autofarm" - it is just part of the filename. */
    bool record_start(const std::string &growid, const std::string &label, std::string &out_error);
    bool record_stop(const std::string &growid, std::string &out_message);
    std::string record_status(const std::string &growid); // @note "" if nothing is being recorded for this growid
    void spam(ENetEvent &event);
    void proof(ENetEvent &event, rule r, const std::string &detail);       // @note something impossible happened: punish by level
    void suspicion(ENetEvent &event, rule r, const std::string &detail);   // @note weak signal: two different ones within 10 minutes become proof

    /* ---- accounts ---- */
    std::string login_block(const ::peer &p);       // @return "" or the message for a temporarily banned account
    void forgive(const std::string &growid);        // @note clears a temporary ban (used by /unban)

    /* ---- for /sentinel and /why ---- */
    struct entry { long long ts{}; std::string growid{}, rule{}, detail{}, action{}; bool proof{}, shadow{}; };
    struct today { long long proofs{}, softs{}, bans{}; };
    std::vector<entry> recent(std::size_t limit, const std::string &growid = "");
    today counts_today();
    bool shadow_active();
    long long shadow_left();      // @note seconds
    long long brake_left();       // @note seconds, 0 = no emergency brake
    void release_brake();
    void set_level(lvl l);
    const char *level_name(lvl l);
}
