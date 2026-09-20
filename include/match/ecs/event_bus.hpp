#pragma once

#include <match/ecs/hooks.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

/**
 * @file event_bus.hpp
 * @brief Deterministic hook dispatch: mod systems subscribe, events fan out.
 *
 * The bus is the runtime half. Dispatch order is a frozen
 * contract: the match's mod-list order first, then each mod's registration
 * index within that mod. Before-hooks all run (they may mutate the payload);
 * a veto is only inspected after every before-hook has run, and cancels the
 * engine default for that event. After-hooks observe the settled state.
 *
 * Runtime guards live here for the hook path: per-hook re-entry
 * depth is capped by `UNI_REENTRY_CAP` (default 8) and a chain abort is
 * attributed to the executing mod; at `UNI_MOD_DISARM_THRESHOLD` (default 3)
 * that mod's systems are neutralized for the match.
 *
 * The bus owns no timers, RNG or match state. Callbacks are opaque
 * a real mod system later walks a behavior graph through the Resolver, tests
 * pass lambdas.
 */

namespace match::ecs {

/**
 * @brief Callback a mod system runs for one subscribed hook.
 *
 * Receives the mutable payload; may rewrite `data` and, for a before-hook, set
 * `veto`. Never owns the payload.
 */
using HookCallback = std::function<void(HookPayload&)>;

/**
 * @struct HookDispatchResult
 * @brief Outcome of one `DispatchBefore` / `DispatchAfter` call.
 */
struct HookDispatchResult {
    uint32_t ran = 0;      /**< callbacks invoked (skips disarmed mods). */
    bool vetoed = false;   /**< before: veto on a veto-capable hook. */
    bool aborted = false;  /**< re-entry cap breached; no hooks ran. */
    std::string aborted_mod;  /**< mod attributed the abort (may be empty). */
    std::string disarmed_mod; /**< set on the dispatch that disarmed a mod. */
};

/**
 * @class EventBus
 * @brief Ordered hook registry plus before/after dispatch.
 */
class EventBus {
public:
    /** Empty mod order; subscriptions outside it sort last (still stable). */
    EventBus();

    /**
     * @brief Build a bus over the frozen match mod list (priority order).
     * @param mod_order Mod ids in dispatch order; unknown ids sort last.
     */
    explicit EventBus(std::vector<std::string> mod_order);

    /**
     * @brief Replace the frozen mod order.
     *
     * The engine calls this once at assembly. Calling it after subscriptions is
     * legal but reorders subsequent dispatch; the order is always the current
     * one.
     */
    void SetModOrder(std::vector<std::string> mod_order);

    /**
     * @brief Register one mod system callback for a hook.
     *
     * @param mod_id             owner mod id (dispatch priority).
     * @param registration_index caller-assigned monotonic index within the mod.
     * @param hook               hook identity (name + phase).
     * @param callback           opaque system callback.
     * @return false on an empty mod id or empty callback (structured miss).
     */
    bool Subscribe(const std::string& mod_id, uint32_t registration_index,
                   const HookId& hook, HookCallback callback);

    /**
     * @brief Run every before-hook for `hook`, in frozen order.
     *
     * The method's phase is authoritative: only `kBefore` subscriptions whose
     * name matches `hook.name` run, and `payload.hook` is normalized to the
     * resolved identity. `payload.veto` is cleared before the hooks run. Every
     * before-hook runs even when one sets `veto`; the flag is read after all
     * have run. `vetoed = true` only when `IsVetoCapable(hook)` and the final
     * flag is set — a veto on a non-vetoable hook never cancels the default.
     * On re-entry-cap breach no hook runs and `aborted` is set.
     */
    HookDispatchResult DispatchBefore(const HookId& hook, HookPayload& payload);

    /** @brief Run every after-hook for `hook`, in frozen order (see above). */
    HookDispatchResult DispatchAfter(const HookId& hook, HookPayload& payload);

    /** @brief True when a mod has been disarmed for the match. */
    bool IsDisarmed(const std::string& mod_id) const;

    /** @brief Chain aborts attributed to `mod_id` so far. */
    uint32_t AbortCount(const std::string& mod_id) const;

    /**
     * @brief Record a chain abort attributed to `mod_id` outside hook dispatch.
     *
     * The Resolver uses this for chain-budget breaches so they count
     * toward the same per-mod disarm threshold as hook re-entry aborts
     * An empty `mod_id` is ignored.
     *
     * @return true when this abort newly disarmed `mod_id`.
     */
    bool NoteChainAbort(const std::string& mod_id);

    /** @brief Live re-entry depth for `hook` (0 when not executing). */
    uint32_t CurrentReentryDepth(const HookId& hook) const;

    /** @brief Effective `UNI_REENTRY_CAP` captured at construction. */
    uint32_t ReentryCap() const { return reentry_cap_; }

    /** @brief Effective `UNI_MOD_DISARM_THRESHOLD` captured at construction. */
    uint32_t DisarmThreshold() const { return disarm_threshold_; }

private:
    struct Subscription {
        std::string mod_id;
        uint32_t registration_index = 0;
        HookId hook;
        HookCallback callback;
        uint64_t seq = 0;  /**< subscribe call order, final tie-break. */
    };

    HookDispatchResult Dispatch(const HookId& hook, HookPhase phase,
                                HookPayload& payload);

    /** @brief Index of `mod_id` in the frozen mod order, or +inf sort key. */
    std::size_t ModPosition(const std::string& mod_id) const;

    /** @brief First subscriber for `hook`, used when no mod executes. */
    std::string FirstSubscriberFor(const HookId& hook) const;

    /** @brief Currently executing mod (top of the system stack), or empty. */
    std::string CurrentMod() const;

    /** @brief Tally an abort for `mod_id` and disarm it at the threshold. */
    void AttributeAbort(const std::string& mod_id, HookDispatchResult& result);

    std::vector<std::string> mod_order_;
    std::vector<Subscription> subscriptions_;
    std::map<HookId, uint32_t> reentry_depth_;
    std::map<std::string, uint32_t> abort_counts_;
    std::set<std::string> disarmed_;
    std::vector<std::string> execution_stack_;
    uint32_t reentry_cap_ = 8;
    uint32_t disarm_threshold_ = 3;
    uint64_t next_seq_ = 0;
};

}  // namespace match::ecs
