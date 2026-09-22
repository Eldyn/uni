/**
 * @file lobby.cpp
 * @brief Implementation of LobbySettings validation policy and Lobby bot-count
 * reconciliation.
 */
#include "common/lobby.hpp"
#include "websocket_context.hpp"
#include <common/bot_names.hpp>
#include <logger.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_instance.hpp>
#include <match/server/match_session.hpp>
#include <openssl/rand.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
constexpr char kCodeAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr int  kCodeLen        = 6;
constexpr int  kAlphabetLen    = 36;
}  // namespace

// INFO: The move operations and destructor are defined here, where the
//       incomplete `unique_ptr` member type (`match::server::MatchSession`)
//       is complete. Declaring them in the header suppresses the implicit
//       special members, so all four are written out explicitly.
Lobby::Lobby() = default;
Lobby::Lobby(Lobby&&) noexcept = default;
Lobby& Lobby::operator=(Lobby&&) noexcept = default;
Lobby::~Lobby() = default;

void LobbySettings::Sanitize(int max_players_ceiling) {
    turn_time_limit_ms = std::clamp(turn_time_limit_ms,
                                     contract::kTurnTimeMinMs, contract::kTurnTimeMaxMs);
    starting_cards = std::clamp(starting_cards,
                                 contract::kStartingCardsMin, contract::kStartingCardsMax);
    bot_count = std::clamp(bot_count, contract::kBotCountMin, contract::kBotCountMax);
    max_players = std::clamp(max_players, 2, max_players_ceiling);
    if (mode != "elimination") mode = "standard";
    survivor_count = std::clamp(survivor_count, 1, std::max(1, max_players - 1));

    const int deck_size = DeckSize();
    if (deck_size > 0) {
        starting_cards = std::max(1, std::min(starting_cards, deck_size / max_players));
    }

    bot_mode = static_cast<BotTakeoverMode>(std::clamp(
        static_cast<int>(bot_mode), contract::kBotModeMin, contract::kBotModeMax));

    // INFO: Mod names are no longer validated against the legacy rule
    //       registry; the mod loader / assembler ignores unknown ids. Only
    //       deduplicate here, preserving the caller's order.
    std::vector<std::string> sanitized_mods;
    std::unordered_set<std::string> seen_mods;
    for (auto& mod : active_mods) {
        if (!seen_mods.insert(mod).second) continue;
        sanitized_mods.push_back(mod);
    }
    active_mods = std::move(sanitized_mods);
}

std::string Lobby::PickBotName(std::mt19937& rng) const {
    std::vector<std::string> available;

    for (const auto& name : match::kReservedBotNames) {
        bool taken = std::ranges::any_of(members, [&](const LobbyMember& m) {
            return m.username == name;
        });
        if (!taken) available.push_back(name);
    }

    if (available.empty()) {
        int fallback_id = static_cast<int>(members.size()) + 1;
        return "Bot_" + std::to_string(fallback_id);
    }

    std::uniform_int_distribution<> dist(0, static_cast<int>(available.size()) - 1);
    return available[dist(rng)];
}

int Lobby::NextFreeSeat() const {
    std::unordered_set<int> taken;
    for (const auto& member : members) taken.insert(member.seat_index);

    for (int seat = 0; seat < settings.max_players; ++seat) {
        if (taken.find(seat) == taken.end()) return seat;
    }
    return static_cast<int>(members.size());
}

void Lobby::SyncBots(std::mt19937& rng) {
    if (session) return;
    int human_count = 0;
    int bot_count = 0;

    for (const auto& member : members) {
        if (member.is_bot) bot_count++;
        else human_count++;
    }

    int desired_bots = settings.bot_count;
    if (human_count + desired_bots > settings.max_players) {
        desired_bots = settings.max_players - human_count;
    }

    while (bot_count < desired_bots) {
        std::string bot_name = PickBotName(rng);
        members.emplace_back(bot_name, nullptr, true, true, NextFreeSeat());
        bot_count++;
    }

    while (bot_count > desired_bots) {
        for (auto it = members.rbegin(); it != members.rend(); ++it) {
            if (it->is_bot) {
                members.erase(std::next(it).base());
                bot_count--;
                break;
            }
        }
    }
}

MemberRemovalResult Lobby::RemoveMember(const std::string& username, std::mt19937& rng) {
    MemberRemovalResult result;
    // INFO: bot-name selection is only needed by SyncBots now; a mid-game
    //       replacement keeps the departing username so the engine mapping
    //       survives.
    (void)rng;

    auto member_it = std::ranges::find(members, username, &LobbyMember::username);
    if (member_it == members.end()) return result;

    result.found = true;
    result.was_connected = member_it->is_connected && member_it->socket;
    result.socket = member_it->socket;

    if (!session) {
        members.erase(member_it);
        return result;
    }

    // INFO: A live match never loses a seated engine player
    //       mid-game. Departure policy only touches lobby bookkeeping; a
    //       departed seat is driven by the turn timeout / bot policy.
    const std::string old_name = member_it->username;
    result.was_their_turn =
        (session->Engine().GetCurrentPlayerUsername() == old_name);

    if (settings.quit_deletes_match) {
        result.match_outcome = MemberRemovalOutcome::kMatchAborted;
        result.old_username = old_name;
        members.erase(member_it);
    } else if (settings.allow_bot_replacement) {
        // INFO: keep the seat and username so the engine mapping holds; the
        //       member flag alone routes the seat through the bot policy.
        member_it->is_bot = true;
        member_it->is_connected = true;
        member_it->socket = nullptr;
        member_it->disconnected_at = std::chrono::steady_clock::time_point{};

        result.match_outcome = MemberRemovalOutcome::kPlayerReplacedByBot;
        result.old_username = old_name;
        result.new_bot_name = old_name;
    } else {
        members.erase(member_it);

        result.match_outcome = MemberRemovalOutcome::kPlayerDroppedFromEngine;
        result.old_username = old_name;
    }

    return result;
}

bool Lobby::PromoteNextHost() {
    for (const auto& m : members) {
        if (m.is_connected && !m.is_bot) {
            host = m.username;
            return true;
        }
    }
    return false;
}

JoinResult Lobby::AddOrHijack(const std::string& username, AppWebSocket* socket) {
    JoinResult result;

    bool user_privacy = false;
    if (socket) {
        auto* data = socket->getUserData();
        if (data) user_privacy = data->privacy_mode;
    }

    if (settings.allow_bot_takeover) {
        for (auto& member : members) {
            if (!member.is_bot) continue;
            const std::string old_bot_name = member.username;

            // INFO: A mid-match hijack rebinds the existing engine
            //       seat (username + socket) so the engine and the lobby stay
            //       in sync. When the seat cannot be rebound the hijack is
            //       skipped (logged) and the joiner falls through to the
            //       spectator path below.
            if (session &&
                !session->RebindPlayer(old_bot_name, username, socket)) {
                Logger::Warn("[Lobby] Mid-game hijack of bot '", old_bot_name,
                             "' by '", username, "' skipped: rebind failed");
                continue;
            }

            member.username = username;
            member.socket = socket;
            member.is_connected = true;
            member.is_bot = false;
            // A bot just became a human: keep the configured bot target in
            // sync so later SyncBots runs don't re-add a phantom bot.
            if (settings.bot_count > 0) settings.bot_count--;
            member.is_spectator = false;
            member.privacy_mode = user_privacy;

            result.outcome = JoinOutcome::kHijackedBot;
            result.old_bot_name = old_bot_name;
            return result;
        }
    }

    if (session) {
        // When match in progress and no bot seat was hijacked, join as spectator!
        members.emplace_back(username, socket, true, false, -1, /*is_spectator=*/true,
                             user_privacy);
        result.outcome = JoinOutcome::kJoinedAsSpectator;
        return result;
    }

    if (static_cast<int>(members.size()) < settings.max_players) {
        int seat = NextFreeSeat();
        members.emplace_back(username, socket, true, false, seat, /*is_spectator=*/false,
                             user_privacy);

        result.outcome = JoinOutcome::kJoinedEmptySlot;
        return result;
    }

    result.outcome = JoinOutcome::kLobbyFull;
    return result;
}

Lobby Lobby::Create(uint32_t id, const std::string& host, AppWebSocket* host_socket,
                     bool is_public, const std::string& name, int turn_time_limit_ms,
                     int starting_cards,
                     const std::function<bool(const std::string&)>& code_taken,
                     int max_players_ceiling) {
    std::string code;
    int attempts = 0;
    do {
        code = GenerateInviteCode();
        if (++attempts > 10)
            throw std::runtime_error("[Lobby] Failed to generate unique code");
    } while (code_taken(code));

    Lobby lobby;
    lobby.id                          = id;
    lobby.settings.is_public          = is_public;
    lobby.settings.turn_time_limit_ms = turn_time_limit_ms;
    lobby.settings.starting_cards     = starting_cards;
    lobby.invite_code                 = code;
    lobby.host                        = host;
    lobby.name                        = name;
    lobby.members.emplace_back(host, host_socket, true, false, 0);
    lobby.settings.Sanitize(max_players_ceiling);

    return lobby;
}

std::string Lobby::GenerateInviteCode() {
    uint8_t raw[kCodeLen];
    if (RAND_bytes(raw, kCodeLen) != 1)
        throw std::runtime_error("[Lobby] RAND_bytes failed generating invite code");

    std::string code(kCodeLen, ' ');
    for (int i = 0; i < kCodeLen; ++i)
        code[i] = kCodeAlphabet[raw[i] % kAlphabetLen];
    return code;
}

std::vector<std::string> Lobby::CollectExpiredDisconnects(
    std::chrono::steady_clock::time_point now, int64_t grace_ms) const {
    std::vector<std::string> expired;
    for (const auto& m : members) {
        if (!m.is_connected && !m.is_bot) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m.disconnected_at)
                    .count();
            if (elapsed > grace_ms) expired.push_back(m.username);
        }
    }
    return expired;
}
