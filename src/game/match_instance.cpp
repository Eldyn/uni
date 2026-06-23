#include "game/rule_registry.hpp"
#include "logger.hpp"
#include <database.hpp>
#include <common/game/card_types.hpp>
#include <common/lobby.hpp>
#include <game/game_state.hpp>
#include <game/match_instance.hpp>
#include <game/rules/standard.hpp>
#include <game/effects/standard.hpp>
#include <common/game/effect.hpp>
#include <controllers/lobby_controller.hpp>
#include <algorithm>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace game {
    void ReshuffleDiscardIntoDraw(GameState* game_state) {
        if (game_state->discard_pile.size() <= 1) return; 
        
        CompactCard top_card = game_state->discard_pile.back();
        game_state->discard_pile.pop_back();
    
        game_state->draw_pile = std::move(game_state->discard_pile);
        game_state->discard_pile.push_back(top_card);
    
        std::random_device random_device;
        std::mt19937 random_generator(random_device());
        std::shuffle(game_state->draw_pile.begin(), game_state->draw_pile.end(), random_generator);
    }

    MatchInstance::MatchInstance(const std::vector<std::pair<std::string, bool>>& players_info, const LobbySettings& settings) : settings_(settings) {
        for (const auto& player_info : players_info) {
            state_.players.emplace_back(player_info.first, player_info.second, false);
        }

        for (const auto& mod_name : settings.active_mods) {
            auto new_rule = RuleRegistry::Create(mod_name);
            if (new_rule) {
                active_rules_.push_back(std::move(new_rule));
                Logger::Info("[Match] Loaded mod: ", mod_name);
            }
        }

        active_rules_.push_back(std::make_unique<StandardRule>());
    }

    json MatchInstance::ExportState() const {
        json state_json;
        
        state_json["status"] = state_.status;
        state_json["active_type"] = state_.active_type;
        state_json["current_player_index"] = state_.current_player_index;
        state_json["play_direction"] = state_.play_direction;
        state_json["rules"] = settings_.active_mods;

        state_json["discard_pile"] = state_.discard_pile;
        state_json["draw_pile"] = state_.draw_pile;

        json players_json = json::array();
        for (const Player player : state_.players) {
            json player_json;

            player_json["username"] = player.username;
            player_json["hand"] = player.hand;
            player_json["has_called_uno"] = player.has_called_uno;
            player_json["is_bot"] = player.is_bot;
            
            players_json.push_back(player_json);
        }

        state_json["players"] = players_json;
    
        return state_json;
    }

    MatchInstance::MatchInstance(const json& saved_state, const LobbySettings& settings) : settings_(settings) {
        std::vector<string> rules; saved_state["rules"].get_to(rules);
        for (const auto& rule : rules) {
            auto new_rule = RuleRegistry::Create(rule);
            if (new_rule) {
                active_rules_.push_back(std::move(new_rule));
                Logger::Info("[Match] Loaded mod: ", rule);
            }
        }
        active_rules_.push_back(std::make_unique<StandardRule>());

        state_.status = static_cast<MatchStatus>(saved_state["status"].get<int>());
        state_.active_type = static_cast<Type>(saved_state.value("active_type", 0));
        state_.current_player_index = saved_state["current_player_index"].get<int>();
        state_.play_direction = saved_state["play_direction"].get<int>();

        saved_state["draw_pile"].get_to(state_.draw_pile);
        saved_state["discard_pile"].get_to(state_.discard_pile);

        for (const auto& player_json : saved_state["players"]) {
            std::vector<CompactCard> hand;
            player_json["hand"].get_to(hand);

            state_.players.emplace_back(player_json["username"], hand, player_json.value("is_bot", false), player_json["has_called_uno"]);
        }
    }

    void MatchInstance::AddPlayerMidGame(const std::string& username, bool is_bot) {
        Player p(username, is_bot, false);

        for (int index = 0; index < settings_.starting_cards; ++index) {
            if (state_.draw_pile.empty()) {
                ReshuffleDiscardIntoDraw(&state_);
                if (state_.draw_pile.empty()) break; // Failsafe
            }
            p.hand.push_back(state_.draw_pile.back());
            state_.draw_pile.pop_back();
        }

        state_.players.push_back(p);
        Logger::Info("[Match] Hot-joined player ", username, " with ", p.hand.size(), " cards.");
    }

    void MatchInstance::RemovePlayerMidGame(const std::string& username) {
        auto it = std::ranges::find(state_.players, username, &Player::username);
        if (it != state_.players.end()) {
            int index_to_remove = std::distance(state_.players.begin(), it);

            // INFO: Safely dump their hand back into the draw pile so the
            //       cards aren't lost.
            state_.draw_pile.insert(state_.draw_pile.end(), it->hand.begin(), it->hand.end());
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(state_.draw_pile.begin(), state_.draw_pile.end(), g);

            state_.players.erase(it);

            if (state_.players.empty()) {
                state_.status = MatchStatus::kFinished;
                return;
            }

            // INFO: Shift the current turn index so the game doesn't skip a
            //       player!
            if (state_.current_player_index >= state_.players.size()) {
                state_.current_player_index %= state_.players.size();
            } else if (index_to_remove < state_.current_player_index) {
                state_.current_player_index--;
            }
        }
    }
    
    void MatchInstance::Start() {
        GenerateDeck();

        for (auto& player : state_.players) {
            for (int index = 0; index < settings_.starting_cards; ++index) {
                player.hand.push_back(state_.draw_pile.back());
                state_.draw_pile.pop_back();
            }
        }

        bool found_starter = false;

        for (int i = state_.draw_pile.size() - 1; i >= 0; --i) {
            int card_val = static_cast<int>(GetValue(state_.draw_pile[i]));

            if (card_val >= 0 && card_val <= 9) {
                state_.discard_pile.push_back(state_.draw_pile[i]);
                state_.draw_pile.erase(state_.draw_pile.begin() + i);
                found_starter = true;
                break;
            }
        }

        if (!found_starter) {
            state_.discard_pile.push_back(state_.draw_pile.back());
            state_.draw_pile.pop_back();
        }

        state_.active_type = GetType(state_.discard_pile.back());
        state_.status = MatchStatus::kPlaying;
    }
    
    void MatchInstance::Tick() {
        while (!state_.effect_queue.empty()) {
            auto current_effect = std::move(state_.effect_queue.front());
            state_.effect_queue.pop_front();
            
            EffectResult result = current_effect->Resolve(&state_, this); 
    
            if (result.status == EffectStatus::kNeedsInput) {
                state_.pending_player = result.target_player;
                state_.pending_action = result.action;
                state_.pending_input_context = result.input_context;
                
                state_.effect_queue.push_front(std::move(current_effect));
                return; 
            }
        }
    }
    
    bool MatchInstance::PlayCard(const std::string& username, uint16_t card_id) {
        if (IsWaitingForInput() || IsGameOver()) return false;

        Player* current_player = GetPlayer(username);
        if (!current_player) return false;

        bool is_out_of_turn = (GetCurrentPlayerUsername() != username);
     
        auto card_iterator = std::ranges::find(current_player->hand, card_id, GetId);
        if (card_iterator == current_player->hand.end()) return false;

        CompactCard played_card = *card_iterator;
        CardPlayedEvent play_event = { username, played_card, !is_out_of_turn, false, is_out_of_turn };

        for (auto& rule : active_rules_) {
            rule->ValidatePlay(&state_, play_event);
            if (play_event.is_handled) break;
        }
        
        if (!play_event.is_valid_play) return false;

        // INFO: Record provenance (who + which hand slot) so clients can
        //       animate the card flying from the exact slot it left, onto the
        //       discard pile.
        state_.last_play = {
            true,
            username,
            static_cast<int>(std::distance(current_player->hand.begin(), card_iterator)),
            played_card
        };

        state_.discard_pile.push_back(played_card);
        state_.active_type = GetType(played_card);
        current_player->hand.erase(card_iterator);

        // INFO: we want to track user stats now :)
        Type c = GetType(played_card);
        Value v = GetValue(played_card);

        session_stats_[username].color_counts[static_cast<int>(c)]++;
        session_stats_[username].value_counts[static_cast<int>(v)]++;

        play_event.is_handled = false;

        for (auto& rule : active_rules_) {
            rule->OnCardPlayed(&state_, play_event);
            if (play_event.is_handled) break;
        }

        state_.effect_queue.push_back(std::make_unique<AdvanceTurnEffect>());

        if (current_player->hand.empty()) {
            state_.status = MatchStatus::kFinished;
            state_.winner = username;

            try {
                auto& db = Database::Get();
                if (!db.IsOpen()) {
                    throw std::runtime_error("DB is not open");
                }

                TransactionGuard tx(db);

                if (!match_id_.empty()) {
                    auto delete_save_status = db.Exec("DELETE FROM saved_matches WHERE id = ?", {match_id_});
                    if (!delete_save_status) {
                        Logger::Warn("[Match] Failed to clean up saved match: ", delete_save_status.error().message);
                    }
                }
                
                auto match_status = db.Exec("INSERT INTO matches (winner_username) VALUES (?)", {username});
                if (!match_status) {
                    throw std::runtime_error(match_status.error().message);
                }
                
                auto match_row = db.QueryOne("SELECT last_insert_rowid() as id", {});
                if (!match_row) {
                    throw std::runtime_error(match_row.error().message);
                }

                int match_id = match_row->value().Get<int>("id");

                for (const auto& p : state_.players) {
                    auto part_status = db.Exec("INSERT INTO match_participants (match_id, username) VALUES (?, ?)", {match_id, p.username});
                    if (!part_status) {
                        throw std::runtime_error(part_status.error().message);
                    }
                    
                    auto profile_status = db.Exec("INSERT OR IGNORE INTO player_stats (username) VALUES (?)", {p.username});
                    if (!profile_status) {
                        throw std::runtime_error(profile_status.error().message);
                    }

                    bool is_winner = (p.username == username);
                    auto& stats = session_stats_[p.username];

                    auto update_status = db.Exec(R"(
                        UPDATE player_stats SET 
                            total_wins = total_wins + ?,
                            total_losses = total_losses + ?,
                            
                            cards_played_red = cards_played_red + ?,
                            cards_played_blue = cards_played_blue + ?,
                            cards_played_green = cards_played_green + ?,
                            cards_played_yellow = cards_played_yellow + ?,
                            cards_played_wild = cards_played_wild + ?,
                            
                            cards_played_0 = cards_played_0 + ?,
                            cards_played_1 = cards_played_1 + ?,
                            cards_played_2 = cards_played_2 + ?,
                            cards_played_3 = cards_played_3 + ?,
                            cards_played_4 = cards_played_4 + ?,
                            cards_played_5 = cards_played_5 + ?,
                            cards_played_6 = cards_played_6 + ?,
                            cards_played_7 = cards_played_7 + ?,
                            cards_played_8 = cards_played_8 + ?,
                            cards_played_9 = cards_played_9 + ?,
                            cards_played_skip = cards_played_skip + ?,
                            cards_played_reverse = cards_played_reverse + ?,
                            cards_played_draw2 = cards_played_draw2 + ?,
                            cards_played_draw4 = cards_played_draw4 + ?,
                            cards_played_colorswitch = cards_played_colorswitch + ?
                        WHERE username = ?
                    )", {
                        is_winner ? 1 : 0, 
                        is_winner ? 0 : 1,
                        
                        stats.color_counts[0], stats.color_counts[1], stats.color_counts[2], stats.color_counts[3], stats.color_counts[4],
                        
                        stats.value_counts[0], stats.value_counts[1], stats.value_counts[2], stats.value_counts[3], stats.value_counts[4],
                        stats.value_counts[5], stats.value_counts[6], stats.value_counts[7], stats.value_counts[8], stats.value_counts[9],
                        stats.value_counts[10], stats.value_counts[11], stats.value_counts[12], stats.value_counts[14], stats.value_counts[13],
                        
                        p.username
                    });

                    if (!update_status) {
                        throw std::runtime_error(update_status.error().message);
                    }
                }

                tx.Commit();
                Logger::Info("[Match] Saved match stats to DB securely.");
        } catch (const std::exception& e) {
            Logger::Error("[Match DB Error] ", e.what());
        }

            return true;
        }

        return true;
    }
    
bool MatchInstance::DrawCard(const std::string& username) {
    if (IsWaitingForInput() || IsGameOver()) return false;

    Player* current_player = GetPlayer(username);
    if (!current_player) return false;

    if (GetCurrentPlayerUsername() != username) return false;

    // Draw stacking penalty resolution
    if (state_.pending_draws > 0) {
        for (int draw_index = 0; draw_index < state_.pending_draws; ++draw_index) {
            if (state_.draw_pile.empty()) {
                ReshuffleDiscardIntoDraw(&state_);
                if (state_.draw_pile.empty()) break; 
            }
            current_player->hand.push_back(state_.draw_pile.back());
            state_.draw_pile.pop_back();
        }
        state_.pending_draws = 0;
        current_player->has_called_uno = false;
        state_.effect_queue.push_back(std::make_unique<AdvanceTurnEffect>());
        return true;
    }

    CardDrawnEvent draw_event = { username, 0, false, false, false };
    for (auto& rule : active_rules_) {
        rule->OnCardDrawn(&state_, draw_event);
    }

    bool playable_found = false;

    do {
        if (state_.draw_pile.empty()) {
            ReshuffleDiscardIntoDraw(&state_);
            if (state_.draw_pile.empty()) break; 
        }

        CompactCard drawn_card = state_.draw_pile.back();
        current_player->hand.push_back(drawn_card);
        state_.draw_pile.pop_back();
        draw_event.drawn_card = drawn_card;
        
        current_player->has_called_uno = false; 

        CardPlayedEvent dummy_event = { username, drawn_card, true, false };
        for (auto& rule : active_rules_) {
            rule->ValidatePlay(&state_, dummy_event);
            if (dummy_event.is_handled) break;
        }

        playable_found = dummy_event.is_valid_play;

        if (playable_found || !draw_event.keep_drawing) {
            break;
        }
    } while (true);

    if (playable_found) {
        if (draw_event.force_play) {
            PlayCard(username, GetId(draw_event.drawn_card));
        } else {
            state_.effect_queue.push_front(std::make_unique<DecideDrawnCardEffect>(username, GetId(draw_event.drawn_card)));
        }
    } else {
        state_.effect_queue.push_back(std::make_unique<AdvanceTurnEffect>());
    }

    return true;
}
    
    void MatchInstance::ProvideInput(const std::string& username, const std::string& input) {
        if (username == state_.pending_player) {
            state_.provided_input = input;
            
            state_.pending_player.clear();
            state_.pending_input_context.clear();
        }
    }

    void MatchInstance::CallUno(const std::string& username) {
        Player* p = GetPlayer(username);
        if (p && p->hand.size() <= 2) {
            p->has_called_uno = true;
        }
    }
    
    void MatchInstance::TakeBotTurn() {
        if (IsGameOver()) return;

        auto greatestType = [this]() -> Type {
            int counts[4] = {0, 0, 0, 0};

            for (const auto& card : state_.players[state_.current_player_index].hand) {
                const Type t = GetType(card);
                if      (t == Type::kWild)   continue;
                if      (t == Type::kRed)    counts[0]++;
                else if (t == Type::kBlue)   counts[1]++;
                else if (t == Type::kGreen)  counts[2]++;
                else if (t == Type::kYellow) counts[3]++;
            }

            int max_idx = 0;
            for (int i = 1; i < 4; i++) {
                if (counts[i] > counts[max_idx]) max_idx = i;
            }

            switch (max_idx) {
                case 0: return Type::kRed;
                case 1: return Type::kBlue;
                case 2: return Type::kGreen;
                default: return Type::kYellow;
            }
        };

        string bot_username = GetCurrentPlayerUsername();

        int next_idx = (state_.current_player_index + state_.play_direction + state_.players.size()) % state_.players.size();
        int next_player_hand_size = state_.players[next_idx].hand.size();
        std::string winning_player = "";

        size_t lowest_card_count = 999;
        for (const auto& p : state_.players) {
            if (p.username != bot_username) {
                if (p.hand.size() < lowest_card_count) {
                    lowest_card_count = p.hand.size();
                    winning_player = p.username;
                }
            }
        }

        // INFO: Action 1: resolve any pending input (one response per call)
        if (IsWaitingForInput()) {
            if (state_.pending_action == Action::kPlayDrawn)
                ProvideInput(state_.pending_player, "0");
            else if (state_.pending_action == Action::kChooseType)
                ProvideInput(state_.pending_player, std::to_string(static_cast<int>(greatestType())));
            else if (state_.pending_action == Action::kChooseTarget)
                ProvideInput(state_.pending_player, winning_player);
            else
                Logger::Error("[MATCH] Bot encountered unknown action: ", static_cast<int>(state_.pending_action));

            Tick();
            return;
        }

        // INFO: Action 2: choose and play the best card (or draw)
        Player* current_player = GetPlayer(bot_username);

        Type dominant_type = greatestType();

        bool found_playable = false;
        int best_score = -9999;
        CompactCard best_card;

        for (CompactCard card : current_player->hand) {
            CardPlayedEvent event = { current_player->username, card, true, false };

            for (auto& rule : active_rules_) {
                rule->ValidatePlay(&state_, event);
                if (event.is_handled) break;
            }

            if (event.is_valid_play) {
                found_playable = true;
                int current_score = 10;

                Type card_type = GetType(card);
                Value card_value = GetValue(card);

                if (current_player->hand.size() == 1) {
                    current_score += 1000;
                }

                if (card_type != Type::kWild) {
                    if (card_type == dominant_type) {
                        current_score += 20;
                    }
                    if (state_.active_type != dominant_type && card_type == dominant_type) {
                        current_score += 40;
                    } else if (state_.active_type == dominant_type && card_type != dominant_type) {
                        current_score -= 30;
                    }
                } else {
                    current_score -= 15;
                }

                if (card_value == Value::kDraw2 || card_value == Value::kWildDraw4) {
                    if (next_player_hand_size == 1) {
                        current_score += 200;
                    } else if (next_player_hand_size <= 3) {
                        current_score += 50;
                    } else if (next_player_hand_size >= 7) {
                        current_score -= 20;
                    }
                }

                if (card_value == Value::kSkip || card_value == Value::kReverse) {
                    if (next_player_hand_size <= 2) {
                        current_score += 30;
                    }
                }

                if (current_score > best_score) {
                    best_score = current_score;
                    best_card = card;
                }
            }
        }

        if (found_playable) {
            CallUno(bot_username);
            PlayCard(current_player->username, GetId(best_card));
        } else {
            DrawCard(current_player->username);
        }
        Tick();
    }
    
    std::string MatchInstance::GetCurrentPlayerUsername() const {
        if (state_.players.empty()) return "";
        return state_.players[state_.current_player_index].username;
    }
    
    Player* MatchInstance::GetPlayer(const std::string& username) {
        auto it = std::ranges::find(state_.players, username, &Player::username);
        return it != state_.players.end() ? &(*it) : nullptr;
    }
    
    void MatchInstance::GenerateDeck() {
        uint16_t unique_card_identifier = 0;
        Type standard_types[] = {Type::kRed, Type::kBlue, Type::kGreen, Type::kYellow};

        for (Type current_color : standard_types) {
            // Generate Zeros
            for (int i = 0; i < settings_.count_zeros; ++i) {
                state_.draw_pile.push_back(MakeCard(current_color, Value::k0, unique_card_identifier++));
            }
                
            // Generate Numbers 1-9
            for (int card_value = 1; card_value <= 9; ++card_value) {
                for (int i = 0; i < settings_.count_numbered; ++i) {
                    state_.draw_pile.push_back(MakeCard(current_color, static_cast<Value>(card_value), unique_card_identifier++));
                }
            }
            
            // Generate Skips
            for (int i = 0; i < settings_.count_skips; ++i) {
                state_.draw_pile.push_back(MakeCard(current_color, Value::kSkip, unique_card_identifier++));
            }
            
            // Generate Reverses (Blocks)
            for (int i = 0; i < settings_.count_reverses; ++i) {
                state_.draw_pile.push_back(MakeCard(current_color, Value::kReverse, unique_card_identifier++));
            }
            
            // Generate +2s
            for (int i = 0; i < settings_.count_draw_two; ++i) {
                state_.draw_pile.push_back(MakeCard(current_color, Value::kDraw2, unique_card_identifier++));
            }
        }

        // 2. Generate Wild Cards (Color-less)
        for (int i = 0; i < settings_.count_wild; ++i) {
            state_.draw_pile.push_back(MakeCard(Type::kWild, Value::kWild, unique_card_identifier++));
        }

        for (int i = 0; i < settings_.count_wild_draw_four; ++i) {
            state_.draw_pile.push_back(MakeCard(Type::kWild, Value::kWildDraw4, unique_card_identifier++));
        }
    
        std::random_device random_device;
        std::mt19937 random_generator(random_device());
        std::shuffle(state_.draw_pile.begin(), state_.draw_pile.end(), random_generator);
    }
    
    nlohmann::json MatchInstance::SerializePlayerState(const std::string& username) const {
        nlohmann::json root;
        auto now = std::chrono::steady_clock::now();
        int remaining_ms = 0;

        if (state_.turn_end_time > now) {
            remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(state_.turn_end_time - now).count();
        }

        root["turn_time_remaining_ms"] = remaining_ms;

        if (IsGameOver()) root["winner"] = state_.winner;
    
        root["match_status"] = static_cast<int>(state_.status);

        root["active_type"] = static_cast<int>(state_.active_type);
        root["current_turn"] = GetCurrentPlayerUsername();
        root["play_direction"] = state_.play_direction;
        root["discard_pile_size"] = state_.discard_pile.size();
        root["pending_draws"] = state_.pending_draws;

        if (state_.last_play.valid) {
            CompactCard lp = state_.last_play.card;
            Type lp_display = GetType(lp);
            if (lp_display == Type::kWild) lp_display = state_.active_type;
            root["last_play"] = {
                {"player", state_.last_play.player},
                {"hand_index", state_.last_play.hand_index},
                {"card", {
                    {"id", GetId(lp)},
                    {"type", static_cast<int>(lp_display)},
                    {"value", static_cast<int>(GetValue(lp))}
                }}
            };
        }

        if (!state_.discard_pile.empty()) {
            CompactCard top = state_.discard_pile.back();
            // INFO: Wild cards inherit active_type so the card visually shows
            //       the chosen type.
            Type display_type = GetType(top);
            if (display_type == Type::kWild) display_type = state_.active_type;
            root["top_card"] = {
                {"id", GetId(top)},
                {"type", static_cast<int>(display_type)},
                {"value", static_cast<int>(GetValue(top))},
            };
        }
    
        nlohmann::json players_array = nlohmann::json::array();
        for (const auto& p : state_.players) {
            nlohmann::json p_json;
            p_json["username"] = p.username;
            p_json["card_count"] = p.hand.size();
            p_json["has_called_uno"] = p.has_called_uno;
            p_json["is_bot"] = p.is_bot;
    
            if (p.username == username) {
                nlohmann::json hand_json = nlohmann::json::array();
                for (CompactCard c : p.hand) {
                    CardPlayedEvent play_check = { username, c, true, false };
                    for (auto& rule : active_rules_) {
                        rule->ValidatePlay(&state_, play_check);
                        if (play_check.is_handled) break;
                    }
                    hand_json.push_back({
                        {"id", GetId(c)},
                        {"type", static_cast<int>(GetType(c))},
                        {"value", static_cast<int>(GetValue(c))},
                        {"can_play", play_check.is_valid_play}
                    });
                }
                p_json["hand"] = hand_json;
            }
            players_array.push_back(p_json);
        }
        root["players"] = players_array;
    
        return root;
    }  

}
