/**
 * @file lobby.cpp
 * @brief Implementation of LobbySettings validation policy and Lobby bot-count
 * reconciliation.
 */
#include "common/lobby.hpp"
#include <common/bot_names.hpp>
#include <match/rule_registry.hpp>
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

void LobbySettings::Sanitize() {
    turn_time_limit_ms = std::clamp(turn_time_limit_ms,
                                     contract::kTurnTimeMinMs, contract::kTurnTimeMaxMs);
    starting_cards = std::clamp(starting_cards,
                                 contract::kStartingCardsMin, contract::kStartingCardsMax);
    bot_count = std::clamp(bot_count, contract::kBotCountMin, contract::kBotCountMax);
    bot_mode = static_cast<BotTakeoverMode>(std::clamp(
        static_cast<int>(bot_mode), contract::kBotModeMin, contract::kBotModeMax));

    const auto& registered_rules = match::RuleRegistry::GetMap();
    std::vector<std::string> sanitized_mods;
    std::unordered_set<std::string> seen_mods;
    for (auto& mod : active_mods) {
        if (registered_rules.find(mod) == registered_rules.end()) continue;
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

void Lobby::SyncBots(std::mt19937& rng) {
    if (match) return;
    int human_count = 0;
    int bot_count = 0;

    for (const auto& member : members) {
        if (member.is_bot) bot_count++;
        else human_count++;
    }

    int desired_bots = settings.bot_count;
    if (human_count + desired_bots > contract::kMaxLobbyMembers) {
        desired_bots = contract::kMaxLobbyMembers - human_count;
    }

    while (bot_count < desired_bots) {
        std::string bot_name = PickBotName(rng);
        members.emplace_back(bot_name, nullptr, true, true);
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

std::string Lobby::GenerateInviteCode() {
    uint8_t raw[kCodeLen];
    if (RAND_bytes(raw, kCodeLen) != 1)
        throw std::runtime_error("[Lobby] RAND_bytes failed generating invite code");

    std::string code(kCodeLen, ' ');
    for (int i = 0; i < kCodeLen; ++i)
        code[i] = kCodeAlphabet[raw[i] % kAlphabetLen];
    return code;
}
