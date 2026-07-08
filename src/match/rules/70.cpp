#include <common/match/matchrule.hpp>
#include <common/match/effect.hpp>
#include <match/match_state.hpp>
#include <match/match_instance.hpp>
#include <match/rule_registry.hpp>
#include <match/effects/pass_hands.hpp>
#include <match/effects/decide_swap_target.hpp>
#include <nlohmann/json.hpp>

namespace match {
    class SevenZeroRule : public MatchRule {
    public:
        ~SevenZeroRule() override = default;

        void ValidatePlay(const MatchState* state, CardPlayedEvent& event) override {}

        void OnCardPlayed(MatchState* state, CardPlayedEvent& event) override {
            Value card_value = GetValue(event.played_card);

            if (card_value == Value::k0)
                state->effect_queue.push_back(std::make_unique<PassHandsEffect>());
            else if (card_value == Value::k7)
                state->effect_queue.push_back(
                    std::make_unique<DecideSwapTargetEffect>(event.player_username));
        }
    };

    static RuleRegistrar registrar("seven_zero", []() {
        return std::make_unique<SevenZeroRule>();
    }, "Seven-Zero",
       "Playing a 7 swaps your hand with a chosen player; playing a 0 rotates all hands.");
}
