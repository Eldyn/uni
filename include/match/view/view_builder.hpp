#pragma once

#include <match/view/event_sink.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file view_builder.hpp
 * @brief the view layer/c per-recipient view filtering and reconnect snapshots
 *
 * Builds the payload a single viewer may see for each engine event descriptor
 * and wraps it through the `EventSink`. The `all`-visibility rows are
 * delegated to `ProjectPublicEvent`; the filtered rows are built here:
 *
 * - `play_rejected`, `prompt_open`, `prompt_close`: target player only.
 * - `cards_drawn`: the owner gets card identities when the descriptor carries
 *   them; every other viewer gets `count` only.
 * - `status_applied` / `status_removed`: a status whose definition is
 *   `hidden` is owner-only; public statuses go to everyone.
 * - `visibility_granted` / `visibility_revoked`: the named viewer only.
 * - `signal`: filtered by the declaring mod manifest's `signals[].audience`.
 *
 * The hidden-status table and the signal-audience table are built once from
 * the loaded mods (the assembly does not retain them); the engine never
 * populates a hidden flag on `ecs::Status`.
 *
 * ADDITIVE: new namespace `match::view`, engine
 * untouched. Payload JSON is built directly against.
 *
 * The view layer adds `BuildSnapshot` over the same builder: base
 * visibility, `VisibilityGrant` aspects, spectator omniscience with
 * per-player `privacy_from_spectators`, and the own-hand playability flag.
 */

namespace match::engine {
class MatchInstance;
}  // namespace match::engine

namespace match::modload {
struct LoadedMod;
}  // namespace match::modload

namespace match::view {

/**
 * @struct Viewer
 * @brief The recipient a view is built for.
 *
 * A player viewer is identified by `username`. A spectator has an empty
 * username and `is_spectator == true`; the view layer does not make spectators
 * omniscient, so a spectator never matches a target-only or
 * owner-only rule.
 */
struct Viewer {
    std::string username;         /**< player username; empty for spectator. */
    bool is_spectator = false;    /**< true for a non-seated connection. */

    /** @brief A seated player viewer. */
    static Viewer Player(std::string username) {
        Viewer viewer;
        viewer.username = std::move(username);
        viewer.is_spectator = false;
        return viewer;
    }

    /** @brief An anonymous spectator viewer. */
    static Viewer Spectator() {
        Viewer viewer;
        viewer.is_spectator = true;
        return viewer;
    }
};

/**
 * @struct SnapshotOptions
 * @brief Per-match snapshot inputs the engine does not own.
 *
 * The lobby owns the phase-1 per-player `privacy_from_spectators` toggle, so
 * the flag is supplied here rather than stored on the engine.
 * A listed player's hidden aspects (hand identity/colour/value/position and
 * hidden statuses) are stripped from spectator views; every other
 * player stays fully visible to a spectator (spectators default omniscient).
 */
struct SnapshotOptions {
    /** Usernames whose hidden aspects are hidden from spectators. */
    std::vector<std::string> privacy_from_spectators;

    /** @brief True when `username` opted into spectator privacy. */
    bool PrivacyOn(const std::string& username) const;
};


/**
 * @class ViewBuilder
 * @brief Filters the engine event log per recipient and stamps `seq`.
 *
 * Construct once per match (it caches the hidden-status and signal-audience
 * tables from the loaded mods) and call `BuildPackets` per recipient, or
 * `Wrap` per event against a caller-owned `EventSink` for an incremental
 * stream. `BuildPackets` starts a fresh sink so each recipient's stream
 * begins at `seq` 0; a filtered event consumes no number (gap-free).
 */
class ViewBuilder {
public:
    /**
     * @brief Bind to a live match and its loaded mod content.
     *
     * @param match Live engine (event log, state, registries).
     * @param mods  Loaded mods, the source of hidden status defs and the
     *              manifest `signals` audience declarations.
     */
    ViewBuilder(const match::engine::MatchInstance& match,
                const std::vector<match::modload::LoadedMod>& mods);

    /**
     * @brief Every packet `viewer` may see, in engine order, `seq`-stamped.
     *
     * Iterates `match.Events()` and wraps each visible descriptor. A fresh
     * `EventSink` is used, so the returned stream starts at `seq` 0 and is
     * gap-free for the viewer.
     *
     * @param viewer Recipient to build for.
     * @return The viewer's visible envelopes.
     */
    std::vector<nlohmann::json> BuildPackets(const Viewer& viewer) const;

    /**
     * @brief Filter and wrap one engine descriptor for `viewer`.
     *
     * Accepts both descriptor shapes (`{type, payload}` and the flat resolver
     * abort form). Returns `nullopt` when the event is not visible to
     * `viewer` (the caller must then not advance `sink`).
     *
     * @param engine_event One `MatchInstance::Events()` descriptor.
     * @param viewer       Recipient to build for.
     * @param sink         Per-recipient sequence source.
     * @return The viewer's envelope, or `nullopt` when filtered out.
     */
    std::optional<nlohmann::json> Wrap(const nlohmann::json& engine_event,
                                       const Viewer& viewer,
                                       EventSink& sink) const;

    /**
     * @brief Synthesize a `prompt_open` for the viewer's parked op input.
     *
     * The engine has no `prompt_open` descriptor yet (`kNeedsInput` parks the
     * pause); this is the seam that turns `PendingInput` into the
     * packet for the target player only. Returns `nullopt` for any other
     * viewer or when no input is parked.
     *
     * @param viewer Recipient to build for.
     * @param sink   Per-recipient sequence source.
     * @return The `prompt_open` envelope, or `nullopt`.
     */
    std::optional<nlohmann::json> BuildPendingPrompt(
        const Viewer& viewer, EventSink& sink) const;

    /**
     * @brief Reconnect snapshot for `viewer`.
     *
     * The `match_state_updated` reconnect truth, built through the same view
     * builder as the event stream: match state (piles, turn/direction/active
     * type, placements/winner), window state, per-player statuses, the
     * viewer's open prompts and the `seq` watermark. Base visibility follows
     * without grants (own hand full identity, others count only, draw
     * pile count only, discard top identity + count, played/public info);
     * `VisibilityGrant` entries reveal the granted aspects of another hand or
     * a pile. A spectator is omniscient except for players opted into
     * `privacy_from_spectators`. Own-hand entries carry a `can_play` flag
     *
     * The watermark is `sink.NextSeq()`, so a caller builds the viewer's
     * packets into `sink` first and then the snapshot reconciles exactly the
     * stream it has sent.
     *
     * @param viewer Recipient to build for.
     * @param sink   The viewer's stream, read for the seq watermark.
     * @return The `match_state_updated` envelope.
     */
    nlohmann::json BuildSnapshot(const Viewer& viewer,
                                 const EventSink& sink) const;

    /**
     * @brief Reconnect snapshot with per-player spectator privacy.
     *
     * @param viewer  Recipient to build for.
     * @param sink    The viewer's stream, read for the seq watermark.
     * @param options Lobby-owned snapshot options (`privacy_from_spectators`).
     * @return The `match_state_updated` envelope.
     */
    nlohmann::json BuildSnapshot(const Viewer& viewer, const EventSink& sink,
                                 const SnapshotOptions& options) const;

    /**
     * @brief True when `status_id` is declared `hidden` by any loaded mod.
     */
    bool StatusHidden(const std::string& status_id) const;

    /**
     * @brief Audience token declared for signal `name`, or `all` when none.
     *
     * Declaration shape: `signals` is an array of
     * `{name, audience}`. Phase-1 audience vocabulary interpreted here:
     * `all` (default), `players` (seated viewers only), `spectators`
     * (spectator viewers only). Any other token is treated as `all`.
     */
    std::string SignalAudience(const std::string& name) const;

    /** @brief The bound engine. */
    const match::engine::MatchInstance& Match() const { return match_; }

private:
    const match::engine::MatchInstance& match_;
    /** status_id -> declared `hidden`. */
    std::unordered_map<std::string, bool> hidden_status_;
    /** signal name -> declared `audience` token. */
    std::unordered_map<std::string, std::string> signal_audience_;
};

}  // namespace match::view
