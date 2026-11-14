#include "pch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <unordered_map>
#include <execinfo.h>
#include <filesystem>

#include "commands/moderation.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/staff.hpp"

#include "sentinel.hpp"

namespace sentinel
{
    /* ---------- dataset recorder ---------- */
    namespace
    {
        struct recording { std::ofstream file; u_int start_ms{}; std::string label{}; long long rows{}; };
        std::unordered_map<std::string, recording> g_recordings; // @note keyed by growid, lowercase

        std::string lower(std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

        std::string safe_label(std::string s)
        {
            for (char &c : s) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
            return s.empty() ? "session" : s.substr(0, 32);
        }
    }


    lvl g_level = lvl::FIERCE;

    namespace
    {
        /* ---------- settings (distances in tiles, times in ms / seconds) ---------- */
        constexpr float RANGE_SOFT = 7.5f, RANGE_HARD = 14.0f;       // @note punch / place / pick up distance. beyond HARD no normal client can do it.
        constexpr float TELEPORT_SOFT = 25.0f, TELEPORT_HARD = 60.0f; // @note distance moved between two movement packets
        constexpr int   ACCOUNT_CHEAP_DAYS = 14, CHEAP_LEVEL = 15;    // @note "throwaway" account: younger than this, or below this level
        constexpr int   TEMP_BAN_DAYS = 7;
        constexpr long long SHADOW_DAYS = 3;                          // @note weak signals only watch (no punishment) for this long after the first start

        const char *const RULE_NAME[] = { "bounds", "ownership", "range", "teleport", "flood", "malformed", "exception", "rate", "regular", "nonstop", "sweep", "spam", "crash" };

        long long g_shadow_until = 0, g_brake_until = 0;
        std::deque<long long> g_recent_enforce;
        char g_last[256] = "no packet yet";

        u_int now_ms() { using namespace std::chrono; return static_cast<u_int>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()); }
        long long now_s() { return static_cast<long long>(std::time(nullptr)); }
        ::peer *peer_of(ENetEvent &event) { return event.peer ? static_cast<::peer*>(event.peer->data) : nullptr; }
        bool exempt(const ::peer &p) { return p.role >= GUARDIAN; }
        const char *name_of(rule r) { return RULE_NAME[static_cast<std::size_t>(r)]; }
        bool bot_like(rule r) { return r == rule::RATE || r == rule::REGULAR || r == rule::NONSTOP || r == rule::SWEEP || r == rule::FLOOD; }
        bool protected_world(const std::string &w) { return w == "START" || w == HUB_WORLD; }

        /* ---------- tiny database helpers ---------- */
        long long cfg_get(const std::string &k, long long def)
        {
            ::hStmt hStmt{ "SELECT v FROM sentinel_cfg WHERE k = ?" };
            MYSQL_BIND param = make_bind_in(k);
            hStmt.bind_param(&param);
            long long v{};
            MYSQL_BIND result = make_bind_out(v);
            mysql_stmt_bind_result(hStmt.pStmt, &result);
            hStmt.execute();
            return (mysql_stmt_fetch(hStmt.pStmt) == 0) ? v : def;
        }
        void cfg_set(const std::string &k, long long v)
        {
            ::hStmt hStmt{ "INSERT INTO sentinel_cfg (k, v) VALUES (?, ?) ON DUPLICATE KEY UPDATE v = VALUES(v)" };
            MYSQL_BIND params[2] = { make_bind_in(k), make_bind_in(v) };
            hStmt.bind_param(params);
            hStmt.execute();
        }
        void log_event(int uid, const std::string &growid, rule r, bool is_proof, const std::string &detail, const std::string &action, bool shadow)
        {
            ::hStmt hStmt{ "INSERT INTO sentinel_event (uid, growid, rule, sev, detail, action, shadow) VALUES (?, ?, ?, ?, ?, ?, ?)" };
            const std::string rule_s = name_of(r), detail_s = detail.substr(0, 160), action_s = action.substr(0, 24), growid_s = growid.substr(0, 24);
            const int sev = is_proof ? 1 : 0, sh = shadow ? 1 : 0;
            MYSQL_BIND params[7] = { make_bind_in(uid), make_bind_in(growid_s), make_bind_in(rule_s), make_bind_in(sev), make_bind_in(detail_s), make_bind_in(action_s), make_bind_in(sh) };
            hStmt.bind_param(params);
            hStmt.execute();
        }
        long long count_prior_proofs(int uid)
        {
            ::hStmt hStmt{ "SELECT COUNT(*) FROM sentinel_event WHERE uid = ? AND sev = 1 AND shadow = 0 AND ts > NOW() - INTERVAL 30 DAY" };
            MYSQL_BIND param = make_bind_in(uid);
            hStmt.bind_param(&param);
            long long n{};
            MYSQL_BIND result = make_bind_out(n);
            mysql_stmt_bind_result(hStmt.pStmt, &result);
            hStmt.execute();
            return (mysql_stmt_fetch(hStmt.pStmt) == 0) ? n : 0;
        }
        long long account_created(int uid)
        {
            ::hStmt hStmt{ "SELECT CAST(UNIX_TIMESTAMP(created_at) AS SIGNED) FROM peer WHERE uid = ?" };
            MYSQL_BIND param = make_bind_in(uid);
            hStmt.bind_param(&param);
            long long t{};
            MYSQL_BIND result = make_bind_out(t);
            mysql_stmt_bind_result(hStmt.pStmt, &result);
            hStmt.execute();
            return (mysql_stmt_fetch(hStmt.pStmt) == 0) ? t : 0;
        }
        std::vector<std::string> owned_worlds(int uid)
        {
            std::vector<std::string> out;
            ::hStmt hStmt{ "SELECT name FROM world WHERE owner = ? LIMIT 50" };
            MYSQL_BIND param = make_bind_in(uid);
            hStmt.bind_param(&param);
            std::string name{};
            u_long length{};
            MYSQL_BIND result = make_bind_out(name);
            result.length = &length;
            mysql_stmt_bind_result(hStmt.pStmt, &result);
            hStmt.execute();
            while (mysql_stmt_fetch(hStmt.pStmt) == 0) out.push_back(name.substr(0, length));
            return out;
        }

        void nuke_worlds(ENetEvent &event, const std::vector<std::string> &targets)
        {
            std::vector<std::string> done;
            for (const std::string &w : targets)
            {
                if (protected_world(w) || !set_nuked(w, true, "SENTINEL")) continue;
                evict_world(event, w, "`4This world was nuked by the staff.``");
                done.push_back(w);
            }
            if (done.empty()) return;
            peers("", PeerSearch::EVERYONE, [&done](ENetPeer *p)
            {
                if (!p->data) return;
                for (std::size_t i = 0; i < done.size() && i < 10; ++i)
                    on::ConsoleMessage(p, std::format("`4{}`` was nuked by the staff. Play fair and keep it clean, everyone!", done[i]));
            });
        }

        /* ---------- the ladder ---------- */
        void enforce(ENetEvent &event, ::peer &p, rule r, const std::string &detail)
        {
            const long long now = now_s();
            const long long prior = count_prior_proofs(p.user_id);
            const long long created = account_created(p.user_id);
            const bool cheap = (created > 0 && now - created < ACCOUNT_CHEAP_DAYS * 86400LL) || p.level.front() < CHEAP_LEVEL;

            enum class act { KICK, TEMP, PERMA } a = act::KICK;
            switch (g_level)
            {
                case lvl::CAREFUL:   a = (prior >= 2) ? act::TEMP : act::KICK; break;
                case lvl::FIERCE:    a = cheap ? act::PERMA : (prior >= 1 ? act::PERMA : act::TEMP); break;
                case lvl::MERCILESS: a = act::PERMA; break;
            }

            /* emergency brake: too many punishments in a short time means a rule may be wrong. kick only, and tell the staff. */
            g_recent_enforce.push_back(now);
            while (!g_recent_enforce.empty() && now - g_recent_enforce.front() > 300) g_recent_enforce.pop_front();
            if (g_recent_enforce.size() > 6 && now >= g_brake_until)
            {
                g_brake_until = now + 1800;
                staff_notify("`4[SENTINEL] EMERGENCY BRAKE:`` too many punishments in 5 minutes. A rule may be wrong. Only kicks until `w/sentinel`` > Release.", GUARDIAN);
            }
            const bool braked = now < g_brake_until;
            if (braked) a = act::KICK;

            std::string action_name = (a == act::PERMA) ? "ban" : (a == act::TEMP) ? "tempban" : (braked ? "kick(brake)" : "kick");
            const std::string generic = "Blocked by the automatic security system.";

            if (a == act::PERMA)
            {
                ::hStmt hStmt{ "UPDATE peer SET ban_reason = ? WHERE uid = ?" };
                MYSQL_BIND params[2] = { make_bind_in(generic), make_bind_in(p.user_id) };
                hStmt.bind_param(params);
                hStmt.execute();
                p.ban_reason = generic;
            }
            else if (a == act::TEMP)
            {
                const long long until = now + TEMP_BAN_DAYS * 86400LL;
                ::hStmt hStmt{ "UPDATE peer SET ban_until = ? WHERE uid = ?" };
                MYSQL_BIND params[2] = { make_bind_in(until), make_bind_in(p.user_id) };
                hStmt.bind_param(params);
                hStmt.execute();
            }

            /* worlds: bots and throwaway accounts lose their worlds. other people's worlds are never touched. */
            std::vector<std::string> targets;
            if (a != act::KICK && (cheap || bot_like(r)))
            {
                targets = owned_worlds(p.user_id);
                if (p.netid != 0)
                {
                    const std::string here = p.recent_worlds.back();
                    auto it = std::ranges::find(worlds, here, &::world::name);
                    if (it != worlds.end() && (it->owner <= 0 || it->owner == p.user_id) && std::ranges::find(targets, here) == targets.end())
                        targets.push_back(here);
                }
            }

            log_event(p.user_id, p.growid, r, true, detail, action_name, false);
            staff_log("sentinel", p.growid, "SENTINEL", std::string(name_of(r)) + ": " + detail);
            staff_notify(std::format("`4[SENTINEL]`` `w{}``: {} -> `w{}``{}. {}", p.growid, name_of(r), action_name,
                targets.empty() ? "" : std::format(" + {} world(s) nuked", targets.size()), detail), GUARDIAN);

            nuke_worlds(event, targets);

            send_gameMessage(event.peer, {.message = "action|log\nmsg|`4You were disconnected by the automatic security system.``"});
            enet_peer_disconnect_later(event.peer, 0);
        }

        bool in_shadow() { return now_s() < g_shadow_until; }

        void evaluate_window(ENetEvent &event, ::peer &p)
        {
            ::guard_state &g = p.guard;
            const std::size_t n = g.gaps.size();

            std::array<u_short, 40> sorted = g.gaps;
            std::ranges::sort(sorted);
            const double median = (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;

            double sum = 0, sq = 0;
            for (u_short gap : g.gaps) sum += gap;
            const double mean = sum / n;
            for (u_short gap : g.gaps) sq += (gap - mean) * (gap - mean);
            const double cv = (mean > 0) ? std::sqrt(sq / n) / mean : 1.0;

            /* @note a bot that randomises its delay (+-25%) still keeps every hit inside a narrow band around its median, a human never does for 40 hits */
            const bool bounded = median > 0 && sorted.front() >= 0.65 * median && sorted.back() <= 1.35 * median;

            /* @note hits arrive in bursts after a network stall, so only THREE windows in a row count */
            g.fast_windows = (mean < 90.0) ? static_cast<u_char>(std::min(250, g.fast_windows + 1)) : 0;
            g.even_windows = ((cv < 0.06 || bounded) && mean < 700.0) ? static_cast<u_char>(std::min(250, g.even_windows + 1)) : 0;

            if (g.fast_windows >= 3) suspicion(event, rule::RATE, std::format("{} windows in a row of 40 hits at {:.0f} ms each", g.fast_windows, mean));
            if (g.even_windows >= 3) suspicion(event, rule::REGULAR, std::format("{} windows in a row with machine-even timing (every {:.0f} ms, variation {:.1f}%)", g.even_windows, median, cv * 100.0));
            if (g.sweep_total >= 40 && g.sweep_hits * 100 / g.sweep_total >= 85)
                suspicion(event, rule::SWEEP, std::format("{}% of hits are neighbours in one direction", g.sweep_hits * 100 / g.sweep_total));
        }

        void *g_trace[64]; // @note pre-sized: the signal handler must not allocate memory

        void write_all(int fd, const char *s, std::size_t n) { [[maybe_unused]] ssize_t r = write(fd, s, n); }
        void write_str(int fd, const char *s) { write_all(fd, s, strnlen(s, 4096)); }

        void crash_handler(int sig)
        {
            const char d[3] = { static_cast<char>('0' + (sig / 10) % 10), static_cast<char>('0' + sig % 10), '\n' };
            const int n = backtrace(g_trace, 64); // @note safe here: backtrace() was already "warmed up" once outside the signal handler

            for (int fd : { 2, open("crash_report.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644) })
            {
                if (fd < 0) continue;
                write_str(fd, "\n[SENTINEL] crash caught, signal=");
                write_all(fd, d, 3);
                write_str(fd, "last packet before the crash: ");
                write_str(fd, g_last);
                write_str(fd, "\nstack trace (run tools-admin/diagnose_crash.sh to turn this into exact file:line):\n");
                backtrace_symbols_fd(g_trace, n, fd); // @note malloc-free, safe to call from a signal handler
                if (fd != 2) close(fd);
            }
            _exit(128 + sig);
        }
    }

    /* ======================= setup / upkeep ======================= */

    void startup()
    {
        g_level = static_cast<lvl>(std::clamp<long long>(cfg_get("level", 1), 0, 2));
        long long shadow = cfg_get("shadow_until", 0);
        if (shadow == 0) { shadow = now_s() + SHADOW_DAYS * 86400LL; cfg_set("shadow_until", shadow); }
        g_shadow_until = shadow;

        std::ifstream report("crash_report.txt");
        if (report)
        {
            const std::string text((std::istreambuf_iterator<char>(report)), std::istreambuf_iterator<char>());
            report.close();
            std::remove("crash_report.txt");
            log_event(0, "(server)", rule::CRASH, true, "server stopped unexpectedly: " + text, "report", false);
            print("SENTINEL: the server crashed last time. See /sentinel. " + text.substr(0, 160));
        }
    }

    void tick()
    {
        static long long last = 0;
        const long long now = now_s();
        if (now - last < 60) return;
        last = now;
        if (g_brake_until != 0 && now >= g_brake_until)
        {
            g_brake_until = 0;
            staff_notify("`2[SENTINEL] Emergency brake released automatically.``", GUARDIAN);
        }
        cfg_set("heartbeat", now);
    }

    void install_crash_handler()
    {
        /* @note a stack-overflow crash can't be caught on the normal stack (pushing the handler's own frame
        * faults again), so the handler runs on a separate, pre-allocated stack instead. */
        static std::vector<char> altstack(SIGSTKSZ * 4);
        stack_t ss{};
        ss.ss_sp = altstack.data();
        ss.ss_size = altstack.size();
        ss.ss_flags = 0;
        sigaltstack(&ss, nullptr);

        struct sigaction sa{};
        sa.sa_handler = crash_handler;
        sa.sa_flags = SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        for (int s : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS }) sigaction(s, &sa, nullptr);

        backtrace(g_trace, 2); // @note "warm up" glibc's unwinder (it allocates memory the very first time it runs) outside of any signal handler
    }

    void remember_packet(const ::peer *p, const char *what, int a, int b, int c)
    {
        std::snprintf(g_last, sizeof(g_last), "player=%s packet=%s a=%d b=%d c=%d time=%lld",
            (p && !p->growid.empty()) ? p->growid.c_str() : "(not logged in)", what, a, b, c, now_s());
    }

    /* ======================= firewall ======================= */

    bool admit(ENetEvent &event, int klass)
    {
        ::peer *p = peer_of(event);
        if (!p || exempt(*p)) return true;
        ::guard_state &g = p->guard;

        static const float RATE[4]  = { 60.0f, 25.0f, 12.0f, 30.0f };
        static const float BURST[4] = { 120.0f, 40.0f, 25.0f, 60.0f };

        const u_int now = now_ms();
        float dt = static_cast<float>(now - g.bucket_ms) / 1000.0f;
        g.bucket_ms = now;
        if (dt > 5.0f || dt < 0.0f) dt = 5.0f;
        for (int i = 0; i < 4; ++i) g.tokens[i] = std::min(BURST[i], g.tokens[i] + RATE[i] * dt);

        if (klass < 0 || klass > 3) klass = 3;
        if (g.tokens[klass] >= 1.0f) { g.tokens[klass] -= 1.0f; return true; }

        if (now - g.drop_window_ms > 5000) { g.drop_window_ms = now; g.dropped = 0; }
        if (++g.dropped > 150)
        {
            g.dropped = 0;
            proof(event, rule::FLOOD, std::format("sending far too many packets (class {})", klass));
        }
        return false;
    }

    void malformed(ENetEvent &event, const std::string &why)
    {
        ::peer *p = peer_of(event);
        if (!p) { enet_peer_disconnect_now(event.peer, 0); return; }
        proof(event, rule::MALFORMED, why);
    }

    void exception(ENetEvent &event, const std::string &what)
    {
        ::peer *p = peer_of(event);
        print("SENTINEL: handler exception: " + what);
        if (!p || exempt(*p)) return;
        ::guard_state &g = p->guard;
        const u_int now = now_ms();
        if (now - g.exception_ms > 60000) g.exceptions = 0;
        g.exception_ms = now;
        log_event(p->user_id, p->growid, rule::EXCEPTION, false, what, "watch", false);
        if (++g.exceptions >= 3) { g.exceptions = 0; proof(event, rule::EXCEPTION, "repeatedly breaking the server: " + what); }
    }

    /* ======================= server authority ======================= */

    bool valid_tile(ENetEvent &event, const ::pos &t)
    {
        if (std::isfinite(t.x) && std::isfinite(t.y) && t.x >= 0.0f && t.x < 100.0f && t.y >= 0.0f && t.y < 60.0f) return true;
        if (::peer *p = peer_of(event); p && !exempt(*p))
            proof(event, rule::BOUNDS, std::format("tile outside the world ({}, {})", t.x, t.y));
        return false;
    }

    bool valid_position(ENetEvent &event, const ::pos &px)
    {
        if (std::isfinite(px.x) && std::isfinite(px.y) && px.x >= -128.0f && px.x <= 3328.0f && px.y >= -128.0f && px.y <= 2048.0f) return true;
        if (::peer *p = peer_of(event); p && !exempt(*p))
            proof(event, rule::BOUNDS, std::format("position outside the world ({}, {})", px.x, px.y));
        return false;
    }

    bool movement_ok(ENetEvent &event, const ::pos &px)
    {
        ::peer *p = peer_of(event);
        if (!p) return false;
        ::guard_state &g = p->guard;
        const u_int now = now_ms();

        const bool baseline = (g.last_move_ms == 0) || static_cast<int>(g.grace_until_ms - now) > 0;
        const float dist = std::hypot(px.x - g.last_x, px.y - g.last_y) / 32.0f;
        const u_int dt = now - g.last_move_ms;

        if (!baseline && !exempt(*p))
        {
            if (dist > TELEPORT_HARD && dt < 1000)
            {
                proof(event, rule::TELEPORT, std::format("moved {:.0f} tiles in {} ms", dist, dt));
                return false;
            }
            if (dist > TELEPORT_SOFT && dt < 300)
                suspicion(event, rule::TELEPORT, std::format("moved {:.0f} tiles in {} ms", dist, dt));
        }
        g.last_x = px.x; g.last_y = px.y; g.last_move_ms = now;
        return true;
    }

    bool interact_ok(ENetEvent &event, const ::pos &tile)
    {
        ::peer *p = peer_of(event);
        if (!p) return false;
        if (exempt(*p) || p->netid == 0) return true;

        const float dist = std::hypot(tile.x + 0.5f - p->pos.x / 32.0f, tile.y + 0.5f - p->pos.y / 32.0f);
        if (dist > RANGE_HARD)
        {
            proof(event, rule::RANGE, std::format("acted on a tile {:.0f} tiles away", dist));
            return false;
        }
        if (dist > RANGE_SOFT) suspicion(event, rule::RANGE, std::format("acted on a tile {:.0f} tiles away", dist));
        return true;
    }

    bool collect_ok(ENetEvent &event, const ::pos &obj)
    {
        ::peer *p = peer_of(event);
        if (!p) return false;
        if (exempt(*p)) return true;

        const float dist = std::hypot(obj.x / 32.0f - p->pos.x / 32.0f, obj.y / 32.0f - p->pos.y / 32.0f);
        if (dist > RANGE_HARD)
        {
            proof(event, rule::RANGE, std::format("picked up an item {:.0f} tiles away", dist));
            return false;
        }
        if (dist > RANGE_SOFT) suspicion(event, rule::RANGE, std::format("picked up an item {:.0f} tiles away", dist));
        return true;
    }

    bool owns(ENetEvent &event, int item_id, kind what)
    {
        ::peer *p = peer_of(event);
        if (!p) return false;
        if (item_id > 0 && item_id < 65535 && p->have_slot(::slot(static_cast<u_short>(item_id), 1)) > 0) return true;

        /* @note a laggy client can send one extra packet right after its last item was used, so one or two misses are forgiven */
        if (exempt(*p)) return false;
        ::guard_state &g = p->guard;
        const u_int now = now_ms();
        if (now - g.own_miss_ms > 10000) g.own_misses = 0;
        g.own_miss_ms = now;
        if (++g.own_misses >= 3)
        {
            g.own_misses = 0;
            proof(event, rule::OWNERSHIP, std::format("tried to {} item {} without owning it", what == EQUIP ? "wear" : "place", item_id));
        }
        return false;
    }

    void grace(::peer &p)
    {
        p.guard.grace_until_ms = now_ms() + 4000;
        p.guard.last_move_ms = 0;
    }

    /* ======================= memory + verdicts ======================= */

    void record(ENetEvent &event, kind k, int x, int y, int arg)
    {
        ::peer *p = peer_of(event);
        if (!p) return;
        ::guard_state &g = p->guard;
        const u_int now = now_ms();

        if (auto it = g_recordings.find(lower(p->growid)); it != g_recordings.end())
        {
            it->second.file << (now - it->second.start_ms) << ',' << static_cast<int>(k) << ',' << x << ',' << y << ',' << arg << '\n';
            if (++it->second.rows % 50 == 0) it->second.file.flush(); // @note keep data on disk without flushing every single row
        }

        g.ring[g.head] = { now, static_cast<u_char>(k), static_cast<short>(x), static_cast<short>(y), static_cast<short>(arg) };
        g.head = static_cast<u_char>((g.head + 1) % g.ring.size());
        if (g.recorded < 65535) ++g.recorded;

        if (k != PUNCH || exempt(*p)) return;

        if (g.last_punch_ms != 0)
        {
            const u_int gap = now - g.last_punch_ms;
            if (gap > 30000) { g.farm_start_ms = now; g.gap_n = 0; g.sweep_hits = g.sweep_total = 0; g.fast_windows = g.even_windows = 0; } // @note a real pause: the streak starts over
            else
            {
                g.gaps[g.gap_head] = static_cast<u_short>(std::min<u_int>(gap, 65535));
                g.gap_head = static_cast<u_char>((g.gap_head + 1) % g.gaps.size());
                if (g.gap_n < g.gaps.size()) ++g.gap_n;
            }
        }
        if (g.farm_start_ms == 0) g.farm_start_ms = now;
        g.last_punch_ms = now;

        const int dx = x - g.last_tx, dy = y - g.last_ty;
        const bool neighbour = (std::abs(dx) + std::abs(dy) == 1);
        ++g.sweep_total;
        if (neighbour && dx == g.last_dx && dy == g.last_dy) ++g.sweep_hits;
        g.last_tx = static_cast<short>(x); g.last_ty = static_cast<short>(y);
        g.last_dx = static_cast<short>(dx); g.last_dy = static_cast<short>(dy);

        if (g.gap_n == g.gaps.size() && g.sweep_total >= 40)
        {
            evaluate_window(event, *p);
            g.gap_n = 0; g.sweep_hits = g.sweep_total = 0;
        }
        if (now - g.farm_start_ms > 45u * 60u * 1000u)
        {
            suspicion(event, rule::NONSTOP, "hitting blocks for 45+ minutes without a real pause");
            g.farm_start_ms = now - 40u * 60u * 1000u; // @note fires again after 5 more minutes
        }
    }

    void spam(ENetEvent &event)
    {
        ::peer *p = peer_of(event);
        if (!p || exempt(*p)) return;
        ::guard_state &g = p->guard;
        const u_int now = now_ms();
        if (now - g.spam_ms > 60000) g.spam_strikes = 0;
        g.spam_ms = now;
        if (++g.spam_strikes < 3) return;

        g.spam_strikes = 0;
        const int until = static_cast<int>(now_s()) + 600;
        ::hStmt hStmt{ "UPDATE peer SET muted_until = ? WHERE uid = ?" };
        MYSQL_BIND params[2] = { make_bind_in(until), make_bind_in(p->user_id) };
        hStmt.bind_param(params);
        hStmt.execute();
        p->muted_until = until;
        log_event(p->user_id, p->growid, rule::SPAM, false, "chat spam three times in a minute: muted 10 min", "mute", false);
        on::ConsoleMessage(event.peer, "`4You were muted for 10 minutes for spamming.``");
    }

    void suspicion(ENetEvent &event, rule r, const std::string &detail)
    {
        ::peer *p = peer_of(event);
        if (!p || exempt(*p)) return;
        ::guard_state &g = p->guard;
        const long long now = now_s();
        const std::size_t idx = static_cast<std::size_t>(r) % g.soft_at.size();

        const bool fresh = now - g.soft_at[idx] > 120; // @note do not write the same weak signal to the database again and again
        g.soft_at[idx] = now;
        int distinct = 0;
        for (std::time_t t : g.soft_at) if (t != 0 && now - t <= 600) ++distinct;

        const bool shadow = in_shadow();
        if (fresh) log_event(p->user_id, p->growid, r, false, detail, shadow ? "shadow" : "watch", shadow);

        if (distinct < 2) return;

        /* @note a person who farms a straight row at a steady rhythm shows rate / regular / sweep too. what a person does not do is
        *  hit blocks for 45 minutes without one real pause, so farming-type signals only count together with that one. */
        auto farming = [](std::size_t i) { return i == static_cast<std::size_t>(rule::RATE) || i == static_cast<std::size_t>(rule::REGULAR) || i == static_cast<std::size_t>(rule::SWEEP) || i == static_cast<std::size_t>(rule::NONSTOP); };
        bool only_farming = true, has_nonstop = false;
        for (std::size_t i = 0; i < g.soft_at.size(); ++i)
        {
            if (g.soft_at[i] == 0 || now - g.soft_at[i] > 600) continue;
            if (!farming(i)) only_farming = false;
            if (i == static_cast<std::size_t>(rule::NONSTOP)) has_nonstop = true;
        }
        if (only_farming && !has_nonstop) return;

        if (shadow)
        {
            if (fresh) log_event(p->user_id, p->growid, r, false, "WOULD PUNISH (shadow mode): two independent signals", "would-punish", true);
            return;
        }
        proof(event, r, "two independent signals: " + detail);
    }

    void proof(ENetEvent &event, rule r, const std::string &detail)
    {
        ::peer *p = peer_of(event);
        if (!p || exempt(*p)) return;
        if (p->user_id <= 0) // @note not logged in yet: there is no account to ban, just cut the connection
        {
            log_event(0, "(guest)", r, true, detail, "disconnect", false);
            enet_peer_disconnect_now(event.peer, 0);
            return;
        }
        enforce(event, *p, r, detail);
    }

    /* ======================= accounts ======================= */

    std::string login_block(const ::peer &p)
    {
        if (p.user_id <= 0) return "";
        ::hStmt hStmt{ "SELECT ban_until FROM peer WHERE uid = ?" };
        MYSQL_BIND param = make_bind_in(p.user_id);
        hStmt.bind_param(&param);
        long long until{};
        MYSQL_BIND result = make_bind_out(until);
        mysql_stmt_bind_result(hStmt.pStmt, &result);
        hStmt.execute();
        if (mysql_stmt_fetch(hStmt.pStmt) != 0 || until <= now_s()) return "";
        return std::format("`4Blocked by the automatic security system.`` Try again in `w{}``.", fmt_duration(until - now_s()));
    }

    void forgive(const std::string &growid)
    {
        ::hStmt hStmt{ "UPDATE peer SET ban_until = 0 WHERE growid = ?" };
        MYSQL_BIND param = make_bind_in(growid);
        hStmt.bind_param(&param);
        hStmt.execute();
    }

    /* ======================= for the dashboard ======================= */

    std::vector<entry> recent(std::size_t limit, const std::string &growid)
    {
        std::vector<entry> out;
        const std::string query = std::format(
            "SELECT CAST(UNIX_TIMESTAMP(ts) AS SIGNED), growid, rule, detail, action, sev, shadow FROM sentinel_event {}ORDER BY id DESC LIMIT {}",
            growid.empty() ? "" : "WHERE growid = ? ", std::min<std::size_t>(limit, 50));
        ::hStmt hStmt{ query };
        MYSQL_BIND param = make_bind_in(growid);
        if (!growid.empty()) hStmt.bind_param(&param);

        long long ts{};
        std::string g{}, r{}, d{}, a{};
        signed sev{}, sh{};
        u_long l[4]{};
        MYSQL_BIND results[7] = { make_bind_out(ts), make_bind_out(g), make_bind_out(r), make_bind_out(d), make_bind_out(a), make_bind_out(sev), make_bind_out(sh) };
        for (int i = 0; i < 4; ++i) results[i + 1].length = &l[i];
        mysql_stmt_bind_result(hStmt.pStmt, results);
        hStmt.execute();
        while (mysql_stmt_fetch(hStmt.pStmt) == 0)
            out.push_back({ ts, g.substr(0, l[0]), r.substr(0, l[1]), d.substr(0, l[2]), a.substr(0, l[3]), sev == 1, sh == 1 });
        return out;
    }

    today counts_today()
    {
        today t{};
        ::hStmt hStmt{ "SELECT CAST(COALESCE(SUM(sev = 1 AND shadow = 0), 0) AS SIGNED), CAST(COALESCE(SUM(sev = 0), 0) AS SIGNED), "
                       "CAST(COALESCE(SUM(action IN ('ban', 'tempban')), 0) AS SIGNED) FROM sentinel_event WHERE ts >= CURDATE()" };
        MYSQL_BIND results[3] = { make_bind_out(t.proofs), make_bind_out(t.softs), make_bind_out(t.bans) };
        mysql_stmt_bind_result(hStmt.pStmt, results);
        hStmt.execute();
        mysql_stmt_fetch(hStmt.pStmt);
        return t;
    }

    bool shadow_active() { return in_shadow(); }
    long long shadow_left() { return std::max<long long>(0, g_shadow_until - now_s()); }
    long long brake_left() { return std::max<long long>(0, g_brake_until - now_s()); }
    void release_brake() { g_brake_until = 0; g_recent_enforce.clear(); }
    void set_level(lvl l) { g_level = l; cfg_set("level", static_cast<long long>(l)); }
    const char *level_name(lvl l) { return l == lvl::CAREFUL ? "Hati-hati" : l == lvl::FIERCE ? "Galak" : "Tanpa ampun"; }

    bool record_start(const std::string &growid, const std::string &label, std::string &out_error)
    {
        const std::string key = lower(growid);
        if (g_recordings.contains(key)) { out_error = "Already recording for " + growid + ". Stop it first."; return false; }

        std::filesystem::create_directories("recordings");
        const std::string file_name = std::format("recordings/{}_{}_{}.csv", safe_label(label), safe_label(growid), now_s());

        recording r{};
        r.file.open(file_name, std::ios::out | std::ios::trunc);
        if (!r.file) { out_error = "Could not create " + file_name; return false; }
        r.file << "# label=" << label << " growid=" << growid << " started=" << now_s() << '\n';
        r.file << "ms_since_start,kind,x,y,arg\n";
        r.start_ms = now_ms();
        r.label = label;
        g_recordings.emplace(key, std::move(r));
        return true;
    }

    bool record_stop(const std::string &growid, std::string &out_message)
    {
        const std::string key = lower(growid);
        auto it = g_recordings.find(key);
        if (it == g_recordings.end()) { out_message = "Nothing is being recorded for " + growid + "."; return false; }

        const long long rows = it->second.rows;
        const std::string label = it->second.label;
        it->second.file.flush();
        it->second.file.close();
        g_recordings.erase(it);
        out_message = std::format("Stopped recording \"{}\" for {}: {} action(s) saved.", label, growid, rows);
        return true;
    }

    std::string record_status(const std::string &growid)
    {
        auto it = g_recordings.find(lower(growid));
        if (it == g_recordings.end()) return "";
        return std::format("recording \"{}\", {} action(s) so far", it->second.label, it->second.rows);
    }
}
