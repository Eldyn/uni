#pragma once

#include <array>
#include <cstddef>
#include <string_view>

/**
 * @file binding_definition_keys.hpp
 * @brief The key names and the closed word lists of a presentation binding
 *        file.
 *
 * ONE table. The loader, the validator, the defs builder, and the tests read
 * the names from here. The JSON schema
 * `contract/schemas/bindings.schema.json` repeats the names; a test fails when
 * the schema and this table differ.
 *
 * DESIGN DECISION (ticket 16): the mod graphics programme already ships a
 * presentation kind in `presentations/<id>.json`. It binds a modifier list to a
 * HOOK or a trigger name (an affliction on a status event). That kind has no
 * event id, no matcher, and no output other than modifiers. A binding is a
 * second, new kind in version 1 style. It lives in the same directory
 * `presentations/`, because a modder looks there for presentations. The top
 * level key `uni:binding` tells the two kinds apart. The old kind stays
 * unchanged. The new kind needs no change of the old client code: the `defs`
 * packet lists both kinds in `presentations[]`, and the affliction store
 * skips an entry that has no `target`.
 *
 * A binding names ONE event id (`uni:event`). A core event is named by its
 * wire type (`turn_skipped`); a mod event by its full id (`my_mod:apple_eaten`).
 * A binding has up to `kMaxCases` cases. Each case has a list of tests and a
 * `uni:present` block. The client runs ALL cases whose tests are all true, in
 * file order. The tests are the matcher of the streak kind (`equals`, `in`,
 * `exists`, `has_tag`) plus `relation`, which only a binding may use:
 * `{ "uni:test": "relation", "uni:role": "actor", "uni:relation": "self" }`.
 * The role `actor` is the envelope actor. The role `target` is the payload
 * field `target`. `self` means that the viewer holds that seat. `other` means
 * that the seat exists and the viewer does not hold it.
 *
 * A `uni:present` block can add:
 *
 * - `uni:beats`: animation beats. A beat is a list of steps. A step has the
 *   same keys as an animation IR step (`op`, `target`, `payload`, `atS`,
 *   `groupId`). The `op` must be in `kBeatOps`. A `target` that starts with
 *   `$payload.` names a payload field; the client replaces it with the value.
 * - `uni:sounds`: sounds. `uni:sound` is a core sound id (`kCoreSoundIds`) or
 *   a value of a `sound` slot in an asset bundle of the same mod.
 * - `uni:log`: a play log line. `uni:variants` lists text KEYS; the client
 *   picks one with the FNV-1a hash over the event `sequence` (the same picker
 *   as the reaction pools). `uni:params` maps a parameter name to its source:
 *   `{ "uni:field": "count" }`, `{ "uni:value": "x" }`, or
 *   `{ "uni:actor": true }`. A param can add `uni:default` (string or
 *   number). The client uses it when the source gives no value. It is not a
 *   source.
 * - A test of `exists`, `equals` or `in` can set `uni:actor: true`. It then
 *   reads the envelope actor, not a payload field or a streak.
 * - `uni:glossary`: a keyword key for `[k=...]`.
 *
 * WARN: the matcher and the parameters run on the CLIENT, on the event as the
 * server PROJECTED it for that viewer. A field with the audience `none` never
 * reaches a client, so a binding cannot read it (error). A field with the
 * audience `actor`, `target`, or `owner` is absent for some viewers (warning).
 * The binding file itself is static and public: do not put a secret in it.
 *
 * INFO: texts. Ticket 20 builds the mod text layer. Until then a log line
 * carries KEYS and parameters only. The client looks the key up in the
 * Paraglide log text layer and shows the raw key when no text exists.
 *
 * Example file (format version 1):
 *
 *     {
 *       "uni:format_version": "1.0.0",
 *       "uni:binding": {
 *         "uni:description": { "uni:identifier": "my_mod:apple_shown" },
 *         "uni:event": "my_mod:apple_eaten",
 *         "uni:cases": [ {
 *           "uni:tests": [ { "uni:test": "relation", "uni:role": "actor",
 *                            "uni:relation": "self" } ],
 *           "uni:present": {
 *             "uni:beats": [ [ { "op": "shake", "target": "screen" } ] ],
 *             "uni:sounds": [ { "uni:sound": "sfx.ui.tick" } ],
 *             "uni:log": { "uni:variants": [ "my_log_you_ate" ],
 *                          "uni:params": {
 *                            "count": { "uni:field": "count" },
 *                            "run": { "uni:streak": "uni:skip_run" } } },
 *             "uni:glossary": "my_mod:apple"
 *           } } ]
 *       }
 *     }
 */

namespace match::modload::binding_keys {

inline constexpr std::string_view kFormatVersion = "uni:format_version";
inline constexpr std::string_view kBinding = "uni:binding";
inline constexpr std::string_view kDescription = "uni:description";
inline constexpr std::string_view kIdentifier = "uni:identifier";
inline constexpr std::string_view kEvent = "uni:event";
inline constexpr std::string_view kCases = "uni:cases";
inline constexpr std::string_view kTests = "uni:tests";
inline constexpr std::string_view kPresent = "uni:present";

inline constexpr std::string_view kBeats = "uni:beats";
inline constexpr std::string_view kSounds = "uni:sounds";
inline constexpr std::string_view kSound = "uni:sound";
inline constexpr std::string_view kVolume = "uni:volume";
inline constexpr std::string_view kPitch = "uni:pitch";
inline constexpr std::string_view kLog = "uni:log";
inline constexpr std::string_view kVariants = "uni:variants";
inline constexpr std::string_view kParams = "uni:params";
inline constexpr std::string_view kGlossary = "uni:glossary";

inline constexpr std::string_view kParamField = "uni:field";
inline constexpr std::string_view kParamValue = "uni:value";
inline constexpr std::string_view kParamActor = "uni:actor";
inline constexpr std::string_view kParamStreak = "uni:streak";
inline constexpr std::string_view kParamKey = "uni:key";
inline constexpr std::string_view kParamKeyField = "uni:key_field";
inline constexpr std::string_view kParamDefault = "uni:default";
inline constexpr std::string_view kSelect = "uni:select";
inline constexpr std::string_view kSelectAll = "all";
inline constexpr std::string_view kSelectFirst = "first";

inline constexpr std::string_view kGt = "gt";
inline constexpr std::string_view kGte = "gte";
inline constexpr std::string_view kLt = "lt";
inline constexpr std::string_view kLte = "lte";

/** @brief Test words of a binding: the matcher words plus numeric compares. */
inline constexpr std::array<std::string_view, 9> kBindingTests = {
    "equals", "in", "exists", "has_tag", "relation", kGt, kGte, kLt, kLte};

inline constexpr std::string_view kRole = "uni:role";
inline constexpr std::string_view kActorRole = "actor";
inline constexpr std::string_view kTargetRole = "target";
inline constexpr std::string_view kSelf = "self";
inline constexpr std::string_view kOther = "other";

/** @brief The prefix of a step target that names a payload field. */
inline constexpr std::string_view kPayloadTargetPrefix = "$payload.";

inline constexpr std::size_t kMaxCases = 8;
inline constexpr std::size_t kMaxBeats = 8;
inline constexpr std::size_t kMaxStepsPerBeat = 16;
inline constexpr std::size_t kMaxSounds = 4;
inline constexpr std::size_t kMaxVariants = 8;
inline constexpr std::size_t kMaxParams = 8;

/**
 * @brief The animation IR ops of the client (the renderer registry of
 *        `cardRegistry.svelte.ts`). A test compares this list with that file.
 */
inline constexpr std::array<std::string_view, 12> kBeatOps = {
    "move",        "flip",          "shake",        "screenEffect",
    "materialEffect", "impact",     "spawnEntity",  "despawnEntity",
    "setEntityState", "attachTo",   "emitParticles", "emitSound"};

/**
 * @brief The sound ids of the client catalog `SFX_CATALOG`
 *        (`audioCatalogs.ts`). A test compares this list with that file.
 */
inline constexpr std::array<std::string_view, 13> kCoreSoundIds = {
    "sfx.turn.start",   "sfx.action.play-card", "sfx.action.draw-card",
    "sfx.deal",         "sfx.invalid",          "sfx.ui.tick",
    "sfx.ui.untick",    "sfx.table.reverse",    "sfx.table.mat-color",
    "sfx.hands.swap",   "sfx.hands.pass",       "sfx.match.victory",
    "sfx.match.defeat"};

}  // namespace match::modload::binding_keys
