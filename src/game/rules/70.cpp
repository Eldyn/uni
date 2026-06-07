#include <common/game/gamerule.hpp>
#include <common/game/effect.hpp>
#include <game/game_state.hpp>
#include <game/match_instance.hpp>
#include <game/rule_registry.hpp>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace game {

    class PassHandsEffect : public Effect {
    public:
        EffectResult Resolve(GameState* state, MatchInstance* match) override {
            int num_players = static_cast<int>(state->players.size());
            if (num_players <= 1) return {EffectStatus::kResolved, "", ""};

            std::vector<std::vector<CompactCard>> new_hands(num_players);
            std::vector<bool> new_uno_status(num_players);

            for (int i = 0; i < num_players; ++i) {
                // Calculate who receives this hand based on play direction
                int next_idx = (i + state->play_direction + num_players) % num_players;
                new_hands[next_idx] = std::move(state->players[i].hand);
                new_uno_status[next_idx] = state->players[i].has_called_uno;
            }

            // Apply the rotated hands back to the state
            for (int i = 0; i < num_players; ++i) {
                state->players[i].hand = std::move(new_hands[i]);
                state->players[i].has_called_uno = new_uno_status[i];
            }

            return {EffectStatus::kResolved, "", ""};
        }
    };

    class ChooseSwapTargetEffect : public Effect {
        std::string username_;
    public:
        ChooseSwapTargetEffect(std::string username) : username_(std::move(username)) {}

        EffectResult Resolve(GameState* state, MatchInstance* match) override {
            // 2. If the player provided a target username, execute the swap!
            if (!state->provided_input.empty()) {
                std::string target_username = state->provided_input;
                state->provided_input.clear();

                auto p1 = std::ranges::find(state->players, username_, &Player::username);
                auto p2 = std::ranges::find(state->players, target_username, &Player::username);

                // Ensure both players exist and aren't the same person
                if (p1 != state->players.end() && p2 != state->players.end() && p1 != p2) {
                    std::swap(p1->hand, p2->hand);
                    std::swap(p1->has_called_uno, p2->has_called_uno);
                }

                Logger::Warn("Tried resolving 7");
                return {EffectStatus::kResolved, "", ""};
            }

            // 1. Ask the frontend who they want to swap with.
            // We pass the list of valid opponents in the action context.
            nlohmann::json action_context = nlohmann::json::array();
            for (const auto& p : state->players) {
                if (p.username != username_) {
                    action_context.push_back(p.username);
                }
            }

            return {EffectStatus::kNeedsInput, "choose_target", username_, action_context.dump()};
        }
    };


    class SevenZeroRule : public GameRule {
    public:
        ~SevenZeroRule() override = default;

        void ValidatePlay(GameState* state, CardPlayedEvent& event) override {}

        void OnCardPlayed(GameState* state, CardPlayedEvent& event) override {
            Value card_value = GetValue(event.played_card);

            if (card_value == Value::k0) state->effect_queue.push_back(std::make_unique<PassHandsEffect>());
            else if (card_value == Value::k7) state->effect_queue.push_back(std::make_unique<ChooseSwapTargetEffect>(event.player_username));
        }
    };

    static RuleRegistrar registrar("seven_zero", []() { 
        return std::make_unique<SevenZeroRule>(); 
    });
}
