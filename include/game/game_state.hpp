#pragma once

#include <common/game/card_types.hpp>
#include <common/game/effect.hpp>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

namespace game {
    
    struct PlayerSessionStats {
        int color_counts[5] = {0};
        int value_counts[15] = {0}; // 0-9, Skip, Reverse, Draw2, ColorSwitch, Draw4
    };

    struct Player {
        std::string username;
        std::vector<CompactCard> hand;
        bool is_bot;
        bool has_called_uno = false;

        Player(std::string u, std::vector<CompactCard> h, bool b, bool uno) : username(u), hand(h), is_bot(b), has_called_uno(uno) {};
        Player(std::string u, bool b, bool uno) : username(u), is_bot(b), has_called_uno(uno) {};
    };
    
    enum class MatchStatus {
        kWaitingToStart,
        kPlaying,
        kFinished
    };

    // enum class InputType {
    //    kPlayDraw,
    //    kChooseColor
    // };

    // inline const std::unordered_map<InputType, std::string> kInputTypeStr {
    //     { game::InputType::kPlayDraw,    "play_drawn_card" },
    //     { game::InputType::kChooseColor, "choose_color"    }
    // };
        
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
