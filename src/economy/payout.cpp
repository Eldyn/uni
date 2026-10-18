#include "economy/payout.hpp"

#include <economy/economy_service.hpp>

#include <ctime>
#include <string>

/**
 * @file payout.cpp
 * @brief Implements the match-end payout. Everything the function decides comes
 *        from `PayoutInput`; the ledger keys make each credit idempotent.
 */

namespace uni::economy {

namespace {

/** @brief Payout amounts, per the economy design (section 3). */
constexpr std::int64_t kBase = 20;
constexpr std::int64_t kWin = 30;
constexpr std::int64_t kDaily = 50;

/** @brief The UTC calendar day of `now_unix`, as `yyyy-mm-dd`. */
std::string UtcDay(std::int64_t now_unix) {
    const std::time_t seconds = static_cast<std::time_t>(now_unix);
    std::tm utc {};
    gmtime_r(&seconds, &utc);
    char day[11] = {};
    std::strftime(day, sizeof(day), "%Y-%m-%d", &utc);
    return day;
}

}  // namespace

void PayMatch(EconomyService& economy, const PayoutInput& in) {
    if (in.creative) return;

    /* INFO: a match pays only when at least two registered humans took part.
     *       A guest has no account, so Balance() is empty for them. */
    int registered_humans = 0;
    for (const PayoutParticipant& participant : in.participants) {
        if (participant.is_bot) continue;
        if (economy.Balance(participant.username).has_value()) {
            ++registered_humans;
        }
    }
    if (registered_humans < 2) return;

    const std::string daily_ref = "daily:" + UtcDay(in.now);

    for (const PayoutParticipant& participant : in.participants) {
        if (participant.is_bot || participant.quit) continue;
        if (!economy.Balance(participant.username).has_value()) continue;

        (void)economy.Credit(participant.username, kBase, "match_base",
                             in.match_id, in.now);
        if (participant.username != in.winner) continue;
        (void)economy.Credit(participant.username, kWin, "match_win",
                             in.match_id, in.now);
        /* The day in the ref makes the unique key pay the bonus once a day. */
        (void)economy.Credit(participant.username, kDaily, "match_daily",
                             daily_ref, in.now);
    }
}

}  // namespace uni::economy
