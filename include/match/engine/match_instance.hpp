#pragma once

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/ecs/hooks.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/play_evaluator.hpp>
#include <match/ops/ops.hpp>
#include <match/resolver.hpp>
#include <match/scheduler.hpp>
#include <match/timers.hpp>

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
 *   `Resolver::ResumeInput`; a `kWindow` pause opens a response window; a
 *   `kSchedule` pause arms its deferred subgraph on the `Scheduler`.
 * - Runs must-apply auto cards (`AutoTrigger.must_apply`) before a
 *   window would open, one per triggering event, emitting `auto_played`; the
 *   Resolver's `must_apply` depth cap bounds re-triggering.
 *
 * Response windows: a `kWindow`
 * pause opens a `WindowState` on the match entity (uniform responders, the
 * `respond_with` eligibility filter, the default and response routes) and arms
 * the `MatchTimers` window clock, which suspends the turn clock.
 * `RespondWindow` plays an eligible card through the normal restriction /
 * `before:play` pipeline and `PassWindow` records a pass; the first collected
 * response wins (arrival order). Early close fires once every responder has
 * replied, timeout routes the default. The winning route resumes through
 * `Resolver::ResumeWindow`, emitting `window_open` / `window_response` /
 * `window_close`.
 *
 * Draw stacking, built on top of the
 * window flow. `draw_cards` gains an optional `n_from_debt` arg that draws a
 * target's accumulated `vanilla:draw_debt` status magnitude; a window node may
 * declare `reopen: true` so a winning response appends its own draw penalty to
 * the debt and re-opens the window on the same budget ledger; and a window that
 * arrives while an op input is parked is deferred rather than dropped, then
 * opened once the input resolves (the wild_draw4 prompt + draw_stacking window
 * case).
 *
 * Scheduled graphs: a
 * `kSchedule` pause arms its `next` subgraph on the owned `Scheduler` with
 * a self-describing envelope (owning system, mod, resume node, pending stack
 * and `SelectorContext`); `Tick` runs the `ms` legs whose duration elapsed and
 * the `turns`/`rounds`/`cards_played` legs run from the turn-end, round-end and
 * `after:play` points respectively. Each elapsed subgraph resolves as a FRESH
 * chain and feeds the same `AppendResult` pause path, so one that pauses on
 * input/window/another schedule parks correctly.
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

    /**
     * @brief Take ownership with an injected wall-clock seam.
     *
     * @param assembly A successful `MatchAssembler::Assemble` result.
     * @param clock    Deterministic `NowMs` for window/turn timers.
     */
    MatchInstance(std::unique_ptr<MatchAssembly> assembly, match::NowMs clock);

    /**
     * @brief Take ownership with explicit window config and clock.
     *
     * @param assembly      A successful `MatchAssembler::Assemble` result.
     * @param window_config Resolved `UNI_WINDOW_MS` / `UNI_WINDOW_MODE`.
     * @param clock         Deterministic `NowMs` for window/turn timers.
     */
    MatchInstance(std::unique_ptr<MatchAssembly> assembly,
                  match::WindowConfig window_config, match::NowMs clock);

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
     * @brief Answer an open response window with an eligible card.
     *
     * The responder must be in the window's responder set and have not yet
     * replied. The card must be in the responder's hand, pass the
     * restriction pipeline and the window's `respond_with` eligibility filter,
     * and survive a `before:play` veto. The first accepted response wins
     * (arrival order); later accepted responses are losers and their cards stay
     * in hand. The window closes early once every responder has replied.
     *
     * @return true when the response was accepted; false on a closed/absent
     *         window, a non-responder, a duplicate reply, an ineligible or
     *         rejected card.
     */
    bool RespondWindow(const std::string& username, ecs::Entity card);

    /**
     * @brief Declare a pass in an open response window.
     *
     * The responder must be in the window's responder set and have not yet
     * replied. The window closes early once every responder has replied; a
     * window with no accepted response routes its default route.
     *
     * A window with a hold (`WindowHoldMs() > 0`) has no pass for anyone but
     * the draw-debt victim, and none for them before the hold elapses; that
     * pass closes the group at once and the defaults draw the debt.
     *
     * @return true when the pass was recorded; false otherwise.
     */
    bool PassWindow(const std::string& username);

    /** @brief True while a response window is open on the match entity. */
    bool WindowOpen() const;

    /**
     * @brief The open group's hold in ms; 0 when none is open or held.
     *
     * The hold is the duration of the group's `jump_in`-kind member(s) (the
     * shortest one), capped at the group's own duration.
     */
    int64_t WindowHoldMs() const;

    /** @brief True when no window is open, it has no hold, or it elapsed. */
    bool WindowHoldElapsed() const;

    /**
     * @brief True when `RespondWindow(player, card)` would accept the card.
     *
     * Side-effect free: open window, pending responder, card in hand, the
     * restriction pipeline and the window's `respond_with`. Drives the
     * snapshot's `can_play` while a window is open.
     */
    bool CanRespondWindow(ecs::Entity player, ecs::Entity card) const;

    /**
     * @brief Build the single play-legality authority for this state.
     *
     * Captures the restriction pipeline, the discard top, the active type and
     * the current player once; `CheckPlayRestrictions`, `CanRespondWindow` and
     * `ResponseEligible` delegate to it. The evaluator borrows the match and
     * must not outlive it.
     */
    PlayEvaluator MakePlayEvaluator() const;

    /**
     * @brief Deterministic response-window state JSON (headless assertions).
     *
     * Shape: `{open, id, responders[], default_route, filter_digest,
     * deadline_ms, respond_with, responses[]}`; `responses[]` entries carry
     * `{player, card, pass, outcome}`. Empty object when no window is parked.
     */
    nlohmann::json ExportWindow() const;

    /** @brief The match's the timer layer window/turn timers (arm / inspect). */
    match::MatchTimers& Timers() { return timers_; }
    /** @brief The match's the timer layer window/turn timers (const). */
    const match::MatchTimers& Timers() const { return timers_; }

    /**
     * @brief Arm the current player's turn clock for `duration_ms`.
     *
     * The production seam for the turn deadline: the controller calls this
     * on every turn start so `ecs::TurnState::turn_deadline_ms` carries an
     * absolute epoch-ms value into snapshots and `turn_advance`. Uses the
     * engine's own clock through the `TurnTimer`.
     *
     * @param duration_ms Turn length in milliseconds.
     * @return true when a current player's `TurnState` was armed.
     */
    bool ArmCurrentTurnDeadline(int64_t duration_ms);

    /**
     * @brief Keep the turn and prompt clocks separate for the current state.
     *
     * The turn clock is armed once per turn (never re-armed by an action
     * inside the same turn) and paused while a prompt or a response window
     * is pending. Each pending prompt gets its own deadline the first time
     * it is seen. The controller calls this after every state change.
     *
     * @param duration_ms Length of a fresh turn and of a fresh prompt.
     */
    void SyncClocks(int64_t duration_ms);

    /**
     * @brief Absolute epoch-ms deadline of the current turn (0 when unarmed
     *        or paused).
     */
    int64_t CurrentTurnDeadlineMs() const;

    /**
     * @brief Advance engine-side counters (round boundary).
     *
     * Turns elapsed seat cycles into `MatchMeta.round` plus a `round_advance`
     * event, wrapped in the `round_end` / `round_start` hook dispatch.
     * Then advances the `MatchTimers`: an open window times out (default
     * route) or early-closes (winning/default route), otherwise the turn
     * deadline is checked. Finally it runs every scheduled graph whose
     * duration elapsed; real-time statuses remain the seam.
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
     * @brief The open response window's request, or nullopt.
     *
     * Set when a `kWindow` pause opens the window and held until it closes
     * (timeout, all-pass or a winning response); see `ExportWindow` for the
     * live responder/response state.
     */
    std::optional<resolver::WindowRequest> PendingWindow() const;

    /**
     * @brief Every member's responders and `respond_with` of the open group.
     *
     * A response is accepted when a member whose own responders include the
     * player has a filter accepting it (member order = mod load order).
     * Empty when no window is parked.
     */
    std::vector<PlayEvaluator::WindowView::Member> WindowFilters() const;

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
        bool penalty_recorded = false; /**< its draw debt is on record. */
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
        int64_t deadline_ms = 0;            /**< absolute epoch ms; 0 = unarmed. */
        int64_t duration_ms = 0;            /**< armed clock length. */
    };

    /**
     * @brief The open response window's parked resolver continuation (12).
     *
     * A window group holds one pause per concurrent window: this pause is the
     * first member (mod load order) and owns the group's collection state;
     * `followers` are the members merged into the group after it.
     */
    struct WindowPause {
        resolver::WindowRequest request;
        resolver::ResolveResult pause;
        resolver::SelectorContext context;
        std::size_t system_index = 0;
        std::string mod_id;
        // INFO: Response collection. `winner` is the first accepted
        //       non-pass responder; its card is committed on close. Losers
        //       never leave their hand.
        ecs::Entity winner{};
        ecs::Entity winner_card{};
        bool has_winner = false;
        uint64_t next_arrival = 0;   /**< server arrival counter. */
        bool settle_play = false;  /**< settle the interrupted play on close. */
        ecs::Entity actor{};       /**< play actor to settle. */
        /** Members merged into this group's window, in mod load order. */
        std::vector<WindowPause> followers;
        /** Member (0 = this pause) owning the winning response. */
        std::size_t winner_member = 0;

        std::size_t MemberCount() const { return followers.size() + 1; }
        const WindowPause& Member(std::size_t index) const {
            return index == 0 ? *this : followers[index - 1];
        }
    };

    /** @brief A window member's route waiting to resume after a close. */
    struct QueuedRoute {
        WindowPause member;  /**< one member; `followers` empty. */
        std::string route;   /**< node id to resume. */
    };

    /** @brief A `play_card` effect request queued from a drained op graph. */
    struct ForcedPlay {
        ecs::Entity player{};  /**< acting player. */
        ecs::Entity card{};    /**< card to play through the normal pipeline. */
    };

    /** @brief Append an event descriptor to the log. */
    void Emit(std::string_view type, nlohmann::json payload);

    /** @brief The live current-turn player, or nullopt. */
    std::optional<ecs::Entity> CurrentPlayer() const;

    /**
     * @brief True when `bits` is one of the parked prompt's offered options.
     *
     * S-4: `choose_card` answers are restricted to the revealed subset the
     * prompt advertised (`pending_input_->payload["options"]`), not the whole
     * frozen index map, so a player cannot name an unoffered card. False when
     * no prompt is parked or the payload carries no option array.
     */
    bool OfferedChoice(int64_t bits) const;

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

    // --- the engine response-window path
    // ---------------------------------------

    /**
     * @brief Open a `kWindow` pause on the match entity.
     *
     * Ensures a `WindowState`, fills responders / routes / filter, arms the
     * The timer layer window timer (suspending the turn clock), emits
     * `window_open` and parks the pause in `pending_window_`. When
     * `fresh_situation` is true and the last play was a `draw_penalty` card,
     * the engine records that card's N as `vanilla:draw_debt` on the current
     * target; a re-open passes false so the response, not the situation, is
     * counted.
     *
     * @param pause           Parked resolver continuation.
     * @param fresh_situation True for a window opened by the triggering play.
     */
    void OpenWindow(WindowPause pause, bool fresh_situation = true);

    /**
     * @brief Merge windows collected in one dispatch into a single group.
     *
     * The first window (mod load order) becomes the group's pause and the rest
     * its `followers`; the group then opens as one window.
     */
    void OpenWindowGroup(std::vector<WindowPause> windows);

    /**
     * @brief Build the `PlayAttempt` for `card` by `player`.
     *
     * Mirrors the state the pipeline reads: `in_turn`, the discard top kind,
     * the active type and the acting hand's kind ids.
     */
    modload::PlayAttempt BuildPlayAttempt(ecs::Entity player, ecs::Entity card,
                                          bool in_turn) const;

    /** @brief Run the restriction pipeline for `attempt`. */
    modload::PlayDecision CheckPlayRestrictions(
        const modload::PlayAttempt& attempt) const;

    /** @brief True when `card` satisfies the window's `respond_with`. */
    bool ResponseEligible(const nlohmann::json& respond_with,
                          const modload::PlayAttempt& attempt) const;

    /**
     * @brief Derive the `on_response` route key for a winning response.
     *
     * Prefers the window's declared `filter_digest`, else the eligibility
     * filter's condition keyword / matched tag, else the sole `on_response`
     * key. Returns empty when no digest can be derived.
     */
    std::string ResponseDigest(const resolver::WindowRequest& request,
                               ecs::Entity card) const;

    /** @brief Route of `request` for a winning `card`, else `default_route`. */
    std::string WinningRoute(const resolver::WindowRequest& request,
                             ecs::Entity card) const;

    /**
     * @brief Commit the winning response card: move to discard + zone events.
     *
     * when `stacks_penalty` (the window stacks draw penalties) its
     * own N is appended to the accumulated `vanilla:draw_debt`, which moves
     * onto the seat after the responder. The window's `on_response` route
     * remains the resolution continuation; re-opening the window is
     * `CloseWindowRoute`'s.
     */
    void CommitWinningPlay(ecs::Entity player, ecs::Entity card,
                           bool stacks_penalty);

    /**
     * @brief Remove every seat's `vanilla:draw_debt`, emitting removals.
     *
     * @param keep Seat whose debt is left alone (may be empty).
     * @return the summed magnitude removed.
     */
    int32_t RemoveDrawDebt(std::optional<ecs::Entity> keep);

    /** @brief True when the window node declares engine-owned chaining. */
    bool WindowReopens(const resolver::WindowRequest& request) const;

    /**
     * @brief True when the window's `any_tag` filter would accept `card`, i.e.
     *        the window answers that draw penalty by stacking onto it.
     */
    bool WindowStacksPenalty(const resolver::WindowRequest& request,
                             ecs::Entity card) const;

    /** @brief The draw penalty N encoded by a card's face label, else 0. */
    int32_t DrawPenaltyMagnitude(ecs::Entity card) const;

    /**
     * @brief Put `card`'s draw penalty on the seat after `player`.
     *
     * The victim is the next seat in turn order from the player who laid the
     * card. Debt already accumulated on any other seat moves onto the victim
     * with it, so a stacked chain always sits on exactly one player.
     * Fail-safe no-op when the card is dead / not a `draw_penalty` card or its
     * magnitude is 0. Uses `status::Apply` with `accumulate`; emits and bridges
     * `status_removed` / `status_applied` events.
     */
    void RecordDrawPenalty(ecs::Entity card, ecs::Entity player);

    /**
     * @brief Close the open window group, emit one `window_close` and resume.
     *
     * With a winning response only the owning member's `on_response` route
     * runs; otherwise every member runs its `default_route` in member order.
     * A response on a debt-carrying play is owned by the group's stacking
     * member whichever member accepted it, so a jump-in accumulates the debt.
     *
     * @param outcome `timeout` / `all_pass` / `pass` / `response` (event
     *                payload).
     */
    void CloseWindowRoute(const std::string& outcome);

    /** @brief True when `player` carries a positive `vanilla:draw_debt`. */
    bool HoldsDrawDebt(ecs::Entity player) const;

    /**
     * @brief Resume the queued member routes in order.
     *
     * The last route settles the interrupted play; a route that parks a new
     * pause leaves the rest queued for `AppendResult` to continue.
     */
    void RunGroupRoutes(bool settle_play, ecs::Entity actor);

    /**
     * @brief First member (mod load order) whose window stacks `card`'s
     *        penalty and re-opens, i.e. the member that resolves a stack.
     */
    std::optional<std::size_t> StackingMember(const WindowPause& group,
                                              ecs::Entity card) const;

    /** @brief True when any group member's window stacks `card`'s penalty. */
    bool GroupStacksPenalty(const WindowPause& group, ecs::Entity card) const;

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

    /** @brief Append a resolved result's events (no pause / settle work). */
    void AppendEvents(const resolver::ResolveResult& result);

    /**
     * @brief Open the oldest window deferred behind a parked op input.
     *
     * The engine two-pause fix: a window that resolves in the same dispatch as
     * a `kNeedsInput` pause is queued instead of dropped; this opens it once
     * the input resolves. Inherits the interrupted play's settle intent.
     *
     * @return true when a deferred window was opened.
     */
    bool OpenDeferredWindow(bool settle_play, ecs::Entity actor);

    /** @brief Bind the selectors a condition may read (mirrors assembly). */
    void BindConditionSelectors(const resolver::SelectorContext& context,
                                ops::ResolutionFrame& frame);

    // --- the engine scheduled graphs ---------------------------

    /**
     * @brief Arm a `schedule` node's deferred subgraph on the Scheduler.
     *
     * Parses the duration (fail-safe WARN on a malformed spec, never arms)
     * and stores a self-describing envelope the Scheduler hands back on
     * expiry: the owning system index, mod, resume node, pending work stack
     * and the serialized `SelectorContext`.
     */
    void ArmSchedule(const resolver::ScheduleRequest& schedule,
                     std::size_t system_index, const std::string& mod_id,
                     const resolver::SelectorContext& context);

    /**
     * @brief Run every elapsed scheduled subgraph as a fresh Resolver chain.
     *
     * When a pause is already parked (`Paused()`) the envelopes are queued in
     * `deferred_scheduled_` instead and run once that pause resolves, so an
     * elapsed schedule is never dropped (mirrors `deferred_windows_`).
     * Otherwise it drains them via `RunScheduled`.
     */
    void ExecuteScheduled(const std::vector<nlohmann::json>& envelopes);

    /**
     * @brief Run a batch of elapsed envelopes in arm order.
     *
     * Stops and defers the remaining envelopes if one parks a pause, and
     * drains any forced `play_card` effects the subgraphs queued once the
     * batch completes. Callers must have checked `Paused()` first.
     */
    void RunScheduled(const std::vector<nlohmann::json>& envelopes);

    /**
     * @brief Run one elapsed envelope: rebuild context, resolve, append.
     *
     * A `system_index` out of range logs a WARN and drops the entry; the
     * resolved result feeds the shared `AppendResult` pause path and its
     * effects are queued exactly like `CollectRuns` does.
     */
    void RunScheduledEnvelope(const nlohmann::json& envelope);

    /**
     * @brief Run any elapsed schedules deferred behind a now-resolved pause.
     *
     * No-op while a pause is still parked. Moves the queue out before running
     * so a nested resolve cannot re-enter it.
     */
    void DrainDeferredScheduled();

    /** @brief Current time from the clock the timers were built with. */
    int64_t Now() const;

    // --- the engine forced-play routing
    // ------------------------------------------

    /**
     * @brief Queue `play_card` effect descriptors from a drained op graph.
     *
     * A `play_card` op cannot run the engine play pipeline itself; it emits an
     * effect and the engine executes it via `PlayCard` at a flow-safe point.
     * Malformed descriptors are ignored (fail-safe).
     */
    void QueueForcedPlays(const std::vector<nlohmann::json>& effects);

    /**
     * @brief Run queued forced plays through the normal play pipeline.
     *
     * Bounded cascade (a forced play's own graphs may queue further requests).
     * @return true when at least one play was accepted.
     */
    bool ExecuteForcedPlays();

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
    std::vector<WindowPause> deferred_windows_;  /**< queued. */
    /** Member routes still to resume after a group close (merged windows). */
    std::vector<QueuedRoute> group_routes_;
    /** Elapsed schedules parked behind a pause. */
    std::vector<nlohmann::json> deferred_scheduled_;
    std::vector<ForcedPlay> forced_plays_;  /**< queued `play_card` effects. */
    match::Scheduler scheduler_;      /**< deferred-graph arm/expiry. */
    match::MatchTimers timers_;       /**< disjoint window/turn clocks. */
    /** Length the controller last armed the turn/prompt clocks with. */
    int64_t clock_duration_ms_ = 0;
    std::optional<ecs::Entity> turn_clock_owner_; /**< player the turn clock was armed for. */
    uint32_t next_window_id_ = 0;     /**< monotonic window id. */
};

}  // namespace match::engine
