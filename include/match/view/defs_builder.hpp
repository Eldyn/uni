#pragma once

#include <match/modload/artifacts.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

/**
 * @file defs_builder.hpp
 * @brief `defs` + `match_start` packets.
 *
 * Content-derived, identical for every recipient, built once from the frozen
 * `MatchRegistries` plus the loaded mods (faces and tags are not retained on
 * the assembly). The `defs_digest` is an FNV-1a hash over the frozen mod list
 * and kind table so a client can detect a stale face cache.
 */

namespace match::engine {
struct MatchRegistries;
}  // namespace match::engine

namespace match::view {

/**
 * @class DefsBuilder
 * @brief Builds the `defs` kind table and the `match_start` packet.
 */
class DefsBuilder {
public:
    /**
     * @brief Build the `defs` packet from frozen registries.
     *
     * Shape: `{defs_digest, mods: [{id, version, index}],
     * kinds: [{index, string_id, face, tags}]}`. `mods` follows the frozen
     * priority order; `kinds` is grouped by mod in that order and each kind's
     * `index` is its `kind_index` within its owning mod. The mod a
     * kind belongs to is the namespace prefix of `string_id`.
     *
     * @param registries Frozen match registries (mod list + kind table).
     * @param mods       Loaded mods, for each kind's face and tags.
     * @return The `defs` payload, including its digest.
     */
    static nlohmann::json Build(
        const match::engine::MatchRegistries& registries,
        const std::vector<match::modload::LoadedMod>& mods);

    /**
     * @brief Recompute the FNV-1a digest a `defs` payload advertises.
     *
     * Digests the `mods` + `kinds` subtrees only, so setting or clearing
     * `defs_digest` does not change the result.
     *
     * @param defs_payload A payload produced by `Build`.
     * @return 16-character lowercase hex digest, or empty on a bad shape.
     */
    static std::string Digest(const nlohmann::json& defs_payload);

    /**
     * @brief Build the `match_start` packet.
     *
     * Shape: `{defs_digest, mods, deck_id, deck_name, settings}`. Mods and the
     * digest are reused verbatim from `Build` so a client can correlate the
     * two packets. `deck_id` / `deck_name` / `settings` are caller-supplied
     * because the assembly does not retain the deck snapshot yet (see the
     * `FIXME` in `match_instance.cpp`).
     *
     * @param registries Frozen match registries.
     * @param mods       Loaded mods (for the shared kind table digest).
     * @param deck_id    Deck `namespace:id`, or empty.
     * @param deck_name  Human-readable deck name, or empty.
     * @param settings   Lobby settings snapshot (defaults to `{}`).
     * @return The `match_start` payload.
     */
    static nlohmann::json BuildMatchStart(
        const match::engine::MatchRegistries& registries,
        const std::vector<match::modload::LoadedMod>& mods,
        const std::string& deck_id, const std::string& deck_name,
        const nlohmann::json& settings = nlohmann::json::object());
};

}  // namespace match::view
