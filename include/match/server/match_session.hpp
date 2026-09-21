#pragma once

#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/view/view_builder.hpp>
#include <transport/ibroadcaster.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
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
     * @brief Answer the parked op-input prompt.
     *
     * Validates the `{prompt_id, value}` pair against the pending input before
     * calling `MatchInstance::SubmitInput`: `prompt_id` must equal the parked
     * prompt's kind, the caller must be its target, and `value` must satisfy
     * the kind's `response_schema` when a schema is known. A rejected pair
     * leaves the prompt parked (`false`, no engine call).
     *
     * @param username  Answering player.
     * @param prompt_id Wire prompt id (the prompt kind, see `prompt_open`).
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
     * hand and its turn state are untouched.
     *
     * @param old_username Current engine username of the seat.
     * @param new_username Username to bind the seat to.
     * @param socket       New socket, or nullptr to leave the current binding.
     * @return false when the old seat does not exist or the names match.
     */
    bool RebindPlayer(const std::string& old_username,
                      const std::string& new_username,
                      AppWebSocket* socket);

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
     * event stream built with the omniscient spectator view, so a mid-match
     * joiner keeps receiving `match_event` / `match_state_updated` /
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
     * parked op-input is additionally surfaced as the target's `prompt_open`
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
     * Idempotent: later calls after the first send are no-ops. Called by
     * `BroadcastSnapshot`, exposed for controllers that finish a match
     * without a snapshot.
     *
     * @param broadcaster Transport sink for `SendJson`.
     * @return true when the match is over (packet sent at least once).
     */
    bool BroadcastMatchOver(IBroadcaster& broadcaster);

    // --- introspection (tests / controllers) --------------------------------

    /** @brief The owned engine. */
    match::engine::MatchInstance& Engine() { return *engine_; }
    /** @brief The owned engine (const). */
    const match::engine::MatchInstance& Engine() const { return *engine_; }
    /** @brief The bound view builder. */
    const match::view::ViewBuilder& View() const { return builder_; }
    /** @brief Index into `Engine().Events()` already emitted. */
    std::size_t EventCursor() const { return cursor_; }
    /** @brief True once a `match_over` packet has been sent. */
    bool MatchOverNotified() const { return over_sent_; }
    /** @brief Recipient socket map as supplied at construction. */
    const SocketMap& Sockets() const { return sockets_; }
    /** @brief Spectator viewer socket map. */
    const SocketMap& Viewers() const { return viewers_; }

private:
    /** @brief Look up a prompt kind's `response_schema`, or nullptr. */
    const nlohmann::json* FindPromptSchema(const std::string& kind) const;

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

    /** @brief Emit a parked op-input as the target's `prompt_open`. */
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
     * @brief Emit the target's `prompt_close` when its prompt is gone.
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

    std::unique_ptr<match::engine::MatchInstance> engine_;
    std::vector<match::modload::LoadedMod> mods_;
    match::view::ViewBuilder builder_;
    SocketMap sockets_;
    /** Spectator viewer sockets (no engine seat);. */
    SocketMap viewers_;
    std::unordered_map<std::string, match::view::EventSink> sinks_;
    /** Per-spectator persistent stream (mirrors `sinks_` for viewers). */
    std::unordered_map<std::string, match::view::EventSink> viewer_sinks_;
    /** Last pending-prompt signature emitted per recipient (dedupe). */
    std::unordered_map<std::string, std::string> prompt_signature_;
    /** Outcome for the pending prompt's close per recipient. */
    std::unordered_map<std::string, std::string> prompt_outcome_;
    /** Prompt kind -> validated `response_schema` (mods + built-ins). */
    std::unordered_map<std::string, nlohmann::json> prompt_schemas_;
    std::size_t cursor_ = 0;   /**< emitted prefix of `Engine().Events()`. */
    bool over_sent_ = false;   /**< `match_over` already broadcast. */
    /** `defs` + `match_start` already emitted for the seated recipients. */
    bool match_start_sent_ = false;
};

}  // namespace match::server
