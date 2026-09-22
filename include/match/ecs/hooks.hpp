#pragma once

#include <match/modload/vocabulary.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

/**
 * @file hooks.hpp
 * @brief Hook identity, mutable payload, and the payload-key contract.
 *
 * A hook is the unit of interception a mod system subscribes to.
 * Every hook has a `before` and an `after` phase; the bare-name universe is
 * owned by the validator's `match::modload::HookCatalog()` and is consumed here
 * through `ResolveHook`, never re-listed.
 *
 * `HookPayload::data` is the mutable event body passed to every callback of a
 * dispatch. Keys are JSON and follow the payload essentials:
 *
 * - `match_start` / `match_end`        : `settings` (object snapshot).
 * - `round_start` / `round_end`        : `round` (int).
 * - `turn_start` / `turn_end`          : `player` (entity handle).
 * - `play`                             : `card`, `player`, `from` (zone).
 * - `draw_attempt`                     : `player`, `source` (pile ref).
 * - `draw`                             : `card`, `player`.
 * - `shuffle`                          : `draw_size`, `discard_size` (ints).
 * - `pile_empty`                       : `pile` (pile ref).
 * - `hand_empty`                       : `player` (entity handle).
 * - `win_check`                        : `player` (entity handle or null).
 * - `status_applied` / `status_removed`: `target`, `status` (instance).
 * - `window_open` / `window_close`     : `window` (object).
 * - `effect_applied`                   : `op`, `args` (summary), `targets`.
 * - `card_entered_zone` / `card_left_zone` : `card`, `from`, `to` (zones).
 * - `roll`                             : `spec`, `outcome`.
 * - `visibility_granted` / `visibility_revoked`
 *                                      : `viewer`, `target`, `aspects`.
 *
 * Veto-capable before-hooks: `turn_end`, `play`, `draw_attempt`
 * `draw`, `pile_empty`, `hand_empty`, `win_check`. Every other hook ignores
 * the `veto` flag. `HookPayload::veto` is only meaningful to
 * `EventBus::DispatchBefore`; the engine default is cancelled when it is set
 * after every before-hook has run.
 */

namespace match::ecs {

/**
 * @enum HookPhase
 * @brief Which side of the engine default a hook runs on.
 *
 * `kBefore` runs ahead of the default handling and may mutate the payload or
 * veto; `kAfter` observes the settled state.
 */
enum class HookPhase {
    kBefore,
    kAfter,
};

/** @brief `before` / `after` token for a phase (inverse of `ResolveHook`). */
inline std::string_view HookPhaseToken(HookPhase phase) {
    return phase == HookPhase::kBefore ? "before" : "after";
}

/**
 * @struct HookId
 * @brief A bare catalog hook name plus its phase.
 *
 * Equality and ordering are total so `HookId` can key the re-entry depth map
 * without a parallel string hash.
 */
struct HookId {
    std::string name;                        /**< bare name, e.g. "play". */
    HookPhase phase = HookPhase::kBefore;    /**< before / after variant. */

    bool operator==(const HookId&) const = default;

    bool operator<(const HookId& other) const {
        if (name != other.name) return name < other.name;
        return static_cast<int>(phase) < static_cast<int>(other.phase);
    }
};

/**
 * @struct HookPayload
 * @brief Mutable event body handed to every callback of one dispatch.
 *
 * A before-hook may rewrite `data` in place; the engine reads the final
 * `data` once all hooks have run. A veto is a request to cancel the engine
 * default only — it does not stop sibling hooks.
 */
struct HookPayload {
    HookId hook;            /**< identity being dispatched. */
    nlohmann::json data = nlohmann::json::object();  /**< mutable body. */
    bool veto = false;      /**< before only: cancel the engine default. */
};

/**
 * @brief True when `hook` names a catalog hook at a legal phase.
 *
 * Consumes `match::modload::ResolveHook`; a bare catalog name is legal at
 * either phase, while an explicit `before:` / `after:` token only matches its
 * own phase. Unknown names are false.
 */
inline bool IsKnownHook(const HookId& hook) {
    std::string name;
    std::string phase;
    if (!match::modload::ResolveHook(hook.name, name, phase)) return false;
    if (phase.empty()) return true;
    return phase == HookPhaseToken(hook.phase);
}

/**
 * @brief True when a before-hook may veto the engine default.
 *
 * The Veto column marks exactly these hooks veto-capable at their
 * `before` variant: `turn_end`, `play`, `draw_attempt`, `draw`, `pile_empty`,
 * `hand_empty`, `win_check`. No other hook may cancel the engine default; a
 * `veto` flag set on a non-vetoable hook is ignored by the EventBus.
 *
 * @return false for an unknown hook or an `after` phase.
 */
inline bool IsVetoCapable(const HookId& hook) {
    if (hook.phase != HookPhase::kBefore) return false;
    if (!match::modload::IsKnownHookName(hook.name)) return false;
    static const char* const kVetoCapable[] = {
        "turn_end", "play",      "draw_attempt", "draw",
        "pile_empty", "hand_empty", "win_check"};
    for (const char* name : kVetoCapable) {
        if (hook.name == name) return true;
    }
    return false;
}

/**
 * @brief Resolve a hook token (`play`, `before:play`, `on_play`) to a `HookId`.
 *
 * Mirrors `match::modload::ResolveHook` (so `on_play` is `after:play`). A bare
 * catalog name leaves the phase unspecified there; here it defaults to
 * `kBefore`, the phase a bare subscription is most often meant to intercept.
 *
 * @return nullopt when the token is not a known hook or phase.
 */
inline std::optional<HookId> ResolveHookId(const std::string& token) {
    std::string name;
    std::string phase;
    if (!match::modload::ResolveHook(token, name, phase)) return std::nullopt;
    HookId hook;
    hook.name = name;
    hook.phase = (phase == "after") ? HookPhase::kAfter : HookPhase::kBefore;
    return hook;
}

}  // namespace match::ecs
