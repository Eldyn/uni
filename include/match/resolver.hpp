#pragma once

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/ecs/event_bus.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/vocabulary.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @file resolver.hpp
 * @brief Behavior-graph walker: selectors, ops, windows, schedules, budgets.
 *
 * The Resolver is the runtime half. It walks a
 * `match::modload::BehaviorGraph` from its entry node, resolves the
 * context-selector DSL into entities, invokes ops through `match::ops`, and
 * routes to `next` nodes. It owns NO timers, RNG or wall-clock: a `window`
 * node pauses and returns a structured `WindowRequest` (the timer layer and the
 * engine open and close it), a `schedule` node returns a `ScheduleRequest`, and
 * an op that returns `kNeedsInput` pauses carrying its `InputRequest`.
 *
 * Resolution is deterministic: node routing follows declared order and forks
 * run their branches in declared order. Guards are env-tunable via
 * `ResolverConfig::FromEnv()`.
 */

namespace match::engine {
struct MatchRegistries;
}  // namespace match::engine

namespace match::resolver {

/**
 * @struct ResolverConfig
 * @brief Runtime guard thresholds, captured once at construction.
 */
struct ResolverConfig {
    uint64_t chain_budget = 10000;  /**< `UNI_CHAIN_BUDGET` ops per chain. */
    uint64_t event_budget = 100000; /**< `UNI_EVENT_BUDGET` per match. */
    uint32_t must_apply_cap = 4;    /**< must-apply re-trigger depth cap. */

    /** @brief Read every guard from the environment with spec defaults. */
    static ResolverConfig FromEnv();
};

/**
 * @struct SelectorContext
 * @brief Seed entities and context flags for selector resolution.
 *
 * `in_window` and `in_card_context` are runtime guards: `@responder` only
 * resolves inside a window route and `@card` only in card-behavior context.
 * Using either outside its context records a guard violation and binds
 * nothing (never trusts load-time validation).
 */
struct SelectorContext {
    std::optional<ecs::Entity> self;      /**< `@self`. */
    std::optional<ecs::Entity> target;    /**< `@target`. */
    std::optional<ecs::Entity> responder; /**< `@responder` (window only). */
    std::optional<ecs::Entity> card;      /**< `@card` (card context only). */
    bool in_window = false;               /**< `@responder` is legal. */
    bool in_card_context = false;         /**< `@card` is legal. */
};

/**
 * @enum ResolveStatus
 * @brief Terminal status of one Resolve / Resume call.
 */
enum class ResolveStatus {
    kComplete,    /**< graph drained, no pause. */
    kNeedsInput,  /**< paused on an op input request. */
    kWindow,      /**< paused at a window node. */
    kSchedule,    /**< paused at a schedule node. */
    kAborted,     /**< chain budget breached; remaining nodes skipped. */
    kError,       /**< structural or op error; chain stopped. */
};

/**
 * @struct ResumeToken
 * @brief Explicit continuation for a paused resolution.
 *
 * `node` is where the next call continues. `prompt`, when set, is the op node
 * id an injected input value is bound to before continuing. A schedule's
 * `node` starts a fresh chain when its duration elapses.
 */
struct ResumeToken {
    std::string node;    /**< immediate node id to continue at. */
    std::string prompt;  /**< bind injected input to this op node id. */
    /**
     * Kind of the prompt that produced this pause, when known. The Resolver
     * uses it on resume to bind the answered value to the matching context
     * selector (a `choose_player` answer binds `@choose_player`) so an op's
     * selector arg can consume the choice.
     */
    std::string prompt_kind;
    /**
     * Work stack remaining after `node` drains (back = next to run). This
     * preserves a fork's not-yet-run branches and its `next` across a pause
     * .
     */
    std::vector<std::string> pending;
};

/**
 * @struct WindowRequest
 * @brief Structured "open a response window" request.
 *
 * The caller opens the window, waits for a response or timeout, then
 * picks a route: a value from `on_response`, or `default_route` on timeout,
 * and calls `ResumeWindow` with that node id.
 */
struct WindowRequest {
    std::string node_id;            /**< window node id. */
    std::string responders_selector; /**< raw selector token. */
    std::vector<ecs::Entity> responders; /**< resolved responder set. */
    nlohmann::json respond_with = nlohmann::json::object(); /**< filter. */
    std::string filter_digest;      /**< optional filter digest key. */
    std::string default_route;      /**< timeout / default node id. */
    std::string resume_node;        /**< fallback continuation node id. */
    std::map<std::string, std::string, std::less<>> on_response;
    std::string duration;           /**< `env` or a duration token. */
    /** Integer `duration` in ms; absent for `env` or an invalid value. */
    std::optional<int64_t> duration_ms;
    /** Presentation tag (`^[a-z0-9_:.-]{1,32}$`); `generic` when unset. */
    std::string kind = "generic";
    nlohmann::json raw = nlohmann::json::object(); /**< verbatim node. */
};

/**
 * @struct ScheduleRequest
 * @brief Structured "defer the next subgraph" request.
 */
struct ScheduleRequest {
    std::string node_id;       /**< schedule node id. */
    nlohmann::json duration;   /**< duration spec object/array. */
    std::string resume_node;   /**< node id to run once elapsed. */
    /**
     * Work stack remaining after the deferred subgraph (back = next). Pass it
     * back to `Resolve` so a pause inside a fork does not drop the fork's
     * remaining branches or its `next`.
     */
    std::vector<std::string> pending;
};

/**
 * @struct ResolveResult
 * @brief Outcome of one Resolve / Resume call.
 */
struct ResolveResult {
    ResolveStatus status = ResolveStatus::kComplete;
    std::string terminal_node;   /**< last node visited. */
    std::optional<ops::InputRequest> input_request; /**< kNeedsInput body. */
    std::optional<WindowRequest> window;            /**< kWindow body. */
    std::optional<ScheduleRequest> schedule;        /**< kSchedule body. */
    std::optional<ResumeToken> resume;              /**< continuation. */
    std::vector<nlohmann::json> events;  /**< collected event descriptors. */
    std::vector<nlohmann::json> effects; /**< collected effect descriptors. */
    std::vector<std::string> guard_violations; /**< selector guard hits. */
    std::string error;           /**< set when status is kError. */
    bool aborted = false;        /**< chain budget breach. */
    bool disarmed = false;       /**< mod disarmed by this abort. */
    bool skipped_disarmed = false; /**< mod already disarmed; nothing ran. */
    std::string aborted_mod;     /**< mod the abort is attributed to. */
    std::string aborted_node;    /**< node the abort occurred at. */
    bool must_apply = false;     /**< must-apply semantics were applied. */
};

/**
 * @brief Condition evaluator: `(store, condition, ctx) -> bool`.
 *
 * The op layer owns the real bodies. This is the seam tests use to inject
 * a synthetic condition; the default for an unregistered keyword is false.
 */
using ConditionFn = std::function<bool(ecs::EntityStore&,
                                       const nlohmann::json&,
                                       ops::OpContext&)>;

/**
 * @class ConditionRegistry
 * @brief Keyword -> evaluator registry for `branch` cases.
 *
 * Accepts a condition as a boolean literal, a single-key object
 * `{ "<keyword>": { ...args } }`, or an object carrying `keyword` /
 * `condition` / `op` with an `args` body. An unregistered keyword logs and
 * evaluates false; the op layer registers the real bodies.
 */
class ConditionRegistry {
public:
    /** @brief Register (or replace) an evaluator; false on empty/null. */
    bool Register(const std::string& keyword, ConditionFn fn);

    /** @brief True when `keyword` has a registered evaluator. */
    bool Has(const std::string& keyword) const;

    /** @brief Evaluate `condition`; unregistered keywords are false. */
    bool Evaluate(ecs::EntityStore& store, const nlohmann::json& condition,
                  ops::OpContext& ctx) const;

private:
    std::map<std::string, ConditionFn, std::less<>> evaluators_;
};

/**
 * @class Resolver
 * @brief Walks behavior graphs against the ECS store.
 */
class Resolver {
public:
    /**
     * @brief Bind the resolver to its runtime collaborators.
     *
     * @param store       entity store the ops mutate.
     * @param runtime     op registry (const; the resolver only invokes).
     * @param event_bus   hook bus ops may dispatch through.
     * @param budgets     the match's the store `BudgetLedger`
     * (chain/event/must).
     * @param conditions  condition evaluator registry (branch cases).
     * @param config      guard thresholds; defaults to `FromEnv()`.
     */
    Resolver(ecs::EntityStore& store, const ops::OpRuntime& runtime,
             ecs::EventBus& event_bus, ecs::BudgetLedger& budgets,
             ConditionRegistry& conditions,
             ResolverConfig config = ResolverConfig::FromEnv());

    /**
     * @brief Start a fresh resolution chain at `entry_node`.
     *
     * Resets `budgets.chain_steps` (a new chain) and binds the standard
     * selectors into `frame`. `entry_node` empty means the graph's first node.
     * A schedule resume re-enters here with `entry_node = resume_node` and
     * `pending = schedule.pending`.
     *
     * @param must_apply request must-apply semantics; when the
     *        re-trigger depth cap is hit the call proceeds as non-must-apply
     *        and logs a WARN.
     * @param pending work stack to run after `entry_node` (back = next); use
     *        the schedule request's `pending` when resuming a deferred fork.
     */
    ResolveResult Resolve(const modload::BehaviorGraph& graph,
                          const std::string& mod_id,
                          const SelectorContext& context,
                          ops::ResolutionFrame& frame,
                          const std::string& entry_node = std::string(),
                          bool must_apply = false,
                          std::vector<std::string> pending = {});

    /**
     * @brief Resume a `kWindow` pause at the caller-chosen route node.
     *
     * Does NOT reset the chain budget: a window chain shares one ledger. The
     * context is treated as a window route (`@responder` legal). Resumes with
     * the pause's `pending` work stack, so a fork's remaining branches and its
     * `next` still run after the chosen route.
     */
    ResolveResult ResumeWindow(const modload::BehaviorGraph& graph,
                               const std::string& mod_id,
                               const SelectorContext& context,
                               ops::ResolutionFrame& frame,
                               const ResolveResult& pause,
                               const std::string& route_node);

    /**
     * @brief Resume a `kNeedsInput` pause with an injected value.
     *
     * Binds `value` to the paused op node id in `frame.prompt_values` so a
     * later `from_prompt` arg can read it, then continues at the op's `next`
     * followed by the pause's `pending` work stack.
     */
    ResolveResult ResumeInput(const modload::BehaviorGraph& graph,
                              const std::string& mod_id,
                              const SelectorContext& context,
                              ops::ResolutionFrame& frame,
                              const ResolveResult& pause,
                              nlohmann::json value);

    /** @brief Effective guard configuration. */
    const ResolverConfig& Config() const { return config_; }

    /**
     * @brief Provide the assembly's frozen card index map.
     *
     * Every `OpContext` this resolver builds carries the pointer so a
     * card-prompt op can resolve a candidate entity to its `CompactCardV2`
     * bits. Additive: the map stays owned by the assembly, which outlives the
     * resolver.
     */
    void SetRegistries(const engine::MatchRegistries* registries) {
        registries_ = registries;
    }

    /**
     * @brief The seat after `anchor` in the current play direction, the same
     *        step `@next_player` takes from the current player.
     */
    std::optional<ecs::Entity> NextInTurnOrder(ecs::Entity anchor) const;

private:
    enum class WalkCode {
        kContinue,  /**< continuations pushed; keep walking. */
        kPause,     /**< result already carries a pause. */
        kAbort,     /**< chain budget breached. */
        kError,     /**< structural or op error. */
    };

    struct WalkState {
        const modload::BehaviorGraph* graph = nullptr;
        std::string mod_id;
        SelectorContext context;
        ops::ResolutionFrame* frame = nullptr;
        ResolveResult* result = nullptr;
        std::map<std::string, const nlohmann::json*, std::less<>> nodes_by_id;
    };

    WalkState MakeState(const modload::BehaviorGraph& graph,
                        const std::string& mod_id,
                        const SelectorContext& context,
                        ops::ResolutionFrame& frame, ResolveResult& result);

    /** @brief Build the initial work stack (pending then `entry`). */
    std::vector<std::string> InitialStack(
        const modload::BehaviorGraph& graph, const std::string& entry,
        const std::vector<std::string>& pending) const;

    ResolveStatus Walk(WalkState& state, std::vector<std::string> stack);

    WalkCode StepOp(WalkState& state, const nlohmann::json& node,
                    std::vector<std::string>& stack);
    WalkCode StepBranch(WalkState& state, const nlohmann::json& node,
                        std::vector<std::string>& stack);
    WalkCode StepFork(WalkState& state, const nlohmann::json& node,
                      std::vector<std::string>& stack);
    WalkCode StepWindow(WalkState& state, const nlohmann::json& node,
                        std::vector<std::string>& stack);
    WalkCode StepSchedule(WalkState& state, const nlohmann::json& node,
                          std::vector<std::string>& stack);

    ResolveStatus Abort(WalkState& state, const std::string& node);

    void BindStandardSelectors(WalkState& state);
    void BindSelectorArgs(WalkState& state, ops::OpArgs& args);
    std::vector<ecs::Entity> ResolveSelector(WalkState& state,
                                             const std::string& token);

    /**
     * @brief True when an op's selector args name `@choose_player` unbound.
     *
     * The `@choose_player` sugar expands to a `choose_player`
     * prompt; when a graph names the selector without first opening that
     * prompt, the Resolver parks the prompt here and re-enters the same op
     * once an answer binds the selector. Returns false once
     * `@choose_player` is bound, including on the re-entered pass.
     */
    bool NeedsChoosePlayerPrompt(WalkState& state, const ops::OpArgs& args) const;

    /** @brief Usernames the `@choose_player` prompt offers `asker` (others). */
    nlohmann::json ChoosePlayerOptions(const ecs::Entity& asker) const;

    /** @brief Resolve a `choose_player` answer username to a live player. */
    std::optional<ecs::Entity> FindPlayerByUsername(
        const std::string& username) const;

    std::vector<ecs::Entity> PlayersBySeat() const;
    std::optional<ecs::Entity> FindCurrentPlayer() const;
    std::optional<ecs::Entity> FindMatch() const;
    std::optional<ecs::Entity> FindPile(ecs::PileKind kind) const;
    std::optional<ecs::Entity> Neighbor(const SelectorContext& context,
                                        int step) const;
    std::optional<ecs::Entity> SeatStep(ecs::Entity anchor, int step) const;
    int DirectionStep() const;

    void AppendEvent(WalkState& state, const nlohmann::json& event);
    static bool IsDroppableEvent(const nlohmann::json& event);

    void Guard(WalkState& state, const std::string& token,
               const std::string& reason);

    bool ApplyMustApply(ResolveResult& result, bool requested);
    void ReleaseMustApply(bool applied);

    ecs::EntityStore& store_;
    const ops::OpRuntime& runtime_;
    ecs::EventBus& event_bus_;
    ecs::BudgetLedger& budgets_;
    ConditionRegistry& conditions_;
    ResolverConfig config_;
    const engine::MatchRegistries* registries_ = nullptr;
};

}  // namespace match::resolver
