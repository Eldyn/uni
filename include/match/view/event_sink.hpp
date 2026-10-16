#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

/**
 * @file event_sink.hpp
 * @brief View layer: per-match `sequence` and `all`-visibility packet shapes.
 *
 * The server-side seam between the engine event log (`MatchInstance::Events()`,
 * `{type, payload}` descriptors) and the wire envelope (
 * `{sequence, id, payload}`). The view layer owns the monotonic `sequence`, the
 * `all`-visibility public projections and the `defs` packet (see
 * `defs_builder.hpp`); the view layer adds the `window_*` / `auto_played`
 * public rows here plus per-recipient filtering in `view_builder.hpp`.
 *
 * Per-recipient filtering sits on top of `Wrap` / `ProjectPublicEvent`
 * without changing this module's contract. A filtered-out event is simply
 * never wrapped, so the stream stays gap-free and numbers are never reused.
 *
 * ADDITIVE: new `match::view` namespace; the engine is
 * untouched. Payload JSON is built directly against the table
 * because the generated `MatchEventPayload::Payload` inner object is an empty
 * placeholder.
 */

namespace match::engine {
class MatchInstance;
}  // namespace match::engine

namespace match::view {

/**
 * @brief Deterministic 64-bit FNV-1a digest rendered as lowercase hex.
 *
 * Used for `defs_digest` and the `match_ended` `final_digest`; it only has to be
 * stable within one build, not cryptographic.
 *
 * @param data Bytes to digest.
 * @return 16-character lowercase hex string.
 */
std::string StableDigest(std::string_view data);

/**
 * @class EventSink
 * @brief Per-match monotonic `sequence` and wire-envelope assembly.
 *
 * One sink per match. `Wrap` stamps the next `sequence` onto a projected payload;
 * `WrapPublic` is the convenience that extracts the descriptor body and
 * projects it when it is one of the `all`-visibility public events. An event
 * that is not projected yields `nullopt` and does NOT
 * consume a `sequence`, so filtering may drop but never renumber.
 */
class EventSink {
public:
    /**
     * @brief Stamp `type` + `payload` with the next `sequence` and advance.
     *
     * @param type    Wire packet type (one of the `x-packet-types`).
     * @param payload Projected payload object.
     * @return The `{sequence, id, payload}` envelope.
     */
    nlohmann::json Wrap(const std::string& type, nlohmann::json payload);

    /**
     * @brief Wrap a projected engine event with its causal frame.
     *
     * Adds `actor` (omitted for the table), `turn`, `round` and `cause` to
     * the envelope. `cause` is the `sequence` that THIS stream gave to the
     * causing event (`causal_frame.cause_event`). When this stream never
     * received the causing event, `cause` is omitted.
     *
     * A roll event gets the public roll ordinal of this stream (1, 2, 3, ...)
     * in place of the engine counter. The engine counter also counts hidden
     * random draws (a reshuffle), so it must never reach a seat.
     *
     * @param type         Wire packet type.
     * @param payload      Projected payload object.
     * @param engine_event The engine descriptor (`event_id`, `causal_frame`).
     * @return The envelope.
     */
    nlohmann::json WrapEngineEvent(const std::string& type,
                                   nlohmann::json payload,
                                   const nlohmann::json& engine_event);

    /**
     * @brief Project and wrap an engine descriptor when it is public.
     *
     * Accepts both descriptor shapes the engine emits: the `{type, payload}`
     * form (`MakeEvent`) and the flat resolver abort form
     * (`{type, mod, node}`). Returns `nullopt` for a non-public or unknown
     * type, or a non-object descriptor, without consuming a `seq`.
     *
     * @param engine_event One `MatchInstance::Events()` descriptor.
     * @param match        The live match (for state + registries).
     * @return The wire envelope, or `nullopt` when not a public event.
     */
    std::optional<nlohmann::json> WrapPublic(
        const nlohmann::json& engine_event,
        const match::engine::MatchInstance& match);

    /** @brief The sequence the next wrapped packet will carry. */
    uint32_t NextSeq() const { return next_seq_; }

    /** @brief Reset the stream to the first sequence (new match / resend). */
    void Reset() {
        next_seq_ = 0;
        roll_ordinal_ = 0;
        sequence_by_event_id_.clear();
        remembered_event_ids_.clear();
    }

private:
    /** Number of engine event ids that one stream remembers for `cause`. */
    static constexpr std::size_t kCauseMemoryLimit = 512;

    void RememberSequence(int64_t event_id, uint32_t sequence);

    uint32_t next_seq_ = 0;
    uint32_t roll_ordinal_ = 0;
    std::unordered_map<int64_t, uint32_t> sequence_by_event_id_;
    std::deque<int64_t> remembered_event_ids_;
};

/**
 * @brief Project one `all`-visibility public event to its payload.
 *
 * Handles the `all`-visibility rows of the table: `card_played`,
 * `reshuffle`, `turn_advance`, `round_advance`, `placement_set`,
 * `window_opened`, `window_responded`, `window_closed`, `auto_played`,
 * `match_ended`, `outcome_rolled`. `match_start` is
 * content-derived and built by `DefsBuilder::BuildMatchStart`. `signal` is
 * not handled here: its audience is declared per mod manifest and resolved
 * per recipient by `ViewBuilder`. All other types return `nullopt`
 * the per-recipient (filtered) rows are built by `ViewBuilder` in
 * `view_builder.hpp`.
 *
 * @param type    Engine event type token.
 * @param payload Descriptor body (or the flat descriptor minus `type`).
 * @param match   The live match (for entity -> wire conversion + end state).
 * @return The payload, or `nullopt` when `type` is not handled here.
 */
std::optional<nlohmann::json> ProjectPublicEvent(
    const std::string& type, const nlohmann::json& payload,
    const match::engine::MatchInstance& match);

}  // namespace match::view
