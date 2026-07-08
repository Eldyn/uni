#include <common/match/matchrule.hpp>
#include <match/match_state.hpp>
#include <match/rule_registry.hpp>

namespace match {
    class JumpInRule : public MatchRule {
    public:
        void ValidatePlay(const MatchState* state, CardPlayedEvent& event) override {
            if (event.is_out_of_turn) {
                if (state->discard_pile.empty()) return;

                CompactCard top_card = state->discard_pile.back();

                if (GetType(event.played_card) == GetType(top_card) &&
                    GetValue(event.played_card) == GetValue(top_card)) {
                    event.is_valid_play = true;
                    event.is_handled = true;
                }
            }
        }

        void OnCardPlayed(MatchState* state, CardPlayedEvent& event) override {
            if (event.is_out_of_turn) {
                auto it = std::ranges::find(state->players, event.player_username,
                                             &Player::username);
                if (it != state->players.end()) {
                    state->current_player_index = std::distance(state->players.begin(), it);
                    Logger::Info("[Match] ", event.player_username, " successfully JUMPED IN!");
                }
            }
        }
    };

    static RuleRegistrar registrar("jump_in", []() {
        return std::make_unique<JumpInRule>();
    }, "Jump In", "Play an identical card (same number and color) out of turn to steal the turn.");
}
