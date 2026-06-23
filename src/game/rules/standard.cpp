#include <game/rules/standard.hpp>
#include "game/effects/standard.hpp"
#include <common/game/effect.hpp>

namespace game {

    void StandardRule::ValidatePlay(const GameState* state, CardPlayedEvent& event) {
        if (event.is_out_of_turn) {
            event.is_valid_play = false;
            return;
        }

        Type played_type = GetType(event.played_card);
        Value played_value = GetValue(event.played_card);

        if (played_type == Type::kWild) {
            return;
        }

        // INFO: We check active_type since the top card might be a Wild.
        //       Since its inherent type is kWild and would return false, we
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
    
    void StandardRule::OnCardPlayed(GameState* state, CardPlayedEvent& event) {
        Value card_value = GetValue(event.played_card);
    
        if (card_value == Value::kSkip) {
            state->effect_queue.push_back(std::make_unique<SkipEffect>());
        } 
        else if (card_value == Value::kReverse) {
            state->effect_queue.push_back(std::make_unique<ReverseEffect>());
        } 
        else if (card_value == Value::kDraw2) {
            std::string next_player = GetNextPlayer(state);
            state->effect_queue.push_back(std::make_unique<DrawEffect>(2, next_player));
            state->effect_queue.push_back(std::make_unique<SkipEffect>());
        } 
        else if (card_value == Value::kWild) {
            state->effect_queue.push_back(std::make_unique<ChooseColorEffect>(event.player_username));
        } 
        else if (card_value == Value::kWildDraw4) {
            std::string next_player = GetNextPlayer(state);
            state->effect_queue.push_back(std::make_unique<ChooseColorEffect>(event.player_username));
            state->effect_queue.push_back(std::make_unique<DrawEffect>(4, next_player));
            state->effect_queue.push_back(std::make_unique<SkipEffect>());
        }
    
        // UNO Penalty
        auto player_iterator = std::ranges::find(state->players, event.player_username, &Player::username);
        if (player_iterator != state->players.end() && player_iterator->hand.size() == 1 && !player_iterator->has_called_uno) {
            state->effect_queue.push_back(std::make_unique<DrawEffect>(2, event.player_username));
        }

        player_iterator->has_called_uno = false;
    }

    std::string StandardRule::GetNextPlayer(GameState* state) {
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
}
