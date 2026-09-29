#pragma once

#include <match/ecs/components.hpp>
#include <match/modload/restriction.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @file play_evaluator.hpp
 * @brief Single play-legality authority.
 *
 * `PlayEvaluator` answers "can seat S play card C now" from one place. It is
 * built once per snapshot / decision from a `MatchInstance`, converts the
 * match's `ecs::RestrictionEntry` pipeline once, binds the assembly's
 * `play_matcher` and `card_facts`, captures the discard top kind, the active
 * type and the current player, and builds each player's `context["hand"]`
 * (kind ids) once.
 *
 * `MatchInstance::CheckPlayRestrictions`, `CanRespondWindow` and
 * `ResponseEligible` are thin wrappers over it; so is the window decision in
 * `RespondWindow`. Behaviour is unchanged — this only unifies the authority.
 */

namespace match::engine {

class MatchInstance;

/**
 * @class PlayEvaluator
 * @brief One snapshot's play-legality decisions.
 */
class PlayEvaluator {
public:
    /**
     * @struct WindowView
     * @brief The live window state `CanRespond` reasons over.
     */
    struct WindowView {
        const std::vector<ecs::Entity>& responders;
        const std::vector<ecs::WindowResponse>& responses;
        const nlohmann::json& respond_with;
    };

    /**
     * @brief Capture the match's restriction pipeline and decision context.
     * @param match Live match; must outlive the evaluator.
     */
    explicit PlayEvaluator(const MatchInstance& match);

    /**
     * @brief True when `player` may play `card` as the current player.
     *
     * Side-effect free. False when `player` is not the current player (the
     * legacy view `CanPlay` behaviour): an out-of-turn play is only legal
     * through an open window, which `CanRespond` covers.
     */
    bool CanPlayInTurn(ecs::Entity player, ecs::Entity card) const;

    /**
     * @brief True when `player` may answer `window` with `card`.
     *
     * Side-effect free. Checks the responder set, a prior reply and hand
     * ownership, then the restriction pipeline and the window's
     * `respond_with` eligibility filter.
     */
    bool CanRespond(const WindowView& window, ecs::Entity player,
                    ecs::Entity card) const;

    /** @brief Run the restriction pipeline for a prebuilt attempt. */
    modload::PlayDecision Check(const modload::PlayAttempt& attempt) const;

    /** @brief True when `card` satisfies a window's `respond_with`. */
    bool IsEligible(const nlohmann::json& respond_with,
                    const modload::PlayAttempt& attempt) const;

private:
    /**
     * @brief Memo key for one restriction verdict.
     *
     * The verdict keys on `(card_kind, in_turn, responding)`; the acting player
     * is included because `context["hand"]` is per-player, so `owns_card` /
     * `plays_bluffing` verdicts can differ between seats with the same kind.
     */
    struct VerdictKey {
        uint32_t player_index = 0;
        uint32_t player_generation = 0;
        std::string card_kind;
        bool in_turn = false;
        bool responding = false;

        bool operator<(const VerdictKey& other) const;
    };

    /** @brief Build the attempt for `player`/`card` (context cached). */
    modload::PlayAttempt BuildAttempt(ecs::Entity player, ecs::Entity card,
                                      bool in_turn, bool responding) const;

    /** @brief `context["hand"]` for `player`, built once and cached. */
    const nlohmann::json& HandContext(ecs::Entity player) const;

    /** @brief Memoized restriction verdict for `player`/`card`. */
    modload::PlayDecision Restrictions(ecs::Entity player, ecs::Entity card,
                                       bool in_turn, bool responding) const;

    const MatchInstance& match_;
    std::vector<modload::RestrictionEntry> entries_;
    std::string top_kind_;
    std::string active_type_;
    std::optional<ecs::Entity> current_player_;
    mutable std::map<uint64_t, nlohmann::json> hands_;
    mutable std::map<VerdictKey, modload::PlayDecision> verdicts_;
};

}  // namespace match::engine
