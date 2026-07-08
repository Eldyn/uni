#include <common/match/matchrule.hpp>
#include <match/match_state.hpp>
#include <match/rule_registry.hpp>
#include <match/effects/standard.hpp>

namespace match {
    class DrawStackingRule : public MatchRule {
    public:
        void ValidatePlay(const MatchState* state, CardPlayedEvent& event) override {
            if (state->pending_draws > 0) {
                Value played_val = GetValue(event.played_card);
                Value top_val = GetValue(state->discard_pile.back());

                if (played_val != top_val ||
                    (played_val != Value::kDraw2 && played_val != Value::kJollyDraw4)) {
                    event.is_valid_play = false;
                    event.is_handled = true;
                }
            }
        }

        void OnCardPlayed(MatchState* state, CardPlayedEvent& event) override {
            Value card_val = GetValue(event.played_card);

            if (card_val == Value::kDraw2) {
                state->pending_draws += 2;
                event.is_handled = true;  // Flag handled so StandardRule doesn't push a skip!
            } else if (card_val == Value::kJollyDraw4) {
                // INFO: stack_bonus=4: pending_draws is incremented AFTER the
                //       colour choice resolves, so other players don't see the
                //       penalty grow before the colour is picked. Stacking
                //       still works because PlayCard is gated by
                //       IsWaitingForInput() until the colour effect resolves
                //       and the turn advances to the next player.
                state->effect_queue.push_back(
                    std::make_unique<ChooseColorEffect>(event.player_username, 4));
                event.is_handled = true;
            }
        }
    };

    static RuleRegistrar registrar("draw_stacking", []() {
        return std::make_unique<DrawStackingRule>();
    }, "Draw Stacking", "Stack +2 and +4 draw cards to pass the penalty to the next player.");
}
