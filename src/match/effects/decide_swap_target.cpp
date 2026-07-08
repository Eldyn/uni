#include <nlohmann/json.hpp>
#include <common/match/matchrule.hpp>
#include <common/match/effect.hpp>
#include <match/match_state.hpp>
#include <match/match_instance.hpp>
#include <match/effects/decide_swap_target.hpp>
#include <match/effects/pass_hands.hpp>
#include <match/effect_registry.hpp>

namespace match {
    EffectResult DecideSwapTargetEffect::Resolve(MatchState* state, MatchInstance* match) {
        if (!state->provided_input.empty()) {
            std::string target_username = state->provided_input;
            state->provided_input.clear();

            Logger::Info("[Effect] 7-Swap received target: '", target_username,
                         "' from player: '", username_, "'");

            auto p1 = std::ranges::find(state->players, username_, &Player::username);
            auto p2 = std::ranges::find(state->players, target_username, &Player::username);

            if (p1 != state->players.end() && p2 != state->players.end() && p1 != p2) {
                Logger::Info("[Effect] Executing swap between ", p1->username, " and ",
                             p2->username);
                std::swap(p1->hand, p2->hand);
                std::swap(p1->has_called_uno, p2->has_called_uno);

                return {EffectStatus::kResolved};
            }

            Logger::Warn("Invalid target for 7 swap: ", target_username);
        }

        nlohmann::json action_context = nlohmann::json::array();
        for (const auto& p : state->players) {
            if (p.username != username_) {
                action_context.push_back(p.username);
            }
        }

        return {EffectStatus::kNeedsInput, Action::kChooseTarget, username_, action_context};
    }

    static EffectRegistrar reg_swap(EffectType::kDecideSwapTarget, [](const auto& e) {
        return std::make_unique<DecideSwapTargetEffect>(e.value("username", ""));
    });
}
