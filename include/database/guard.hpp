#pragma once

#include <array>
#include <ctime>

/* memory of the security system (Sentinel) about one player. lives inside ::peer, never saved to the database. */
struct guard_state
{
    struct act { u_int ms{}; u_char kind{}; short x{}, y{}; short arg{}; };

    /* flight recorder: the last 48 things the player did */
    std::array<act, 48> ring{};
    u_char head{0};
    u_short recorded{0};

    /* packet firewall: token buckets for movement / tile actions / text / everything else */
    std::array<float, 4> tokens{ 120.0f, 40.0f, 25.0f, 60.0f };
    u_int bucket_ms{0};
    u_short dropped{0};
    u_int drop_window_ms{0};

    /* position tracking (pixels) and a grace period after the SERVER moved the player */
    float last_x{0}, last_y{0};
    u_int last_move_ms{0};
    u_int grace_until_ms{0};

    /* behaviour statistics for farming bots */
    u_int farm_start_ms{0};
    u_int last_punch_ms{0};
    std::array<u_short, 40> gaps{};
    u_char gap_n{0}, gap_head{0};
    short last_tx{-100}, last_ty{-100}, last_dx{0}, last_dy{0};
    u_short sweep_hits{0}, sweep_total{0};
    u_char fast_windows{0}, even_windows{0};   // @note consecutive 40-hit windows that looked like a machine (a lag burst cannot fake three in a row)

    /* small counters with their own time windows */
    u_char own_misses{0};   u_int own_miss_ms{0};
    u_char spam_strikes{0}; u_int spam_ms{0};
    u_char exceptions{0};   u_int exception_ms{0};

    /* last time (unix seconds) each weak signal fired: two different ones within 10 minutes count as proof */
    std::array<std::time_t, 16> soft_at{};
};
