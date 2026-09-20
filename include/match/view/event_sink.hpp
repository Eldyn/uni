#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/**
 * @file event_sink.hpp
 * @brief View layer: per-match `seq` and `all`-visibility packet shapes.
 *
 * The server-side seam between the engine event log (`MatchInstance::Events()`,
 * `{type, payload}` descriptors) and the wire envelope (
 * `{seq, type, payload}`). The view layer owns the monotonic `seq`, the ten
 * public event projections, and the `defs` packet (see `defs_builder.hpp`).
 *
 * The view layer will add per-recipient filtering on top of `Wrap` /
 * `ProjectPublicEvent` without changing this module's contract. A filtered-out
 * event is simply never wrapped, so the stream stays gap-free and numbers are
 * never reused.
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
 * Used for `defs_digest` and the `match_end` `final_digest`; it only has to be
 * stable within one build, not cryptographic.
 *
 * @param data Bytes to digest.
 * @return 16-character lowercase hex string.
 */
std::string StableDigest(std::string_view data);

/**
 * @class EventSink
 * @brief Per-match monotonic `seq` and wire-envelope assembly.
 *
 * One sink per match. `Wrap` stamps the next `seq` onto a projected payload;
 * `WrapPublic` is the convenience that extracts the descriptor body and
 * projects it when it is one of the `all`-visibility public events. An event
 * that is not projected yields `nullopt` and does NOT
 * consume a `seq`, so filtering may drop but never renumber.
 */
class EventSink {
public:
    /**
     * @brief Stamp `type` + `payload` with the next `seq` and advance.
     *
     * @param type    Wire packet type (one of the `x-packet-types`).
     * @param payload Projected payload object.
     * @return The `{seq, type, payload}` envelope.
     */
    nlohmann::json Wrap(const std::string& type, nlohmann::json payload);

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

    /** @brief The seq the next wrapped packet will carry. */
    uint32_t NextSeq() const { return next_seq_; }

    /** @brief Reset the stream to the first seq (new match / resend). */
    void Reset() { next_seq_ = 0; }

private:
    uint32_t next_seq_ = 0;
};

/**
 * @brief Project one `all`-visibility public event to its payload.
 *
 * Handles `card_played`, `reshuffle`, `turn_advance`, `round_advance`,
 * `placement`, `match_end`, `roll_result`, `signal`, `chain_aborted` and
 * `mod_disarmed`. `match_start` is content-derived and built by
 * `DefsBuilder::BuildMatchStart`. All other types return `nullopt` (deferred
 * to the view layer/c).
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
