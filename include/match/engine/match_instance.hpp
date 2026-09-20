#pragma once

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/engine/match_assembler.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file match_instance.hpp
 * @brief Engine `MatchInstance`: input flow, turn advance, win.
 *
 * It owns the `MatchAssembly` (store, bus, resolver, registries, RNG) and
 * exposes the public surface for controllers and headless tests:
 * `PlayCard`, `DrawCard`, `SubmitInput`, `Tick`, plus read-only state access.
 *
 * The engine scope (this slice):
 *
 * - `PlayCard` runs the restriction pipeline (the validator
 *   `EvaluatePlayRestrictions` + the `PlayConditionMatcher` bound to the
 *   engine's true state). A deny emits `play_rejected {player, reason_id}` and
 *   returns false. An allow performs the engine default play: move the card
 *   from the acting player's hand to the discard, emit `card_left_zone` /
 *   `card_entered_zone` / `card_played`, set `active_type_req` and the engine's
 *   last-play record, then advance the turn per `MatchMeta.direction` and the
 *   The op layer extra-turn / one-shot-skip bookkeeping.
 * - `DrawCard` draws one card, auto-reshuffling the discard into the draw pile
 *   when it empties (emitting `reshuffle`), then advances the turn.
 * - Win/placement: an empty hand after a play pushes the player onto
 *   `Placements`, emits `placement {player, place}` and marks the match over.
 * - `Tick` advances the round counter once a full seat cycle has elapsed
 *   (emitting `round_advance`); timers, windows and scheduled graphs are the
 *   documented the engine seam.
 *
 * Explicitly NOT in this slice (kept as documented stubs / seams):
 *
 * - EventBus hook dispatch (`before:play`, `after:play`, `draw_attempt`, ...)
 *   and Resolver graph draining belong.
 * - Response windows, prompts, draw stacking and `SubmitInput` belong to
 *   The engine; `PendingInput` is always empty here.
 * - The content gap vocabulary additions (play-card op, drawn-card
 *   playability, player-count condition) belong.
 */

namespace match::engine {

/**
 * @class MatchInstance
 * @brief Headless match driver over a `MatchAssembly`.
 */
class MatchInstance {
public:
    /**
     * @brief Take ownership of an assembled match and start it.
     *
     * The assembly is heap-pinned and non-movable (the Resolver holds
     * references into it), so it must be passed as the `unique_ptr` it was
     * created behind. Construction finalises the match: the assembler has
     * already dealt hands, opened the discard and installed the restriction
     * entries, so `Start` only asserts the initial turn owner and is
     * idempotent.
     *
     * @param assembly A successful `MatchAssembler::Assemble` result.
     */
    explicit MatchInstance(std::unique_ptr<MatchAssembly> assembly);

    MatchInstance(const MatchInstance&) = delete;
    MatchInstance& operator=(const MatchInstance&) = delete;
    MatchInstance(MatchInstance&&) = delete;
    MatchInstance& operator=(MatchInstance&&) = delete;
    ~MatchInstance() = default;

    /**
     * @brief Ensure the match is live and a current player is set.
     *
     * Idempotent. Called by the constructor; exposed so a future slice can
     * move the `turn_start` / `round_start` hook dispatch here.
     */
    void Start();

    /**
     * @brief Attempt to play `card` from `username`'s hand.
     *
     * Runs the restriction pipeline against true engine state; a deny records
     * `play_rejected` and returns false. On allow, performs the engine default
     * play and advances the turn (unless the play emptied the hand and ended
     * the match).
     *
     * @param username Acting player's username.
     * @param card     Card entity; the view layer resolves a wire
     *                 `CompactCardV2` to an entity through
     *                 `MatchRegistries::CardEntity`.
     * @return true when the play was accepted (even if it ended the match);
     *         false on a deny, an unknown player, a dead/non-card entity or a
     *         finished match.
     */
    bool PlayCard(const std::string& username, ecs::Entity card);

    /**
     * @brief Draw one card for the current player.
     *
     * Auto-reshuffles the discard into the draw pile when it is empty,
     * emitting `reshuffle`, then emits `cards_drawn` and advances the turn.
     * The drawn-card play decision is a seam: this slice always
     * passes the turn after the draw.
     *
     * @return false on an unknown player, a non-current player, an exhausted
     *         draw pile with nothing to reshuffle, or a finished match.
     */
    bool DrawCard(const std::string& username);

    /**
     * @brief Stub for the window/prompt input flow.
     *
     * No window or prompt can be open in this slice, so this always returns
     * false. The engine binds `value` to the pending window response.
     */
    bool SubmitInput(const std::string& username,
                     const nlohmann::json& value);

    /**
     * @brief Advance engine-side counters (round boundary).
     *
     * The caller drives the clock; this slice only turns elapsed seat cycles
     * into `MatchMeta.round` plus a `round_advance` event. Turn timers,
     * window timers, real-time statuses and scheduled graphs are the engine
     * seam.
     */
    void Tick();

    // --- read-only state (headless tests / snapshots) ----------------------

    /** @brief True once a player has won. */
    bool IsMatchOver() const;

    /** @brief Winner username, or empty when the match is unfinished. */
    std::string GetWinner() const;

    /** @brief Finish order as usernames, best place first. */
    std::vector<std::string> GetPlacements() const;

    /** @brief Current-turn player entity, or nullopt. */
    std::optional<ecs::Entity> GetCurrentPlayer() const;

    /** @brief Current-turn player username, or empty. */
    std::string GetCurrentPlayerUsername() const;

    /** @brief Player entity for `username`, or nullopt. */
    std::optional<ecs::Entity> FindPlayer(const std::string& username) const;

    /**
     * @brief Deterministic state JSON for headless assertions / snapshots.
     *
     * Mirrors the legacy `ExportState` essentials: status, current player,
     * direction, round, active type, winner/placements, pile sizes, top card,
     * last play and per-player card counts. Not the wire snapshot.
     */
    nlohmann::json ExportState() const;

    /**
     * @brief Pending input envelope, or nullopt.
     */
    std::optional<nlohmann::json> PendingInput() const;

    // --- event stream ------------------------------------------------------

    /**
     * @brief Every event descriptor emitted so far (`{type, payload}`).
     *
     * This is the event source; the view layer owns the wire `seq` and
     * per-recipient filtering.
     */
    const std::vector<nlohmann::json>& Events() const { return events_; }

    /** @brief Move the accumulated events out and clear the log. */
    std::vector<nlohmann::json> TakeEvents();

    // --- owned assembly ----------------------------------------------------

    /** @brief The owned assembly (store, bus, resolver, registries). */
    MatchAssembly& Assembly() { return *assembly_; }
    /** @brief The owned assembly (const). */
    const MatchAssembly& Assembly() const { return *assembly_; }
    /** @brief The entity store. */
    ecs::EntityStore& Store() { return assembly_->store; }
    /** @brief The entity store (const). */
    const ecs::EntityStore& Store() const { return assembly_->store; }
    /** @brief Frozen per-match registries. */
    const MatchRegistries& Registries() const { return assembly_->registries; }

private:
    /** @brief One accepted play's provenance, for the state JSON. */
    struct LastPlay {
        ecs::Entity player{};
        ecs::Entity card{};
        uint32_t hand_ordinal = 0;
    };

    /** @brief Append an event descriptor to the log. */
    void Emit(std::string_view type, nlohmann::json payload);

    /** @brief The live current-turn player, or nullopt. */
    std::optional<ecs::Entity> CurrentPlayer() const;

    /**
     * @brief Advance the turn through the `advance_turn` op body.
     *
     * Reusing the op keeps the direction / extra-turn / one-shot-skip
     * bookkeeping in exactly one place. Appends the op's `turn_advance`
     * event and counts a seat change toward the round boundary.
     */
    void AdvanceTurn();

    /**
     * @brief Move the discard (except its top) back onto the draw pile and
     *        shuffle it with the match RNG (legacy `ReshuffleDiscardIntoDraw`).
     *
     * @return false when there is nothing to reshuffle.
     */
    bool ReshuffleDiscardIntoDraw();

    /** @brief Shuffle the draw pile and renumber its `in_zone` ordinals. */
    void ShuffleDrawPile();

    /** @brief Top-of-discard card entity, or nullopt. */
    std::optional<ecs::Entity> TopDiscard() const;

    /** @brief Apply the engine default win/placement for `player`. */
    void DeclareHandEmptyWin(ecs::Entity player);

    std::unique_ptr<MatchAssembly> assembly_;
    std::vector<nlohmann::json> events_;
    std::optional<LastPlay> last_play_;
    std::optional<ecs::Entity> winner_;
    uint64_t turns_elapsed_ = 0;  /**< distinct-seat advances so far. */
    bool started_ = false;
    bool finished_ = false;
};

}  // namespace match::engine
