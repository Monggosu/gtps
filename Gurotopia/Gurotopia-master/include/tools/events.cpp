#include "pch.hpp"

#include <ctime>

#include "events.hpp"

namespace
{
    struct boost_state
    {
        double multiplier{1.0};
        long long until{0}; // @note unix time
    };
    boost_state gems_boost{}, xp_boost{};

    long long now() { return static_cast<long long>(std::time(nullptr)); }
    boost_state &state(bool xp) { return xp ? xp_boost : gems_boost; }
}

double gem_multiplier() { return (now() < gems_boost.until) ? gems_boost.multiplier : 1.0; }
double xp_multiplier()  { return (now() < xp_boost.until)   ? xp_boost.multiplier   : 1.0; }

void set_boost(bool xp, double multiplier, int minutes)
{
    boost_state &s = state(xp);
    if (minutes <= 0 || multiplier <= 1.0) { s = {}; return; }
    s.multiplier = multiplier;
    s.until = now() + static_cast<long long>(minutes) * 60;
}

long long boost_seconds_left(bool xp)
{
    const long long left = state(xp).until - now();
    return left > 0 ? left : 0;
}
