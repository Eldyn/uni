#pragma once

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/ecs/hooks.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/ops/ops.hpp>
#include <match/resolver.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file match_instance.hpp
 * @brief Engine `MatchInstance`: input flow, hooks, resolution, win.
 *
 * It owns the `MatchAssembly` (store, bus, resolver, registries, RNG) and
 * exposes the public surface for controllers and headless tests:
 * `PlayCard`, `DrawCard`, `SubmitInput`, `Tick`, plus read-only state access.
 *
 * Play flow: restriction-pipeline play, default draw, turn advance, win.
 *
 * Hooks and resolution:
 *
 * - Dispatches every engine-flow hook through the `EventBus` at the
 *   correct phase and payload keys (`turn_start`/`turn_end`, `round_start`/
 *   `round_end`, `play`, `draw_attempt`/`draw`, `shuffle`/`pile_empty`,
 *   `hand_empty`, `win_check`, `card_entered_zone`/`card_left_zone`,
 *   `match_end`, plus the op-bridged `status_*`, `roll`, `visibility_*` and
 *   `effect_applied` observation hooks). Before-veto on a veto-capable hook
 *   cancels the engine default (play, draw skip/undo, `turn_end` extra turn,
 *   `pile_empty` reshuffle block, `hand_empty`/`win_check` win block or
 *   rewrite). `match_start` stays assembly-owned and is not re-dispatched.
 * - Drains the played card's behavior graphs through the frozen
 *   `assembly.systems` order via the bus / `Resolver::Resolve`; op-emitted
 *   `ResolveResult` events are appended to the ordered per-match event list.
 *   A `kNeedsInput` pause is held and resumed through `SubmitInput` ->
 *   `Resolver::ResumeInput`; a `kWindow`/`kSchedule` pause is handed to the
 *   The engine seam (this slice detects and exposes it, it does not run the
 *   window).
 * - Runs must-apply auto cards (`AutoTrigger.must_apply`) before a
 *   window would open, one per triggering event, emitting `auto_played`; the
 *   Resolver's `must_apply` depth cap bounds re-triggering.
 *
 * Explicitly NOT in this slice: response windows, prompts UX, draw stacking and
 * the full `SubmitInput` window flow; the gap vocabulary additions
 * WS/view building.
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
     * entries, so `Start` only asserts the initial turn owner, dispatches the
     * round/turn start hooks and is idempotent.
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
     * @brief Ensure the match is live, a current player is set and start
     *        hooks fired.
     *
     * Idempotent. Dispatches `round_start {round:0}` and `turn_start` for the
     * initial player. Called by the constructor.
     */
    void Start();

    /**
     * @brief Attempt to play `card` from `username`'s hand.
     *
     * Runs the restriction pipeline against true engine state; a deny records
     * `play_rejected` and returns false. On allow, runs the flow:
     * `before:play` (a veto cancels the default) -> zone hooks + engine move +
     * `after:play` (the card's behavior graphs drain through the Resolver) ->
     *  must-apply auto cards -> `hand_empty` / `win_check` settle or
     * turn advance. A `kNeedsInput` / `kWindow` pause stops the flow and is
     * resumed later.
     *
     * @param username Acting player's username.
     * @param card     Card entity; the view layer resolves a wire
     *                 `CompactCardV2` to an entity through
     *                 `MatchRegistries::CardEntity`.
     * @return true when the play was accepted (even if it ended the match or
     *         paused); false on a deny, an unknown player, a dead/non-card
     *         entity, a finished match or when input is already pending.
     */
    bool PlayCard(const std::string& username, ecs::Entity card);

    /**
     * @brief Draw one card for the current player.
     *
     * Fires `draw_attempt` (veto = skip this draw), auto-reshuffles the discard
     * into the draw pile when it is empty (`pile_empty` veto blocks the
     * reshuffle, `shuffle` observes it), fires `draw` (veto = undo), then emits
     * `cards_drawn` and advances the turn. The drawn-card play decision is a
     * The engine and the engine seam: this slice always passes the turn after
     * the draw.
     *
     * @return false on an unknown player, a non-current player, an exhausted
     *         draw pile with nothing to reshuffle, or a finished match.
     */
    bool DrawCard(const std::string& username);

    /**
     * @brief Bind a paused op input and resume the Resolver.
     *
     * Only valid while `PendingInput()` is set and the value is addressed to
     * the paused op's target. Calls `Resolver::ResumeInput`, appends the
     * resumed events and either settles the interrupted play or continues to
     * the next pause. Window responses are the engine's and are refused here.
     */
    bool SubmitInput(const std::string& username,
                     const nlohmann::json& value);

    /**
     * @brief Advance engine-side counters (round boundary).
     *
     * Turns elapsed seat cycles into `MatchMeta.round` plus a `round_advance`
     * event, wrapped in the `round_end` / `round_start` hook dispatch.
     * Turn timers, window timers, real-time statuses and scheduled graphs are
     * the seam.
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
     * @brief Pending op-input envelope (`InputRequest`), or nullopt.
     *
     * Shape: `{kind, target, payload}`. Set when a resolver op returned
     * `kNeedsInput`; cleared by `SubmitInput`.
     */
    std::optional<nlohmann::json> PendingInput() const;

    /**
     * @brief Pending response-window request, or nullopt.
     *
     * The engine detects a `kWindow` pause and parks it here; the engine owns
     * opening, timing, collecting responses and `Resolver::ResumeWindow`.
     */
    std::optional<resolver::WindowRequest> PendingWindow() const;

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

    /** @brief A paused op awaiting `SubmitInput`. */
    struct InputPause {
        ecs::Entity target{};               /**< asked player. */
        bool has_target = false;            /**< prompt declared a target. */
        std::string kind;                   /**< prompt kind. */
        nlohmann::json payload = nlohmann::json::object();
        resolver::ResolveResult pause;      /**< carries the resume token. */
        resolver::SelectorContext context;  /**< selectors at the pause. */
        ops::ResolutionFrame frame;         /**< prompt bindings. */
        std::size_t system_index = 0;       /**< system that paused. */
        std::string mod_id;
        bool settle_play = false;           /**< settle the play on resume. */
        ecs::Entity actor{};                /**< play actor to settle. */
    };

    /** @brief A paused response window parked. */
    struct WindowPause {
        resolver::WindowRequest request;
        resolver::ResolveResult pause;
        resolver::SelectorContext context;
        std::size_t system_index = 0;
        std::string mod_id;
    };

    /** @brief Append an event descriptor to the log. */
    void Emit(std::string_view type, nlohmann::json payload);

    /** @brief The live current-turn player, or nullopt. */
    std::optional<ecs::Entity> CurrentPlayer() const;

    /**
     * @brief Advance the turn through the `advance_turn` op body.
     *
     * Wrapped in the `turn_end` (before-veto = extra turn) and
     * `turn_start` hooks; the op keeps direction / extra-turn / one-shot-skip
     * bookkeeping in exactly one place. Appends the op's `turn_advance` event
     * and counts a seat change toward the round boundary.
     */
    void AdvanceTurn();

    /**
     * @brief Move the discard (except its top) back onto the draw pile and
     *        shuffle it with the match RNG (legacy `ReshuffleDiscardIntoDraw`).
     *
     * Fires `pile_empty` (before-veto blocks the reshuffle) and `shuffle`.
     *
     * @return false when there is nothing to reshuffle or it was vetoed.
     */
    bool ReshuffleDiscardIntoDraw();

    /** @brief Shuffle the draw pile and renumber its `in_zone` ordinals. */
    void ShuffleDrawPile();

    /** @brief Top-of-discard card entity, or nullopt. */
    std::optional<ecs::Entity> TopDiscard() const;

    /** @brief Apply the engine default win/placement for `player`. */
    void DeclareHandEmptyWin(ecs::Entity player);

    /** @brief `hand_empty` / `win_check` settle after a play. */
    void SettleAfterPlay(ecs::Entity player);

    // --- the engine hook / resolver path
    // ---------------------------------------

    /** @brief True while an op input or response window is parked. */
    bool Paused() const;

    /**
     * @brief Dispatch a before-hook through the EventBus and drain runs.
     *
     * @param name Hook name.
     * @param data Mutable payload; updated from the hooks' final `data`.
     * @return true when a veto-capable hook vetoed the engine default.
     */
    bool Before(const char* name, nlohmann::json& data);

    /** @brief Dispatch an after-hook through the EventBus and drain runs. */
    void After(const char* name, const nlohmann::json& data);

    /**
     * @brief Drain newly recorded `assembly.runs` into the event log.
     *
     * Appends each run's op events in order, bridges op events to their
     * observation hooks (`status_*`, `roll`, `visibility_*`,
     * `effect_applied`) and parks any `kNeedsInput` / `kWindow` pause.
     * Reentrant
     * calls return immediately; the outer loop consumes the new runs.
     */
    void CollectRuns();

    /** @brief Bridge one op event descriptor to its hook, if any. */
    void BridgeRunEvent(const nlohmann::json& event);

    /**
     * @brief Run must-apply auto cards for `trigger`.
     *
     * Scans `AutoTrigger` components with `must_apply` whose condition matches,
     * auto-plays one per iteration (never the same card twice) and emits
     * `auto_played`; bounded by the Resolver's `must_apply_cap`.
     */
    void RunMustApply(const char* trigger);

    /** @brief First un-played must-apply card whose condition matches. */
    std::optional<ecs::Entity> FindMustApplyCard(
        const std::vector<ecs::Entity>& played);

    /** @brief Evaluate an `AutoTrigger` condition against true engine state. */
    bool AutoConditionMatches(ecs::Entity card);

    /** @brief Auto-play `card`: move, zone hooks, graph, `auto_played`. */
    bool AutoPlayCard(ecs::Entity card, const char* trigger);

    /**
     * @brief Append a resumed resolution and park any further pause.
     *
     * @param result       The `ResumeInput` result.
     * @param system_index Paused system index (for a further pause).
     * @param mod_id       Paused system's owning mod.
     * @param context      Selectors at the pause.
     * @param frame        Prompt bindings after the injected value.
     * @param settle_play  Settle the interrupted play when the graph drains.
     * @param actor        Play actor to settle.
     */
    void AppendResult(const resolver::ResolveResult& result,
                      std::size_t system_index, const std::string& mod_id,
                      const resolver::SelectorContext& context,
                      const ops::ResolutionFrame& frame, bool settle_play,
                      ecs::Entity actor);

    /** @brief Bind the selectors a condition may read (mirrors assembly). */
    void BindConditionSelectors(const resolver::SelectorContext& context,
                                ops::ResolutionFrame& frame);

    std::unique_ptr<MatchAssembly> assembly_;
    std::vector<nlohmann::json> events_;
    std::optional<LastPlay> last_play_;
    std::optional<ecs::Entity> winner_;
    uint64_t turns_elapsed_ = 0;  /**< distinct-seat advances so far. */
    bool started_ = false;
    bool finished_ = false;

    std::size_t collected_runs_ = 0;   /**< drained `assembly.runs` prefix. */
    bool collecting_runs_ = false;     /**< CollectRuns re-entry guard. */
    bool must_apply_active_ = false;   /**< RunMustApply re-entry guard. */
    std::optional<InputPause> pending_input_;
    std::optional<WindowPause> pending_window_;
};

}  // namespace match::engine
