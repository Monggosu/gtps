#include "pch.hpp"

#include "economy.hpp"

void log_economy(const std::string &kind, int from_uid, int to_uid, long long amount, long long tax, const std::string &note)
{
    ::hStmt hStmt{ "INSERT INTO economy_log (kind, from_uid, to_uid, amount, tax, note) VALUES (?, ?, ?, ?, ?, ?)" };

    MYSQL_BIND params[6] = {
        make_bind_in(kind),
        make_bind_in(from_uid),
        make_bind_in(to_uid),
        make_bind_in(amount),
        make_bind_in(tax),
        make_bind_in(note)
    };
    hStmt.bind_param(params);
    hStmt.execute();
}

std::string format_gems(long long value)
{
    std::string digits = std::to_string(value < 0 ? -value : value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(i, ",");
    return (value < 0) ? "-" + digits : digits;
}
