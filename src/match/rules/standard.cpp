#include <match/rules/standard.hpp>
#include "match/effects/standard.hpp"
#include <common/match/effect.hpp>

namespace match {

    void StandardRule::ValidatePlay(const MatchState* state, CardPlayedEvent& event) {
        if (event.is_out_of_turn) {
            event.is_valid_play = false;
            return;
        }

        Type played_type = GetType(event.played_card);
        Value played_value = GetValue(event.played_card);

        if (played_type == Type::kWhite) {
            return;
        }

        // INFO: We check active_type since the top card might be a White.
        //       Since its inherent type is kWhite and would return false, we
        //       use the active_type which is whatever the player chose.
        if (played_type == state->active_type) {
            return;
        }

        if (!state->discard_pile.empty()) {
            CompactCard top_card = state->discard_pile.back();
            Value top_value = GetValue(top_card);

            if (played_value == top_value) {
                return;
            }
        }

        event.is_valid_play = false;
    }

    void StandardRule::OnCardPlayed(MatchState* state, CardPlayedEvent& event) {
        Value card_value = GetValue(event.played_card);

        if (card_value == Value::kSkip) {
            state->effect_queue.push_back(std::make_unique<SkipEffect>());
        } else if (card_value == Value::kReverse) {
            state->effect_queue.push_back(std::make_unique<ReverseEffect>());
        } else if (card_value == Value::kDraw2) {
            std::string next_player = GetNextPlayer(state);
            state->effect_queue.push_back(std::make_unique<DrawEffect>(2, next_player));
            state->effect_queue.push_back(std::make_unique<SkipEffect>());
        } else if (card_value == Value::kJolly) {
            state->effect_queue.push_back(
                std::make_unique<ChooseColorEffect>(event.player_username));
        } else if (card_value == Value::kJollyDraw4) {
            std::string next_player = GetNextPlayer(state);
            state->effect_queue.push_back(
                std::make_unique<ChooseColorEffect>(event.player_username));
            state->effect_queue.push_back(std::make_unique<DrawEffect>(4, next_player));
            state->effect_queue.push_back(std::make_unique<SkipEffect>());
        }
    }

    std::string StandardRule::GetNextPlayer(MatchState* state) {
        int next_index = state->current_player_index + state->play_direction;
        int total_players = static_cast<int>(state->players.size());

        // Safely wrap around the array bounds
        if (next_index >= total_players) {
            next_index %= total_players;
        } else {
            while (next_index < 0) {
                next_index += total_players;
            }
        }

        return state->players[next_index].username;
    }
}  // namespace match
