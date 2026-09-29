#include "match/engine/play_evaluator.hpp"

#include "match/engine/match_instance.hpp"
#include "match/modload/play_conditions.hpp"

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <utility>

/**
 * @file play_evaluator.cpp
 * @brief `PlayEvaluator` implementation.
 */

namespace match::engine {
namespace {

using nlohmann::json;

/** @brief Username of a player entity, or empty when not a live player. */
std::string PlayerUsername(const ecs::EntityStore& store, ecs::Entity player) {
    const ecs::PlayerInfo* info = store.Get<ecs::PlayerInfo>(player);
    return info == nullptr ? std::string() : info->username;
}

/** @brief Packed entity identity for the per-player hand cache. */
uint64_t EntityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) | entity.generation;
}

}  // namespace

bool PlayEvaluator::VerdictKey::operator<(const VerdictKey& other) const {
    return std::tie(player_index, player_generation, card_kind, in_turn,
                    responding)
        < std::tie(other.player_index, other.player_generation,
                   other.card_kind, other.in_turn, other.responding);
}

PlayEvaluator::PlayEvaluator(const MatchInstance& match) : match_(match) {
    const ecs::EntityStore& store = match_.Store();
    const MatchRegistries& registries = match_.Registries();

    // INFO: convert the pipeline once; the matcher and card facts are bound
    //       per call from the assembly, matching the legacy wrapper exactly.
    if (const ecs::PlayRestriction* pipeline =
            store.Get<ecs::PlayRestriction>(registries.match)) {
        entries_.reserve(pipeline->entries.size());
        for (const ecs::RestrictionEntry& entry : pipeline->entries) {
            modload::RestrictionEntry converted;
            converted.id = entry.id;
            converted.phase = entry.phase == ecs::RestrictionPhase::kAllow
                                  ? "allow"
                                  : "deny";
            converted.condition = entry.condition;
            entries_.push_back(std::move(converted));
        }
    }

    if (const ecs::PileContents* discard =
            store.Get<ecs::PileContents>(registries.discard_pile)) {
        if (!discard->cards.empty()) {
            const ecs::CardIdentity* top =
                store.Get<ecs::CardIdentity>(discard->cards.back());
            if (top != nullptr) top_kind_ = top->kind_id;
        }
    }

    if (const ecs::ActiveTypeReq* active =
            store.Get<ecs::ActiveTypeReq>(registries.match)) {
        active_type_ =
            active->type.has_value() ? *active->type : std::string();
    }

    current_player_ = match_.GetCurrentPlayer();
}

const nlohmann::json& PlayEvaluator::HandContext(ecs::Entity player) const {
    const uint64_t key = EntityKey(player);
    const auto cached = hands_.find(key);
    if (cached != hands_.end()) return cached->second;

    json hand_kinds = json::array();
    const ecs::EntityStore& store = match_.Store();
    if (const ecs::Hand* hand = store.Get<ecs::Hand>(player)) {
        for (ecs::Entity held : hand->cards) {
            const ecs::CardIdentity* held_id =
                store.Get<ecs::CardIdentity>(held);
            if (held_id != nullptr) hand_kinds.push_back(held_id->kind_id);
        }
    }
    return hands_.emplace(key, std::move(hand_kinds)).first->second;
}

modload::PlayAttempt PlayEvaluator::BuildAttempt(ecs::Entity player,
                                                 ecs::Entity card,
                                                 bool in_turn,
                                                 bool responding) const {
    const ecs::EntityStore& store = match_.Store();
    modload::PlayAttempt attempt;
    attempt.player = PlayerUsername(store, player);
    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    attempt.card_kind =
        identity == nullptr ? std::string() : identity->kind_id;
    attempt.in_turn = in_turn;
    attempt.responding = responding;

    json context = json::object();
    if (!top_kind_.empty()) context["top_kind"] = top_kind_;
    context["active_type"] = active_type_;
    context["hand"] = HandContext(player);
    attempt.context = std::move(context);
    return attempt;
}

modload::PlayDecision PlayEvaluator::Check(
    const modload::PlayAttempt& attempt) const {
    const modload::PlayConditionMatcher* matcher =
        match_.Assembly().play_matcher.get();
    modload::ConditionMatcher condition =
        [matcher](const json& value, const modload::PlayAttempt& a) {
            return matcher != nullptr && matcher->Matches(value, a);
        };
    return modload::EvaluatePlayRestrictions(entries_, attempt, condition);
}

bool PlayEvaluator::IsEligible(const nlohmann::json& respond_with,
                               const modload::PlayAttempt& attempt) const {
    if (respond_with.is_null()) return true;
    if (!respond_with.is_object() || respond_with.empty()) return true;

    const modload::PlayConditionMatcher* matcher =
        match_.Assembly().play_matcher.get();

    // INFO: the declared `condition` form is jump_in/no_bluffing eligibility;
    //       a bare single-keyword play condition is accepted too. A
    //       `condition` next to a tag filter must hold as well as the tag.
    const auto condition = respond_with.find("condition");
    if (condition != respond_with.end()) {
        if (matcher == nullptr || !matcher->Matches(*condition, attempt)) {
            return false;
        }
        if (respond_with.size() == 1) return true;
    }
    if (respond_with.size() == 1
        && modload::IsPlayConditionKeyword(respond_with.begin().key())) {
        return matcher != nullptr && matcher->Matches(respond_with, attempt);
    }

    const auto& card_facts = match_.Assembly().card_facts;
    const auto facts_it = card_facts.find(attempt.card_kind);
    if (facts_it == card_facts.end()) return false;
    const modload::PlayCardFacts& facts = facts_it->second;

    const auto any_tag = respond_with.find("any_tag");
    if (any_tag != respond_with.end()) {
        if (any_tag->is_string()) {
            const std::string tag = any_tag->get<std::string>();
            return std::find(facts.tags.begin(), facts.tags.end(), tag)
                != facts.tags.end();
        }
        if (any_tag->is_array()) {
            for (const json& tag : *any_tag) {
                if (tag.is_string()
                    && std::find(facts.tags.begin(), facts.tags.end(),
                                 tag.get<std::string>())
                           != facts.tags.end()) {
                    return true;
                }
            }
        }
        return false;
    }
    const auto tag = respond_with.find("tag");
    if (tag != respond_with.end() && tag->is_string()) {
        const std::string wanted = tag->get<std::string>();
        return std::find(facts.tags.begin(), facts.tags.end(), wanted)
            != facts.tags.end();
    }
    const auto kind = respond_with.find("kind");
    if (kind != respond_with.end() && kind->is_string()) {
        return attempt.card_kind == kind->get<std::string>();
    }
    // WARN: an unrecognized filter admits nothing rather than everything.
    return false;
}

modload::PlayDecision PlayEvaluator::Restrictions(ecs::Entity player,
                                                  ecs::Entity card,
                                                  bool in_turn,
                                                  bool responding) const {
    const ecs::CardIdentity* identity =
        match_.Store().Get<ecs::CardIdentity>(card);
    const std::string card_kind =
        identity == nullptr ? std::string() : identity->kind_id;
    const VerdictKey key{player.index, player.generation, card_kind, in_turn,
                         responding};
    const auto cached = verdicts_.find(key);
    if (cached != verdicts_.end()) return cached->second;

    const modload::PlayAttempt attempt =
        BuildAttempt(player, card, in_turn, responding);
    const modload::PlayDecision decision = Check(attempt);
    verdicts_.emplace(key, decision);
    return decision;
}

bool PlayEvaluator::CanPlayInTurn(ecs::Entity player, ecs::Entity card) const {
    const bool in_turn =
        current_player_.has_value() && *current_player_ == player;
    if (!in_turn) return false;
    return Restrictions(player, card, /*in_turn=*/true, /*responding=*/false)
        .allowed;
}

bool PlayEvaluator::CanRespond(const WindowView& window, ecs::Entity player,
                               ecs::Entity card) const {
    const ecs::EntityStore& store = match_.Store();
    if (std::find(window.responders.begin(), window.responders.end(), player)
        == window.responders.end()) {
        return false;
    }
    for (const ecs::WindowResponse& existing : window.responses) {
        if (existing.responder == player) return false;
    }
    const ecs::Hand* hand = store.Get<ecs::Hand>(player);
    if (hand == nullptr
        || std::find(hand->cards.begin(), hand->cards.end(), card)
               == hand->cards.end()) {
        return false;
    }

    const bool in_turn =
        current_player_.has_value() && *current_player_ == player;
    if (!Restrictions(player, card, in_turn, /*responding=*/true).allowed) {
        return false;
    }
    const modload::PlayAttempt attempt =
        BuildAttempt(player, card, in_turn, /*responding=*/true);
    for (const WindowView::Member& member : window.members) {
        if (std::find(member.responders.begin(), member.responders.end(),
                      player)
            == member.responders.end()) {
            continue;
        }
        if (IsEligible(member.respond_with, attempt)) return true;
    }
    return false;
}

}  // namespace match::engine
