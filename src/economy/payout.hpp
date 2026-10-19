#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/**
 * @file payout.hpp
 * @brief The match-end payout: base, win and the first-win-of-the-day bonus.
 *
 * `PayMatch` is a pure function over an `EconomyService`: it decides who is
 * paid and how much from the match data and the wall clock the caller passes
 * in, then writes every credit through the ledger service. Every credit is
 * keyed by the match id, so a repeated call for one match pays once. A match
 * pays only when at least two registered humans took part; bots, guests,
 * quitters and creative matches pay nothing.
 */
namespace uni::economy {

class EconomyService;

/** @brief One player in a finished match, as the engine exported them. */
struct PayoutParticipant {
    std::string username;
    bool is_bot = false;
    bool quit = false;
};

/** @brief Everything the payout needs about one finished match. */
struct PayoutInput {
    std::string match_id;
    bool creative = false;
    std::vector<PayoutParticipant> participants;
    std::string winner;
    std::int64_t now = 0;
};

/** @brief Cash credited by one `PayMatch` call, by username (only > 0). */
using PayoutEarnings = std::map<std::string, std::int64_t>;

/**
 * @brief Pay every eligible participant of a finished match.
 *
 * Base 20 for finishing, win 30 for the winner and daily 50 for the winner's
 * first win of the UTC day. No-op for a creative match, a match with fewer than
 * two registered humans, and any participant who is a bot, a guest or quit.
 * @param economy The ledger service the credits are written through.
 * @param in      The finished match: id, mode, players, winner and server time.
 * @return The amount each participant earned in this call; a participant who
 *         earned nothing (or was already paid) is absent.
 */
PayoutEarnings PayMatch(EconomyService& economy, const PayoutInput& in);

}  // namespace uni::economy
