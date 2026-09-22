#pragma once

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/ecs/event_bus.hpp>
#include <match/modload/vocabulary.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file ops.hpp
 * @brief The op seam: resolved args, invocation context, result, registry.
 *
 * Ops are total, bounded functions over the entity store:
 * `(store, args, ctx) -> effects + events`. The Resolver walks a
 * behavior graph, binds context selectors and `from_prompt` values, and calls
 * an op through `OpRuntime`. The op layer fills the real bodies; the Resolver
 * ships stubs only.
 *
 * Frozen seam:
 *
 * - `OpArgs` carries the node's raw `args` JSON plus the entities the Resolver
 *   already resolved for selector-typed args. Ops read args
 *   through the fail-safe getters, which consult the `OpSignature` /
 *   `ArgSpec` table in `match::modload`.
 * - `OpContext` carries the `EventBus`, the match budget ledger, the
 *   resolution frame (bound selectors + prompt results) and the one
 *   op-writable slot for requesting input.
 * - `OpResult` carries a status (`kResolved` / `kNeedsInput` / `kError`), an
 *   op value, emitted events, emitted effects and an error string.
 * - `OpFn` is the single function type every op uses.
 * - `OpRuntime` registers op bodies by `OpCatalog()` name. Production stubs are
 *   wired in exactly one place (`RegisterDefaultOps` / `MakeDefaultRuntime`).
 *
 * `call_original` is special: it is not a normal op body. It is
 * only meaningful inside a `wrap` mutation, where it invokes the wrapped
 * original behavior. It still has an `OpCatalog()` row and therefore still
 * receives a "not implemented" stub here; the op layer owns its real wrapping
 * body.
 */

namespace match::engine {
struct MatchRegistries;
}  // namespace match::engine

namespace match::ops {

/**
 * @enum OpStatus
 * @brief Outcome class of one op invocation.
 */
enum class OpStatus {
    kResolved,    /**< ran to completion; `value` holds the result. */
    kNeedsInput,  /**< paused on a prompt; `value` holds the prompt envelope. */
    kError,       /**< refused or failed; `error` explains why. */
};

/**
 * @struct InputRequest
 * @brief An op's request for external input.
 *
 * An op that needs a choice fills `OpContext::input_request` and returns
 * `OpStatus::kNeedsInput`. The Resolver pauses the graph and resumes it once
 * the caller supplies the chosen route/value.
 */
struct InputRequest {
    std::string kind;                 /**< prompt kind, e.g. choose_color. */
    std::optional<ecs::Entity> target;  /**< asked player, when applicable. */
    nlohmann::json payload = nlohmann::json::object();  /**< envelope body. */
};

/**
 * @struct ResolutionFrame
 * @brief Bound context selectors and prompt results for one resolution.
 *
 * The Resolver owns and populates the frame: `selectors` maps a spec
 * context token (`@self`, `@target`, `@others`, ...) to its bound
 * entities, and `prompt_values` maps a prompt node id to the result that a
 * later `from_prompt` arg reads. Ops only read; `OpContext::input_request` is
 * the one op-writable slot.
 */
struct ResolutionFrame {
    std::map<std::string, std::vector<ecs::Entity>, std::less<>>
        selectors;  /**< selector token -> bound entities. */
    std::map<std::string, nlohmann::json, std::less<>>
        prompt_values;  /**< prompt node id -> bound result. */

    /** @brief Bound entities for `token`, or nullptr when unbound. */
    const std::vector<ecs::Entity>* FindSelector(
        std::string_view token) const {
        auto it = selectors.find(std::string(token));
        return it == selectors.end() ? nullptr : &it->second;
    }

    /** @brief First bound entity for `token`, or nullopt when unbound/empty. */
    std::optional<ecs::Entity> FirstSelector(std::string_view token) const {
        const std::vector<ecs::Entity>* found = FindSelector(token);
        if (found == nullptr || found->empty()) return std::nullopt;
        return found->front();
    }

    /** @brief Result bound to prompt node `node_id`, or nullptr. */
    const nlohmann::json* FindPromptValue(std::string_view node_id) const {
        auto it = prompt_values.find(std::string(node_id));
        return it == prompt_values.end() ? nullptr : &it->second;
    }

    /** @brief Bind `token` to `entities` (replacing any prior binding). */
    void BindSelector(std::string token, std::vector<ecs::Entity> entities) {
        selectors[std::move(token)] = std::move(entities);
    }

    /** @brief Bind a prompt result to `node_id`. */
    void BindPromptValue(std::string node_id, nlohmann::json value) {
        prompt_values[std::move(node_id)] = std::move(value);
    }
};

/**
 * @class OpArgs
 * @brief A node's declared args, raw JSON plus Resolver-bound entities.
 *
 * `raw_` is the node's verbatim `args` object; `entities_` holds the resolved
 * entities for selector-typed args (a set selector may bind several). Typed
 * getters consult `match::modload::FindOp` / `ArgSpec` and fail safe: a
 * missing arg, an undeclared arg, a declaration/JSON kind mismatch or a
 * wrong-typed value all yield `false` (or nullptr / nullopt / the fallback)
 * rather than throwing or returning garbage.
 *
 * Undeclared extra args (legal when `allows_extra_args`) are reachable through
 * `Raw()` / `Find()`; the typed getters intentionally reject them.
 */
class OpArgs {
public:
    OpArgs() = default;

    explicit OpArgs(std::string op_name,
                    nlohmann::json raw = nlohmann::json::object())
        : op_name_(std::move(op_name)), raw_(std::move(raw)) {}

    /** @brief Catalog name of the op the args belong to. */
    const std::string& OpName() const { return op_name_; }

    /** @brief Verbatim `args` object (never mutated after construction). */
    const nlohmann::json& Raw() const { return raw_; }

    /** @brief Declared signature for `OpName()`, or nullptr when unknown. */
    const modload::OpSignature* Signature() const {
        return modload::FindOp(op_name_);
    }

    /** @brief Declared spec for arg `name`, or nullptr when not declared. */
    const modload::ArgSpec* Spec(std::string_view name) const {
        const modload::OpSignature* signature = Signature();
        if (signature == nullptr) return nullptr;
        for (const modload::ArgSpec& arg : signature->args) {
            if (arg.name == name) return &arg;
        }
        return nullptr;
    }

    /** @brief True when `OpName()` declares an arg named `name`. */
    bool Declares(std::string_view name) const {
        return Spec(name) != nullptr;
    }

    /** @brief True when the raw args object carries key `name`. */
    bool Has(std::string_view name) const {
        return raw_.is_object() && raw_.contains(std::string(name));
    }

    /** @brief Raw value for `name`, or nullptr when absent / not an object. */
    const nlohmann::json* Find(std::string_view name) const {
        if (!raw_.is_object()) return nullptr;
        auto it = raw_.find(std::string(name));
        return it == raw_.end() ? nullptr : &(*it);
    }

    // --- typed extraction (declaration-checked, fail safe) -----------------

    /** @brief Integer out-param; false when undeclared, absent or not int. */
    bool GetInt(std::string_view name, int64_t& out) const {
        const modload::ArgSpec* spec = Spec(name);
        if (spec == nullptr || !IntLike(spec->type)) return false;
        const nlohmann::json* value = Find(name);
        if (value == nullptr || !value->is_number_integer()) return false;
        out = value->get<int64_t>();
        return true;
    }

    /** @brief Number out-param; false when undeclared/absent/wrong kind. */
    bool GetNumber(std::string_view name, double& out) const {
        const modload::ArgSpec* spec = Spec(name);
        if (spec == nullptr || !NumberLike(spec->type)) return false;
        const nlohmann::json* value = Find(name);
        if (value == nullptr || !value->is_number()) return false;
        out = value->get<double>();
        return true;
    }

    /** @brief Bool out-param; false when undeclared, absent or not bool. */
    bool GetBool(std::string_view name, bool& out) const {
        const modload::ArgSpec* spec = Spec(name);
        if (spec == nullptr || !BoolLike(spec->type)) return false;
        const nlohmann::json* value = Find(name);
        if (value == nullptr || !value->is_boolean()) return false;
        out = value->get<bool>();
        return true;
    }

    /** @brief String out-param; false when undeclared, absent or not string. */
    bool GetString(std::string_view name, std::string& out) const {
        const modload::ArgSpec* spec = Spec(name);
        if (spec == nullptr || !StringLike(spec->type)) return false;
        const nlohmann::json* value = Find(name);
        if (value == nullptr || !value->is_string()) return false;
        out = value->get<std::string>();
        return true;
    }

    /** @brief Object value; nullptr when undeclared, absent or not object. */
    const nlohmann::json* GetObject(std::string_view name) const {
        const modload::ArgSpec* spec = Spec(name);
        if (spec == nullptr || !ObjectLike(spec->type)) return nullptr;
        const nlohmann::json* value = Find(name);
        if (value == nullptr || !value->is_object()) return nullptr;
        return value;
    }

    /** @brief Array value; nullptr when undeclared, absent or not array. */
    const nlohmann::json* GetArray(std::string_view name) const {
        const modload::ArgSpec* spec = Spec(name);
        if (spec == nullptr || !ArrayLike(spec->type)) return nullptr;
        const nlohmann::json* value = Find(name);
        if (value == nullptr || !value->is_array()) return nullptr;
        return value;
    }

    // --- fail-safe convenience accessors -----------------------------------

    int64_t IntOr(std::string_view name, int64_t fallback) const {
        int64_t out = fallback;
        return GetInt(name, out) ? out : fallback;
    }

    double NumberOr(std::string_view name, double fallback) const {
        double out = fallback;
        return GetNumber(name, out) ? out : fallback;
    }

    bool BoolOr(std::string_view name, bool fallback) const {
        bool out = fallback;
        return GetBool(name, out) ? out : fallback;
    }

    std::string StringOr(std::string_view name, std::string fallback) const {
        std::string out;
        return GetString(name, out) ? out : std::move(fallback);
    }

    // --- Resolver-bound selector entities ----------------------------------

    /** @brief Bind selector arg `name` to Resolver-resolved entities. */
    void BindSelector(std::string name, std::vector<ecs::Entity> entities) {
        entities_[std::move(name)] = std::move(entities);
    }

    /** @brief True when `name` is bound to at least one entity. */
    bool HasEntities(std::string_view name) const {
        const std::vector<ecs::Entity>* found = FindEntities(name);
        return found != nullptr && !found->empty();
    }

    /** @brief Bound entities for `name`, or nullptr when never bound. */
    const std::vector<ecs::Entity>* FindEntities(
        std::string_view name) const {
        auto it = entities_.find(std::string(name));
        return it == entities_.end() ? nullptr : &it->second;
    }

    /** @brief First bound entity for `name`, or nullopt when unbound/empty. */
    std::optional<ecs::Entity> FirstEntity(std::string_view name) const {
        const std::vector<ecs::Entity>* found = FindEntities(name);
        if (found == nullptr || found->empty()) return std::nullopt;
        return found->front();
    }

    /** @brief Bound entities for `name`, or an empty vector when unbound. */
    std::vector<ecs::Entity> EntitiesOrEmpty(std::string_view name) const {
        const std::vector<ecs::Entity>* found = FindEntities(name);
        return found == nullptr ? std::vector<ecs::Entity>() : *found;
    }

private:
    static bool IntLike(modload::ArgType type) {
        return type == modload::ArgType::kInt
            || type == modload::ArgType::kNumber
            || type == modload::ArgType::kAny;
    }

    static bool NumberLike(modload::ArgType type) { return IntLike(type); }

    static bool BoolLike(modload::ArgType type) {
        return type == modload::ArgType::kBool
            || type == modload::ArgType::kAny;
    }

    static bool StringLike(modload::ArgType type) {
        switch (type) {
            case modload::ArgType::kString:
            case modload::ArgType::kEnum:
            case modload::ArgType::kSelector:
            case modload::ArgType::kKindRef:
            case modload::ArgType::kStatusRef:
            case modload::ArgType::kTagRef:
            case modload::ArgType::kRestrictionRef:
            case modload::ArgType::kNodeRef:
            case modload::ArgType::kChoiceSpec:
            case modload::ArgType::kStackPolicy:
            case modload::ArgType::kComparison:
            case modload::ArgType::kZone:
            case modload::ArgType::kPileRef:
            case modload::ArgType::kAspectMask:
            case modload::ArgType::kDuration:
            case modload::ArgType::kAny:
                return true;
            default:
                return false;
        }
    }

    static bool ObjectLike(modload::ArgType type) {
        switch (type) {
            case modload::ArgType::kObject:
            case modload::ArgType::kCondition:
            case modload::ArgType::kRollSpec:
            case modload::ArgType::kRestrictionEntry:
            case modload::ArgType::kDuration:
            case modload::ArgType::kAny:
                return true;
            default:
                return false;
        }
    }

    static bool ArrayLike(modload::ArgType type) {
        switch (type) {
            case modload::ArgType::kArray:
            case modload::ArgType::kAspectMask:
            case modload::ArgType::kDuration:
            case modload::ArgType::kAny:
                return true;
            default:
                return false;
        }
    }

    std::string op_name_;
    nlohmann::json raw_ = nlohmann::json::object();
    std::map<std::string, std::vector<ecs::Entity>, std::less<>> entities_;
};

/**
 * @struct OpContext
 * @brief Per-invocation handles an op needs to run.
 *
 * `event_bus` lets an op fire hooks; `budgets` is the match's the store
 * `BudgetLedger` (ops account chain steps/events through it); `frame` exposes
 * the Resolver's bound selectors and prompt results. `input_request` is the
 * only op-writable slot: an op that needs a choice sets it and returns
 * `OpStatus::kNeedsInput`.
 *
 * Ownership and lifetime belong to the Resolver / MatchInstance; the
 * context holds references only.
 */
struct OpContext {
    OpContext(ecs::EventBus& bus, ecs::BudgetLedger& ledger,
              ResolutionFrame& frame)
        : event_bus(bus), budgets(ledger), frame(frame) {}

    ecs::EventBus& event_bus;         /**< hook dispatch for op side effects. */
    ecs::BudgetLedger& budgets;       /**< match budget ledger. */
    ResolutionFrame& frame;           /**< bound selectors + prompt results. */
    std::optional<InputRequest> input_request; /**< set when input is needed. */

    /**
     * INFO: The assembly's frozen `CompactCardV2` index map, set by
     *       the Resolver on every invocation so a card-prompt op can resolve a
     *       candidate entity to its wire bits. Null in bare-op tests that do
     *       not assemble a match; an op must then decline to emit identity.
     */
    const engine::MatchRegistries* registries = nullptr;
};

/**
 * @struct OpResult
 * @brief Structured outcome of one op invocation.
 *
 * `events` and `effects` are ordered JSON descriptors: events are the
 * event-stream packets an op emits; effects are effect descriptors the
 * Resolver routes on. Both are opaque to this seam and interpreted by the
 * Resolver / event sink.
 */
struct OpResult {
    OpStatus status = OpStatus::kResolved;  /**< outcome class. */
    nlohmann::json value = nullptr;         /**< op value / prompt envelope. */
    std::vector<nlohmann::json> events;     /**< emitted event descriptors. */
    std::vector<nlohmann::json> effects;    /**< emitted effect descriptors. */
    std::string error;                      /**< set when status is kError. */

    static OpResult Resolved(nlohmann::json value = nullptr) {
        OpResult result;
        result.status = OpStatus::kResolved;
        result.value = std::move(value);
        return result;
    }

    static OpResult NeedsInput(nlohmann::json value) {
        OpResult result;
        result.status = OpStatus::kNeedsInput;
        result.value = std::move(value);
        return result;
    }

    static OpResult Error(std::string message) {
        OpResult result;
        result.status = OpStatus::kError;
        result.error = std::move(message);
        return result;
    }

    /** @brief False only for `kError`; `kNeedsInput` is a valid pause. */
    bool ok() const { return status != OpStatus::kError; }
};

/**
 * @brief The single function type every op body uses.
 *
 * `args` is const: an op reads its declared args and resolved entities but
 * never mutates them. `ctx` is mutable: an op may raise its input request.
 */
using OpFn = OpResult (*)(ecs::EntityStore& store, const OpArgs& args,
                          OpContext& ctx);

/**
 * @class OpRuntime
 * @brief Registry binding `OpCatalog()` names to op bodies.
 *
 * `Register` is the seam tests use to install a synthetic op; production
 * stubs are installed by `RegisterDefaultOps` / `MakeDefaultRuntime`, the one
 * place that wires catalog names to bodies. `Invoke` is a structured miss on
 * an unknown or null registration, never a crash.
 */
class OpRuntime {
public:
    /**
     * @brief Register (or replace) an op body.
     * @return false on an empty name or a null function.
     */
    bool Register(const std::string& name, OpFn fn) {
        if (name.empty() || fn == nullptr) return false;
        ops_[name] = fn;
        return true;
    }

    /** @brief True when `name` has a non-null body registered. */
    bool IsRegistered(std::string_view name) const {
        auto it = ops_.find(std::string(name));
        return it != ops_.end() && it->second != nullptr;
    }

    /** @brief Number of registered names. */
    std::size_t Size() const { return ops_.size(); }

    /** @brief Registered names, in deterministic (map) order. */
    std::vector<std::string> Names() const {
        std::vector<std::string> names;
        names.reserve(ops_.size());
        for (const auto& entry : ops_) names.push_back(entry.first);
        return names;
    }

    /**
     * @brief Run the body registered for `name`.
     *
     * @return `kError` with an explanatory message when `name` is unknown or
     *         has no body; otherwise the body's own result.
     */
    OpResult Invoke(const std::string& name, ecs::EntityStore& store,
                    const OpArgs& args, OpContext& ctx) const {
        auto it = ops_.find(name);
        if (it == ops_.end() || it->second == nullptr) {
            return OpResult::Error("unknown op: " + name);
        }
        return it->second(store, args, ctx);
    }

private:
    std::map<std::string, OpFn> ops_;
};

/**
 * @brief Install the production stubs for every `OpCatalog` name.
 *
 * This is the single place production registrations happen. Every name in
 * `OpCatalog()` is registered; known names map to their per-family stub and
 * any catalog name without a family stub falls back to a generic
 * "not implemented" body. The op layer replaces the per-family bodies, not this
 * wiring.
 */
void RegisterDefaultOps(OpRuntime& runtime);

/** @brief Build a runtime with all production stubs installed. */
OpRuntime MakeDefaultRuntime();

}  // namespace match::ops
