#pragma once

#include <common/game/card_types.hpp>
#include <common/game/effect.hpp>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

namespace game {
    
    struct Player {
        std::string username;
        std::vector<CompactCard> hand;
        bool has_called_uno = false;
    };
    
    enum class MatchStatus {
        kWaitingToStart,
        kPlaying,
        kFinished
    };
        
    struct GameState {
        MatchStatus status = MatchStatus::kWaitingToStart;
        std::string winner; 
    
        std::vector<Player> players;
        int current_player_index = 0;
        int play_direction = 1; 
            
        std::vector<CompactCard> draw_pile;
        std::vector<CompactCard> discard_pile;
        Color active_color = Color::kRed; 
        
        std::deque<std::unique_ptr<Effect>> effect_queue;
        
        std::string pending_player;
        std::string pending_input_type;
        std::string pending_input_context; // The JSON payload string
        std::string provided_input; 

        std::chrono::steady_clock::time_point turn_end_time;
    };

    void ReshuffleDiscardIntoDraw(GameState* game_state);
}
