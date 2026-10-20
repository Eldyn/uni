#pragma once

#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/server/ready_barrier.hpp>
#include <match/server/snapshot_request_limiter.hpp>
#include <match/view/event_tail.hpp>
#include <match/view/view_builder.hpp>
#include <transport/ibroadcaster.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @file match_session.hpp
 * @brief Additive server-side match session.
 *
 * `MatchSession` is the transport-facing owner of one new-engine match. It
 * binds the `match::engine::MatchInstance` to the view layer and
 * exposes both directions of the match protocol:
 *
 * - input: `PlayCard` / `DrawCard` / `SubmitInput` / `RespondWindow` /
 *   `PassWindow` / `Tick`, mapping wire `CompactCardV2.bits` to an entity
 *   through the frozen `MatchRegistries`;
 * - output: `EmitEvents` (per-recipient filtered `match_event` envelopes),
 *   `BroadcastSnapshot` (per-recipient `match_state_updated`) and
 *   `BroadcastMatchOver` (the terminal `match_over` packet).
 *
 * One `EventSink` is kept PER RECIPIENT for the session's life so the
 * per-viewer `seq` stream never resets and never renumbers.
 * The session owns the engine and the loaded mod vector: `ViewBuilder` caches
 * content tables at construction and holds them for the session's life, so
 * the mod vector must outlive the builder (declared before it).
 *
 * ADDITIVE: this slice does not flip `Lobby`'s
 * ownership of the legacy `match::MatchInstance`, and does not register
 * WebSocket handlers; the session layer wires the controller, the controller
 * deletes the old path.
 */

namespace match::server {

/**
 * @class MatchSession
 * @brief Owns a new-engine match and drives its per-recipient wire output.
 */
class MatchSession {
public:
    /** @brief Recipient username -> transport socket (opaque in tests). */
    using SocketMap = std::unordered_map<std::string, AppWebSocket*>;

    /**
     * @brief Bind an assembled match to its recipients.
     *
     * The engine must be a live, started `MatchInstance`; the mod vector must
     * be the same one the engine was assembled from (the view layer reads
     * faces, tags, hidden statuses and signal audiences from it).
     *
     * @param engine  Live engine, ownership taken.
     * @param mods    Loaded mods backing the match (kept for the session).
     * @param sockets Recipient username -> socket map.
     */
    MatchSession(std::unique_ptr<match::engine::MatchInstance> engine,
                 std::vector<match::modload::LoadedMod> mods,
                 SocketMap sockets);

    /**
     * @brief Bind an assembled match to a mod set shared with other matches.
     *
     * Lobbies that play the same mods point at one immutable vector instead
     * of holding a copy each.
     */
    MatchSession(
        std::unique_ptr<match::engine::MatchInstance> engine,
        std::shared_ptr<const std::vector<match::modload::LoadedMod>> mods,
        SocketMap sockets);

    MatchSession(const MatchSession&) = delete;
    MatchSession& operator=(const MatchSession&) = delete;
    MatchSession(MatchSession&&) = delete;
    MatchSession& operator=(MatchSession&&) = delete;
    ~MatchSession() = default;

    // --- input --------------------------------------------------------------

    /**
     * @brief Play a wire-addressed card for `username`.
     *
     * @param username Acting player.
     * @param card_bits Packed `CompactCardV2.bits`.
     * @return false when `card_bits` maps to no assembled card or the engine
     *         rejected the play.
     */
    bool PlayCard(const std::string& username, uint32_t card_bits);

    /**
     * @brief Draw one card for `username`.
     */
    bool DrawCard(const std::string& username);

    /**
     * @brief Keep the parked play/keep choice for `username`.
     *
     * Thin forwarding to `MatchInstance::KeepDrawn`: only the player whose
     * playable voluntary draw parked the choice may resolve it. Clears the
     * choice and advances the turn on success.
     *
     * @param username Acting player.
     * @return true when `username` owned the parked choice.
     */
    bool KeepDrawn(const std::string& username);

    /**
     * @brief Answer the parked op-input prompt.
     *
     * Validates the `{prompt_id, value}` pair against the pending input before
     * calling `MatchInstance::SubmitInput`: `prompt_id` must equal the parked
     * prompt's kind, the caller must be its target, and `value` must satisfy
     * the kind's `response_schema` when a schema is known. A rejected pair
     * leaves the prompt parked (`false`, no engine call).
     *
     * @param username  Answering player.
     * @param prompt_id Wire prompt id (the prompt kind, see `prompt_opened`).
     * @param value     JSON answer to validate against `response_schema`.
     * @return true when the answer was validated and accepted by the engine.
     */
    bool SubmitInput(const std::string& username, const std::string& prompt_id,
                     const nlohmann::json& value);

    /**
     * @brief Answer an open response window with a wire-addressed card.
     */
    bool RespondWindow(const std::string& username, uint32_t card_bits);

    /**
     * @brief Declare a pass in an open response window.
     */
    bool PassWindow(const std::string& username);

    /** @brief Advance engine timers / round bookkeeping. */
    void Tick();

    /**
     * @brief Arm the current player's engine turn deadline.
     *
     * Thin forwarding to `MatchInstance::ArmCurrentTurnDeadline`; the
     * controller calls it on every turn start so the snapshot / `turn_advance`
     * carry a live absolute deadline. The AFK/bot policy stays the
     * controller's single-shot timer.
     *
     * @param duration_ms Turn length in milliseconds.
     * @return true when the current player's turn clock was armed.
     */
    bool ArmTurnTimer(int64_t duration_ms);

    // --- identity / socket rebinding ----------------------------------------

    /**
     * @brief Rebind an existing engine seat to a new username and socket.
     *
     * Used for a mid-game bot hijack: renames the engine player's
     * `PlayerInfo.username`, moves its per-recipient event sink and prompt
     * bookkeeping to the new key, and updates the socket map. The seat, its
     * hand and its turn state are untouched. A same-name call flips the
     * seat to a human in place (the Creative flip op path).
     *
     * @param old_username Current engine username of the seat.
     * @param new_username Username to bind the seat to.
     * @param socket       New socket, or nullptr to leave the current binding.
     * @return false when the old seat does not exist or a name is empty.
     */
    bool RebindPlayer(const std::string& old_username,
                      const std::string& new_username,
                      AppWebSocket* socket);

    /**
     * @brief Rename an engine seat, preserving its bot/connected flags.
     *
     * Unlike RebindPlayer (a bot hijack that always humanizes), this keeps
     * `PlayerInfo.is_bot` / `connected` and moves the socket, stream, prompt
     * and director/privacy/skin keying to the new name. Used by the Creative
     * director rename op.
     *
     * @param old_username Current engine username of the seat.
     * @param new_username Username to bind the seat to.
     * @return false when either name is empty, the names match, the old seat
     *         does not exist, or the new name is already taken.
     */
    bool RenameSeat(const std::string& old_username,
                    const std::string& new_username);

    /**
     * @brief Hand a departing human's engine seat to a named bot.
     *
     * Renames the seat like RebindPlayer, flags it as a bot and drops the
     * departing socket so the leaver stops receiving the match stream and
     * cannot be re-seated by a username-keyed rejoin. A same-name call
     * flips the seat to a bot in place (the Creative flip op path).
     *
     * @param old_username Departing human's engine username.
     * @param bot_name     Replacement bot username.
     * @return false when the old seat does not exist or a name is empty.
     */
    bool HandSeatToBot(const std::string& old_username,
                       const std::string& bot_name);

    /**
     * @brief Override the avatar strip a seat renders with.
     *
     * Display-only: never touches `PlayerInfo.is_bot` (turn logic reads
     * that). The snapshot carries the override as the player entry's
     * `skin`; the client picks the bot/player strip from it.
     *
     * @param username Seated engine username.
     * @param skin     "bot" or "player".
     * @return false when the seat is unknown or the value is invalid.
     */
    bool SetSeatSkin(const std::string& username, const std::string& skin);

    /** @brief The skin override for `username`, or empty when none. */
    std::string SeatSkin(const std::string& username) const;

    /** @brief All seat -> ("bot"|"player") skin overrides. */
    std::unordered_map<std::string, std::string> SkinOverrides() const;

    /** @brief True for the two accepted skin override values. */
    static bool IsValidSkinValue(const std::string& skin);

    /**
     * @brief Update the socket bound to a seated recipient (reconnect).
     * @param username Seated player username.
     * @param socket   Replacement socket.
     * @return false when `username` is not a session recipient.
     */
    bool BindSocket(const std::string& username, AppWebSocket* socket);

    /**
     * @brief Register (or rebind) a spectator viewer socket.
     *
     * Spectators are not engine seats: they receive a persistent per-viewer
     * event stream built with the spectator view (`SpectatorViewer`), so a
     * mid-match joiner keeps receiving `match_event` / `match_state_updated` /
     * `match_over` after their initial snapshot. Additive: seated behaviour
     * is untouched.
     *
     * @param username Spectator username.
     * @param socket   Spectator socket; nullptr keeps the entry unbound.
     */
    void BindViewer(const std::string& username, AppWebSocket* socket);

    /**
     * @brief Remove a spectator viewer's socket and per-viewer stream.
     * @param username Spectator username.
     * @return true when a spectator entry existed and was removed.
     */
    bool UnbindViewer(const std::string& username);

    /**
     * @brief Set the Streamer/Privacy Mode of the owner of a seat.
     *
     * The setting applies to every spectator packet that the session builds
     * after this call: a spectator that joins later and the next client event
     * and snapshot of a spectator that watches now.
     *
     * @param username Owner of the seat.
     * @param enabled  True: spectators see card backs for this seat.
     */
    void SetSeatPrivacy(const std::string& username, bool enabled);

    /** @brief Mark or clear a host Creative director for VIP x-ray. */
    void SetDirector(const std::string& username, bool enabled);

    /** @brief True when username has Creative x-ray. */
    bool IsDirector(const std::string& username) const;

    /** @brief Stack exact kinds on top of the draw pile, first kind drawn first. */
    bool StackDrawPile(const std::vector<std::string>& kinds);

    /** @brief Apply a Creative scene: exact hands, pile top, discard top. */
    bool ApplyScene(const nlohmann::json& scene,
                    std::map<std::string, nlohmann::json>* out_intents);

    /** @brief Capture visible match state as a scene-equivalent checkpoint. */
    nlohmann::json SaveCheckpoint();

    /** @brief Restore a checkpoint captured by SaveCheckpoint. */
    bool RestoreCheckpoint(const nlohmann::json& checkpoint,
                           std::map<std::string, nlohmann::json>* out_intents);

    /** @brief Store a checkpoint in the session slot. */
    void StoreCheckpoint(nlohmann::json checkpoint);

    /** @brief True when the session holds a stored checkpoint. */
    bool HasCheckpoint() const;

    /** @brief The stored checkpoint. */
    const nlohmann::json& Checkpoint() const;

    /** @brief Stage scene intents for the turn driver to queue. */
    void SetSceneIntents(std::map<std::string, nlohmann::json> intents);

    /** @brief Take and clear staged scene intents. */
    std::map<std::string, nlohmann::json> TakeSceneIntents();

    /** @brief The spectator viewer with the current private seats. */
    match::view::Viewer SpectatorViewer() const;

    /**
     * @brief Turn a seat into a spectator (an eliminated seat).
     *
     * The socket and the stream of the seat move to the spectator registry.
     * The `sequence` of the stream continues without a gap. The seat gets the
     * spectator projection from the next packet.
     *
     * @param username Username of the seat.
     * @return true when the seat existed and moved.
     */
    bool DemoteSeatToSpectator(const std::string& username);

    // --- wire output --------------------------------------------------------

    /**
     * @brief Emit `defs` then `match_start` once, before any other packet.
     *
     * For every seated recipient with a live socket this pushes the
     * content-derived `defs` kind table ( needed to decode
     * `CompactCardV2.bits`) and then `match_start` through that recipient's
     * PERSISTENT `EventSink`, exactly like `EmitEvents` (`action` =
     * `match_event`), so `seq` stays monotonic and the following snapshot's
     * watermark reconciles. Both packets are `all`-visibility, so the payload
     * is identical for everyone. Idempotent: a second call in one session is
     * a no-op.
     *
     * The controller calls this once at match creation, before the first
     * `EmitEvents` / `BroadcastSnapshot`.
     *
     * @param broadcaster Transport sink for `SendJson`.
     */
    void EmitMatchStart(IBroadcaster& broadcaster);

    /**
     * @brief Drain new engine events as per-recipient `match_event` packets.
     *
     * Each recipient's persistent `EventSink` filters and stamps the events
     * it may see; the cursor advances once over the shared engine log. A
     * parked op-input is additionally surfaced as the target's `prompt_opened`
     * (once per distinct prompt; spectators and other players never receive
     * it).
     *
     * @param broadcaster Transport sink for `SendJson`.
     */
    void EmitEvents(IBroadcaster& broadcaster);

    /**
     * @brief Send each recipient their `match_state_updated` snapshot.
     *
     * The snapshot reconciles exactly the packets already emitted into that
     * recipient's sink (its `seq` watermark is `EventSink::NextSeq()`). When
     * the match is finished this also sends `match_over` once per recipient
     *
     * @param broadcaster Transport sink for `SendJson`.
     */
    void BroadcastSnapshot(IBroadcaster& broadcaster);

    /**
     * @brief `BroadcastSnapshot` with lobby-owned snapshot options.
     *
     * Carries the per-player spectator counts the lobby computed for the
     * current member roster; the engine owns no lobby state, so
     * the caller supplies them. Internal snapshots (turn timer, bots) use the
     * option-less overload and emit zero counts.
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @param options     Lobby-owned snapshot inputs.
     */
    void BroadcastSnapshot(IBroadcaster& broadcaster,
                           const match::view::SnapshotOptions& options);

    /**
     * @brief Send one recipient their `match_state_updated` snapshot.
     *
     * Unlike `BroadcastSnapshot`, this targets a single socket so a joiner /
     * reconnect can be served without re-broadcasting to everyone. A seated
     * recipient is built against its persistent sink (correct `seq`
     * watermark); a spectator bound mid-match gets the omniscient spectator
     * view on its persistent viewer stream, with `defs` emitted first when the
     * stream has not seen it (the session, so late joiners can decode card
     * bits). An unbound/unknown recipient keeps the old fresh-stream view.
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @param socket      Recipient socket (nullptr is a no-op).
     * @param username    Recipient username.
     * @param is_spectator True to force the spectator view.
     */
    void SendSnapshot(IBroadcaster& broadcaster, AppWebSocket* socket,
                      const std::string& username, bool is_spectator);

    /**
     * @brief Send the terminal `match_over` packet once, if finished.
     *
     * Idempotent once delivered: later calls after every recipient accepted
     * the frame are no-ops. A backpressure drop on any recipient leaves the
     * sent-latch clear, so the next call (every `BroadcastSnapshot` ends
     * here) retries the one-shot instead of losing it. Called by
     * `BroadcastSnapshot`, exposed for controllers that finish a match
     * without a snapshot.
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @return true when the terminal frame has been accepted for all
     * recipients; false when the match is still running or a drop left
     * delivery pending.
     */
    bool BroadcastMatchOver(IBroadcaster& broadcaster);

    /**
     * @brief Answer a `match_snapshot_request` from a seat or a spectator.
     *
     * Sends the current snapshot. Its `seq_watermark` is the `sequence` of
     * the next packet of the recipient, so the client continues from it. A
     * recipient that asks again inside the rate limit interval gets nothing.
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @param username    Requesting seat or spectator.
     * @param options     Snapshot inputs the lobby owns.
     * @return true when a snapshot was sent.
     */
    bool HandleSnapshotRequest(IBroadcaster& broadcaster,
                               const std::string& username,
                               const match::view::SnapshotOptions& options);

    /** @brief Set the minimum time between two requests of one recipient. */
    void SetSnapshotRequestIntervalMs(int64_t min_interval_ms) {
        snapshot_request_limiter_.SetMinIntervalMs(min_interval_ms);
    }

    // --- ready barrier

    /**
     * @brief Arm the load barrier and broadcast the first `players_ready`.
     *
     * Seats with a live socket at start are pending; bots and
     * already-disconnected humans count as ready. `total` is the seated
     * player count (bots included, spectators excluded).
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @param timeout_ms Barrier timeout the controller arms; `players_ready`
     *        reports what is left of it (0 = none, field omitted).
     */
    void BeginReadyBarrier(IBroadcaster& broadcaster, int64_t timeout_ms = 0);

    /**
     * @brief Mark a seated player's client as loaded.
     *
     * @param username    Reporting player.
     * @param broadcaster Transport sink for `SendJson`.
     * @return true when the ready count changed (packet broadcast).
     */
    bool MarkSeatReady(const std::string& username, IBroadcaster& broadcaster);

    /** @brief True when every pending seat has reported. */
    bool ReadyBarrierComplete() const;

    /** @brief True once the barrier has opened (no more waiting). */
    bool ReadyBarrierOpen() const;

    /**
     * @brief Open the barrier and broadcast `match_begin` on first open.
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @return true on the first open only.
     */
    bool OpenReadyBarrier(IBroadcaster& broadcaster);

    // --- introspection (tests / controllers) --------------------------------

    /** @brief The owned engine. */
    match::engine::MatchInstance& Engine() { return *engine_; }
    /** @brief The owned engine (const). */
    const match::engine::MatchInstance& Engine() const { return *engine_; }
    /** @brief The bound view builder. */
    const match::view::ViewBuilder& View() const { return builder_; }
    /**
     * @brief The stored logical client events for a late joiner.
     *
     * The session records each engine event here one time, when it emits the
     * event. A warm-up snapshot (`SendSnapshot`, `HandleSnapshotRequest`)
     * projects the store for the joining viewer as `match_state.tail`.
     */
    const match::view::EventTail& Tail() const { return tail_; }
    /** @brief Index into `Engine().Events()` already emitted. */
    std::size_t EventCursor() const { return cursor_; }
    /** @brief True once `match_over` has been accepted for all recipients. */
    bool MatchOverNotified() const { return over_sent_; }

    /** @brief Record that a human seat left the match before the end. */
    void MarkSeatQuit(const std::string& username) {
        quit_seats_.insert(username);
    }

    /** @brief True when `username` left the match before the end. */
    bool SeatQuit(const std::string& username) const {
        return quit_seats_.contains(username);
    }

    /**
     * @brief Store the settled match payouts (username -> cash, only > 0).
     * `BroadcastMatchOver` writes them into the `match_over` frame.
     */
    void SetPayouts(std::map<std::string, std::int64_t> payouts) {
        payouts_ = std::move(payouts);
        payouts_settled_ = true;
    }

    /** @brief True after `SetPayouts`; the match is not paid twice. */
    bool PayoutsSettled() const { return payouts_settled_; }

    /** @brief The settled payouts; empty before `SetPayouts`. */
    const std::map<std::string, std::int64_t>& Payouts() const {
        return payouts_;
    }

    /** @brief Recipient socket map as supplied at construction. */
    const SocketMap& Sockets() const { return sockets_; }
    /** @brief Spectator viewer socket map. */
    const SocketMap& Viewers() const { return viewers_; }

    /**
     * @brief The content-derived `defs` table.
     *
     * `defs` depends only on the engine registries + mods fixed at
     * construction, so every recipient (and every late joiner via
     * `EnsureDefs`) shares one copy. When the assembly content publishes a
     * shared DOM (`AssemblyContent::defs`) every session with that content
     * returns the same object; otherwise the session builds and caches its own.
     * Same bytes as before; wire format unchanged.
     */
    const nlohmann::json& CachedDefs() const;

private:
    /** @brief Look up a prompt kind's `response_schema`, or nullptr. */
    const nlohmann::json* FindPromptSchema(const std::string& kind) const;

    /**
     * @brief Move the per-recipient stream/prompt state to a new seat key.
     *
     * The renamed seat inherits its `seq` watermark and prompt dedupe; a
     * missing entry is created empty so later lookups never insert.
     */
    void MoveSeatKeys(const std::string& old_username,
                      const std::string& new_username);

    /**
     * @brief Copy `options` with this session's skin overrides merged in.
     *
     * The session is the source of truth for skins; controller-built options
     * already carry them, so the merge is idempotent.
     */
    match::view::SnapshotOptions SnapshotOptionsWithSkins(
        const match::view::SnapshotOptions& options) const;

    /**
     * @brief True when `value` satisfies the supported schema subset.
     *
     * Supports the rendering vocabulary: `type` in string / boolean
     * / integer / number / object / array, `enum`, and numeric `minimum` /
     * `maximum`. An absent/empty schema or an unknown `type` token is
     * permissive (a mod kind with no declared schema is not blocked).
     */
    static bool MatchesSchema(const nlohmann::json& value,
                              const nlohmann::json& schema);

    /** @brief Fill the kind -> `response_schema` table from the mods. */
    void BuildPromptSchemas();

    /** @brief Emit a parked op-input as the target's `prompt_opened`. */
    void EmitPendingPrompt(IBroadcaster& broadcaster);

    /**
     * @brief Emit `defs` into a recipient stream that has not seen it yet.
     *
     * Late joiners (a spectator bound after start, or a seat that was
     * unreachable when `EmitMatchStart` ran) need the kind table to decode
     * card bits. When the match has started and `sink` is still at seq 0, this
     * wraps `defs` into that recipient's persistent sink and sends it, so the
     * stream stays monotonic and the next packet's watermark is consistent.
     * A no-op before start, on a null socket, or on a sink already advanced.
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @param socket      Recipient socket (non-null checked by the caller too).
     * @param sink        The recipient's persistent stream.
     */
    void EnsureDefs(IBroadcaster& broadcaster, AppWebSocket* socket,
                    match::view::EventSink& sink);

    /**
     * @brief Emit the target's `prompt_answered` when its prompt is gone.
     *
     * Synthesizes `{prompt_id, outcome}` through the recipient's persistent
     * `EventSink` (so the close consumes a `seq`), sends it as a
     * `match_event`, then resets the pending outcome. `prompt_id` is the
     * parked prompt kind recovered from the stored signature; the view layer
     * uses the kind as the prompt id.
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @param username    Recipient whose prompt just closed.
     * @param socket      Recipient socket (non-null, caller-checked).
     * @param signature   Last emitted pending-prompt signature.
     */
    void EmitPromptClose(IBroadcaster& broadcaster, const std::string& username,
                         AppWebSocket* socket, const std::string& signature);

    /** @brief Wrap `payload` as `type` into one recipient's persistent sink. */
    void SendBarrierPacket(IBroadcaster& broadcaster, AppWebSocket* socket,
                           match::view::EventSink& sink, const std::string& type,
                           const nlohmann::json& payload);

    /** @brief Send a barrier packet to every seated recipient and spectator. */
    void BroadcastBarrierPacket(IBroadcaster& broadcaster, const std::string& type,
                                const nlohmann::json& payload);

    /** @brief `{ready, total}` from the ready barrier. */
    nlohmann::json ReadyProgressPayload() const;

    std::unique_ptr<match::engine::MatchInstance> engine_;
    std::shared_ptr<const std::vector<match::modload::LoadedMod>> mods_;
    match::view::ViewBuilder builder_;
    match::view::EventTail tail_;
    SocketMap sockets_;
    /** Spectator viewer sockets (no engine seat);. */
    SocketMap viewers_;
    /** Owners with Streamer/Privacy Mode on, without duplicate. */
    std::vector<std::string> private_seats_;
    std::vector<std::string> directors_;
    /** Seat -> ("bot"|"player") display-only avatar-strip overrides. */
    std::unordered_map<std::string, std::string> skin_overrides_;
    nlohmann::json checkpoint_ = nullptr;
    std::map<std::string, nlohmann::json> scene_intents_;
    std::unordered_map<std::string, match::view::EventSink> sinks_;
    /** Per-spectator persistent stream (mirrors `sinks_` for viewers). */
    std::unordered_map<std::string, match::view::EventSink> viewer_sinks_;
    /** Last pending-prompt signature emitted per recipient (dedupe). */
    std::unordered_map<std::string, std::string> prompt_signature_;
    /** Outcome for the pending prompt's close per recipient. */
    std::unordered_map<std::string, std::string> prompt_outcome_;
    /** Prompt kind -> validated `response_schema` (mods + built-ins). */
    std::unordered_map<std::string, nlohmann::json> prompt_schemas_;
    /** Rate limit for `match_snapshot_request`. */
    SnapshotRequestLimiter snapshot_request_limiter_ =
        SnapshotRequestLimiter::FromEnv();
    /** Load barrier: seats pending the client's ready report. */
    ReadyBarrier ready_barrier_;
    /** Barrier timeout armed by the controller (0 = none) and its start. */
    int64_t barrier_timeout_ms_ = 0;
    std::chrono::steady_clock::time_point barrier_armed_at_{};
    std::size_t cursor_ = 0;   /**< emitted prefix of `Engine().Events()`. */
    bool over_sent_ = false;   /**< `match_over` already broadcast. */
    /** Human seats that left before the end (bot replacement or drop). */
    std::set<std::string> quit_seats_;
    /** Settled match payouts, carried by `match_over`. */
    std::map<std::string, std::int64_t> payouts_;
    bool payouts_settled_ = false;
    /** `defs` + `match_start` already emitted for the seated recipients. */
    bool match_start_sent_ = false;
    /** Built-once `defs` table, only when the content does not share one. */
    mutable std::optional<nlohmann::json> defs_cache_;
};

/**
 * @brief True when `kind` is an engine built-in prompt kind.
 *
 * Phase-1 built-ins (`choose_card`, `choose_color`, `choose_player`,
 * `choose_yes_no`, `choose_value`) are engine-owned: a mod declaration may add
 * a NEW kind but must never replace one of these schemas. Shared by
 * `MatchSession` and `HeuristicBotPolicy` so both wire validators agree.
 */
bool IsBuiltinPromptKind(const std::string& kind);

}  // namespace match::server
