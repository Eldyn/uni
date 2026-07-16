#include "common/match/effect.hpp"
#include "match/match_state.hpp"
#include "match/match_instance.hpp"
#include <match/effects/standard.hpp>
#include <match/effect_registry.hpp>
#include <algorithm>

namespace match {

    EffectResult AdvanceTurnEffect::Resolve(MatchState* state, MatchInstance* match) {
        state->current_player_index += state->play_direction;
        int total_players = static_cast<int>(state->players.size());

        if (state->current_player_index >= total_players) {
            state->current_player_index %= total_players;
        } else while (state->current_player_index < 0) {
            state->current_player_index += total_players;
        }
        return {EffectStatus::kResolved};
    }

    EffectResult DrawEffect::Resolve(MatchState* state, MatchInstance* match) {
        auto player_iterator = std::ranges::find(state->players, target_username_,
                                                  &Player::username);
        if (player_iterator != state->players.end()) {
            for (int draw_index = 0; draw_index < count_; ++draw_index) {
                if (state->draw_pile.empty()) {
                    ReshuffleDiscardIntoDraw(state, match->Rng());
                    if (state->draw_pile.empty()) break;
                }
                player_iterator->hand.push_back(state->draw_pile.back());
                state->draw_pile.pop_back();
            }
        }
        return {EffectStatus::kResolved};
    }

    EffectResult SkipEffect::Resolve(MatchState* state, MatchInstance* match) {
        state->effect_queue.push_front(std::make_unique<AdvanceTurnEffect>());
        return {EffectStatus::kResolved};
    }

    EffectResult ReverseEffect::Resolve(MatchState* state, MatchInstance* match) {
        state->play_direction *= -1;
        if (state->players.size() == 2) {
            state->effect_queue.push_front(std::make_unique<AdvanceTurnEffect>());
        }
        return {EffectStatus::kResolved};
    }

    EffectResult ChooseColorEffect::Resolve(MatchState* state, MatchInstance* match) {
        if (!state->provided_input.empty()) {
            state->active_type = static_cast<Type>(std::stoi(state->provided_input));
            state->provided_input.clear();
            state->pending_draws += stack_bonus_;
            return {EffectStatus::kResolved};
        }
        return {EffectStatus::kNeedsInput, Action::kChooseType, target_username_};
    }

    EffectResult DecideDrawnCardEffect::Resolve(MatchState* state, MatchInstance* match) {
        if (!state->provided_input.empty()) {
            std::string choice = state->provided_input;
            state->provided_input.clear();

            if (std::stoi(choice) == 0) {
                match->PlayCard(username_, card_id_);
            } else {
                state->effect_queue.push_back(std::make_unique<AdvanceTurnEffect>());
            }
            return {EffectStatus::kResolved};
        }

        nlohmann::json action_context = {
            {"card_id", card_id_},
            {"message", "You drew a playable card!"}
        };

        return {EffectStatus::kNeedsInput, Action::kPlayDrawn, username_, action_context};
    }

    static EffectRegistrar reg_advance(EffectType::kAdvanceTurn, [](const auto&) {
        return std::make_unique<AdvanceTurnEffect>();
    });
    static EffectRegistrar reg_skip(EffectType::kSkip, [](const auto&) {
        return std::make_unique<SkipEffect>();
    });
    static EffectRegistrar reg_reverse(EffectType::kReverse, [](const auto&) {
        return std::make_unique<ReverseEffect>();
    });
    static EffectRegistrar reg_draw(EffectType::kDraw, [](const auto& e) {
        return std::make_unique<DrawEffect>(e.value("count", 0), e.value("target", ""));
    });
    static EffectRegistrar reg_color(EffectType::kChooseColor, [](const auto& e) {
        return std::make_unique<ChooseColorEffect>(e.value("target", ""),
                                                     e.value("stack_bonus", 0));
    });
    static EffectRegistrar reg_drawn(EffectType::kDecideDrawnCard, [](const auto& e) {
        return std::make_unique<DecideDrawnCardEffect>(
            e.value("username", ""), e.value("card_id", static_cast<uint16_t>(0)));
    });

}
