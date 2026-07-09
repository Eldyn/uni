/**
 * @file lobby.cpp
 * @brief Implementation of LobbySettings validation policy.
 */
#include "common/lobby.hpp"
#include <match/rule_registry.hpp>
#include <algorithm>
#include <unordered_set>

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
