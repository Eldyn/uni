#include <nlohmann/json.hpp>
#include <common/match/matchrule.hpp>
#include <common/match/effect.hpp>
#include <match/match_state.hpp>
#include <match/match_instance.hpp>
#include <match/effects/pass_hands.hpp>
#include <match/effect_registry.hpp>

namespace match {
    EffectResult PassHandsEffect::Resolve(MatchState* state, MatchInstance* match) {
        int num_players = static_cast<int>(state->players.size());
        if (num_players <= 1) return {EffectStatus::kResolved};

        std::vector<std::vector<CompactCard>> new_hands(num_players);

        for (int i = 0; i < num_players; ++i) {
            // Calculate who receives this hand based on play direction
            int next_idx = (i + state->play_direction + num_players) % num_players;
            new_hands[next_idx] = std::move(state->players[i].hand);
        }

        // Apply the rotated hands back to the state
        for (int i = 0; i < num_players; ++i) {
            state->players[i].hand = std::move(new_hands[i]);
        }

        return {EffectStatus::kResolved};
    }

    static EffectRegistrar reg_pass(EffectType::kPassHands, [](const auto&) {
        return std::make_unique<PassHandsEffect>();
    });
}
