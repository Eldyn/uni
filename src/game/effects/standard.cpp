#include "common/game/effect.hpp"
#include "game/game_state.hpp"
#include "game/match_instance.hpp"
#include <game/effects/standard.hpp>
#include <algorithm>

namespace game {

    EffectResult AdvanceTurnEffect::Resolve(GameState* state, MatchInstance* match) {
        state->current_player_index += state->play_direction;
        int total_players = static_cast<int>(state->players.size());
        
        if (state->current_player_index >= total_players) {
            state->current_player_index %= total_players;
        } else while (state->current_player_index < 0) {
            state->current_player_index += total_players;
        }
        return {EffectStatus::kResolved, "", ""};
    }
    
    EffectResult DrawEffect::Resolve(GameState* state, MatchInstance* match) {
        auto player_iterator = std::ranges::find(state->players, target_username_, &Player::username);
        if (player_iterator != state->players.end()) {
            for (int draw_index = 0; draw_index < count_; ++draw_index) {
                if (state->draw_pile.empty()) {
                    ReshuffleDiscardIntoDraw(state);
                    if (state->draw_pile.empty()) break; 
                }
                player_iterator->hand.push_back(state->draw_pile.back());
                state->draw_pile.pop_back();
            }
            player_iterator->has_called_uno = false; 
        }
        return {EffectStatus::kResolved, "", ""};
    }
    
    EffectResult SkipEffect::Resolve(GameState* state, MatchInstance* match) {
        state->effect_queue.push_front(std::make_unique<AdvanceTurnEffect>());
        return {EffectStatus::kResolved, "", ""};
    }
    
    EffectResult ReverseEffect::Resolve(GameState* state, MatchInstance* match) {
        state->play_direction *= -1;
        if (state->players.size() == 2) {
            state->effect_queue.push_front(std::make_unique<AdvanceTurnEffect>());
        }
        return {EffectStatus::kResolved, "", ""};
    }
    
    EffectResult ChooseColorEffect::Resolve(GameState* state, MatchInstance* match) {
        // TODO: switch from "COLOR" to Color::kColor input
        if (!state->provided_input.empty()) {
            if (state->provided_input == "RED") state->active_color = Color::kRed;
            else if (state->provided_input == "BLUE") state->active_color = Color::kBlue;
            else if (state->provided_input == "GREEN") state->active_color = Color::kGreen;
            else if (state->provided_input == "YELLOW") state->active_color = Color::kYellow;
            
            state->provided_input.clear();
            return {EffectStatus::kResolved, "", ""};
        }
        return {EffectStatus::kNeedsInput, "choose_color", target_username_, ""};
    }
    
    EffectResult DecideDrawnCardEffect::Resolve(GameState* state, MatchInstance* match) {
        if (!state->provided_input.empty()) {
            std::string choice = state->provided_input;
            state->provided_input.clear();
    
            if (choice == "PLAY") {
                match->PlayCard(username_, card_id_);
            } else {
                state->effect_queue.push_back(std::make_unique<AdvanceTurnEffect>());
            }
            return {EffectStatus::kResolved, "", "", ""};
        }
    
        nlohmann::json action_context = {
            {"card_id", card_id_},
            {"message", "You drew a playable card!"}
        };
    
        return {EffectStatus::kNeedsInput, "play_drawn_card", username_, action_context.dump()};
    }

} 
