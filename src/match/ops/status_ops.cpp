#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

/**
 * @file status_ops.cpp
 * @brief Status op bodies.
 *
 * Every body is a total, bounded function over the store: it reads its
 * declared args through the fail-safe `OpArgs` getters, mutates the store
 * `Status` component through the checked typed API, and emits event
 * descriptors via `MakeEvent`. A missing/unbound selector, a dead entity, a
 * malformed duration or an unknown stack policy is a fail-safe `kResolved`
 * no-op, never a crash.
 *
 * Ruling The op layer manipulates the frozen the store `Status` component
 * directly and stores exactly what the args give it. Applying a status
 * definition's own defaults (`hidden`, its declared `stack_policy`,
 * `on_expire`) and retaining multiple `independent` instances are the timer
 * layer's status subsystem; the seams are documented in the report. The store's
 * `status` pool holds one instance per entity, so `independent` is honoured by
 * minting a fresh `instance_id` for the incoming instance (the previous
 * instance is not retained until the timer layer adds multi-instance storage).
 */

namespace match::ops::detail {
namespace {

using nlohmann::json;

/** @brief Entity handle as a JSON object. */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/** @brief Wire token for a duration unit. */
std::string_view UnitToken(ecs::DurationUnit unit) {
    switch (unit) {
        case ecs::DurationUnit::kMs:
            return "ms";
        case ecs::DurationUnit::kTurns:
            return "turns";
        case ecs::DurationUnit::kRounds:
            return "rounds";
        case ecs::DurationUnit::kCardsPlayed:
            return "cards_played";
    }
    return "turns";
}

/** @brief Parse an unit token; false on an unknown token. */
bool ParseUnit(const std::string& token, ecs::DurationUnit& out) {
    if (token == "ms") {
        out = ecs::DurationUnit::kMs;
    } else if (token == "turns") {
        out = ecs::DurationUnit::kTurns;
    } else if (token == "rounds") {
        out = ecs::DurationUnit::kRounds;
    } else if (token == "cards_played") {
        out = ecs::DurationUnit::kCardsPlayed;
    } else {
        return false;
    }
    return true;
}

/** @brief Parse one `{unit, value}` leg; false on a malformed leg. */
bool ParseDurationLeg(const json& leg, ecs::DurationSpec& out) {
    if (!leg.is_object()) return false;
    const auto unit_it = leg.find("unit");
    const auto value_it = leg.find("value");
    if (unit_it == leg.end() || value_it == leg.end()) return false;
    if (!unit_it->is_string() || !value_it->is_number_integer()) return false;
    ecs::DurationUnit unit{};
    if (!ParseUnit(unit_it->get<std::string>(), unit)) return false;
    const int64_t value = value_it->get<int64_t>();
    if (value < 0) return false;
    out.unit = unit;
    out.value = value;
    return true;
}

/**
 * @brief Parse a `duration` arg (object or compound array) to one leg.
 *
 * The store's `Status.duration` is a single `DurationSpec`, so a compound
 * duration collapses to the leg that would elapse first: the smallest `ms` leg
 * when one exists, otherwise the smallest value among the remaining units.
 * Every leg must be well formed or the whole parse fails (fail-safe no-op). The
 * timer layer owns true compound-duration evaluation.
 */
bool ParseDuration(const json& duration, ecs::DurationSpec& out) {
    if (duration.is_object()) return ParseDurationLeg(duration, out);
    if (!duration.is_array() || duration.empty()) return false;

    bool have = false;
    bool best_is_ms = false;
    ecs::DurationSpec best{};
    for (const json& leg : duration) {
        ecs::DurationSpec parsed{};
        if (!ParseDurationLeg(leg, parsed)) return false;
        const bool is_ms = parsed.unit == ecs::DurationUnit::kMs;
        if (!have) {
            best = parsed;
            best_is_ms = is_ms;
            have = true;
            continue;
        }
        if (is_ms && !best_is_ms) {
            best = parsed;
            best_is_ms = true;
        } else if (is_ms == best_is_ms && parsed.value < best.value) {
            best = parsed;
        }
    }
    if (!have) return false;
    out = best;
    return true;
}

/**
 * @brief Parse a `kStackPolicy` token into its enum plus `cap:N` ceiling.
 *
 * The token grammar is frozen by `modload::IsValidStackPolicy`; this re-checks
 * it so a bad token is a fail-safe no-op rather than a silent default. `cap`
 * is saturated at `UINT32_MAX`. `cap == 0` means "no ceiling".
 */
bool ParseStackPolicy(const std::string& token, ecs::StackPolicy& policy,
                      uint32_t& cap) {
    cap = 0;
    if (token == "replace") {
        policy = ecs::StackPolicy::kReplace;
        return true;
    }
    if (token == "accumulate") {
        policy = ecs::StackPolicy::kAccumulate;
        return true;
    }
    if (token == "independent") {
        policy = ecs::StackPolicy::kIndependent;
        return true;
    }
    if (token.rfind("cap:", 0) != 0) return false;
    const std::string digits = token.substr(4);
    if (digits.empty()) return false;
    uint64_t value = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return false;
        if (value > std::numeric_limits<uint32_t>::max() / 10u) {
            value = std::numeric_limits<uint32_t>::max();
            break;
        }
        value = value * 10u + static_cast<uint64_t>(c - '0');
    }
    if (value > std::numeric_limits<uint32_t>::max()) {
        value = std::numeric_limits<uint32_t>::max();
    }
    policy = ecs::StackPolicy::kCap;
    cap = static_cast<uint32_t>(value);
    return true;
}

/** @brief Saturating 32-bit add (magnitudes are `int32_t`). */
int32_t SaturateAdd(int32_t a, int32_t b) {
    const int64_t sum = static_cast<int64_t>(a) + static_cast<int64_t>(b);
    if (sum > std::numeric_limits<int32_t>::max()) {
        return std::numeric_limits<int32_t>::max();
    }
    if (sum < std::numeric_limits<int32_t>::min()) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(sum);
}

/**
 * @brief Mint the instance id for an incoming status instance.
 *
 * Deterministic and order-independent (no process-wide counter): the first
 * instance is `1`, a re-apply increments the existing id. The timer layer owns
 * real per-match instance allocation; deriving here keeps tests reproducible.
 */
uint32_t NextInstanceId(const ecs::Status* existing) {
    if (existing == nullptr) return 1;
    if (existing->instance_id == std::numeric_limits<uint32_t>::max()) {
        return existing->instance_id;
    }
    return existing->instance_id + 1;
}

/**
 * @brief Applied magnitude from `params` (`params.magnitude`), default 1.
 *
 * `params` is optional and its shape is not schema-validated for a magnitude,
 * so a missing / non-integer value falls back to 1 (documented seam: The timer
 * layer may broaden the parameter vocabulary).
 */
int32_t MagnitudeFrom(const json* params) {
    if (params == nullptr || !params->is_object()) return 1;
    const auto it = params->find("magnitude");
    if (it == params->end() || !it->is_number_integer()) return 1;
    const int64_t value = it->get<int64_t>();
    if (value > std::numeric_limits<int32_t>::max()) {
        return std::numeric_limits<int32_t>::max();
    }
    if (value < std::numeric_limits<int32_t>::min()) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(value);
}

/** @brief Accumulate toward `cap` (`cap == 0` = no ceiling). */
int32_t CapAccumulate(int32_t current, int32_t add, uint32_t cap) {
    const int32_t summed = SaturateAdd(current, add);
    if (cap == 0) return summed;
    if (cap > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return summed;
    }
    const int32_t ceiling = static_cast<int32_t>(cap);
    return summed > ceiling ? ceiling : summed;
}

/** @brief Clamp `value` into the declared bounds of `spec` (fail-safe). */
int64_t ClampToDeclaredBounds(const modload::ArgSpec* spec, int64_t value) {
    if (spec == nullptr || !spec->has_bounds) return value;
    const int64_t lo = static_cast<int64_t>(spec->min_value);
    const int64_t hi = static_cast<int64_t>(spec->max_value);
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

}  // namespace

OpResult OpApplyStatus(ecs::EntityStore& store, const OpArgs& args,
                       OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> target = args.FirstEntity("target");
    if (!target.has_value() || !store.IsAlive(*target)) {
        return OpResult::Resolved();
    }

    std::string kind;
    if (!args.GetString("status_kind", kind) || kind.empty()) {
        return OpResult::Resolved();
    }

    // INFO: duration is optional; absent means "no duration leg stored". A
    //       present-but-malformed duration is a fail-safe no-op.
    ecs::DurationSpec duration{ecs::DurationUnit::kTurns, 0};
    const json* raw_duration = args.GetObject("duration");
    if (raw_duration == nullptr) raw_duration = args.GetArray("duration");
    if (raw_duration != nullptr && !ParseDuration(*raw_duration, duration)) {
        return OpResult::Resolved();
    }

    // INFO: stack_policy is optional; absent or a wrong JSON kind falls back to
    //       the existing instance's policy (then "replace"), a
    //       present-but-invalid token is a fail-safe no-op.
    ecs::StackPolicy incoming = ecs::StackPolicy::kReplace;
    uint32_t incoming_cap = 0;
    bool has_policy_arg = true;
    std::string policy_token;
    if (!args.GetString("stack_policy", policy_token)) {
        if (args.Has("stack_policy")) return OpResult::Resolved();
        has_policy_arg = false;
        policy_token = "replace";
    }
    if (!ParseStackPolicy(policy_token, incoming, incoming_cap)) {
        return OpResult::Resolved();
    }

    const int32_t magnitude = MagnitudeFrom(args.GetObject("params"));

    // INFO: The `stack_policy` arg is authoritative when present; an
    //       absent arg falls back to the existing instance's policy (then
    //       "replace"). `independent` still mints a fresh instance id: The
    //       store stores one `Status` per entity, so the previous instance is
    //       not retained until the timer layer adds multi-instance storage.
    const ecs::Status* existing = store.Get<ecs::Status>(*target);
    ecs::Status value;
    if (existing != nullptr && existing->status_id == kind) {
        value = *existing;
        const ecs::StackPolicy policy =
            has_policy_arg ? incoming : existing->stack_policy;
        value.stack_policy = policy;
        value.cap = has_policy_arg ? incoming_cap : existing->cap;
        switch (policy) {
            case ecs::StackPolicy::kReplace:
                value.magnitude = magnitude;
                value.duration = duration;
                break;
            case ecs::StackPolicy::kAccumulate:
                value.magnitude = SaturateAdd(existing->magnitude, magnitude);
                break;
            case ecs::StackPolicy::kIndependent:
                value.instance_id = NextInstanceId(existing);
                value.magnitude = magnitude;
                value.duration = duration;
                break;
            case ecs::StackPolicy::kCap:
                value.magnitude =
                    CapAccumulate(existing->magnitude, magnitude, value.cap);
                break;
        }
    } else {
        value.status_id = kind;
        value.magnitude = magnitude;
        value.stack_policy = incoming;
        value.cap = incoming_cap;
        value.duration = duration;
        value.instance_id = NextInstanceId(existing);
    }

    if (store.Add(*target, value) == nullptr) return OpResult::Resolved();

    const std::string duration_unit(UnitToken(value.duration.unit));
    json payload = {{"target", EntityJson(*target)},
                    {"status_kind", value.status_id},
                    {"magnitude", value.magnitude},
                    {"duration_unit", duration_unit},
                    {"instance", value.instance_id}};
    OpResult result = OpResult::Resolved(payload);
    result.events.push_back(MakeEvent("status_applied", payload));
    return result;
}

OpResult OpRemoveStatus(ecs::EntityStore& store, const OpArgs& args,
                        OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> target = args.FirstEntity("target");
    if (!target.has_value() || !store.IsAlive(*target)) {
        return OpResult::Resolved();
    }
    const ecs::Status* existing = store.Get<ecs::Status>(*target);
    if (existing == nullptr) return OpResult::Resolved();

    // INFO: catalog `either_of` {status_kind, instance}; `status_kind` wins
    //       when both are present. Negative / out-of-range instance ids are a
    //       fail-safe miss.
    std::string kind;
    const bool has_kind = args.GetString("status_kind", kind) && !kind.empty();
    int64_t instance = 0;
    const bool has_instance = args.GetInt("instance", instance)
        && instance >= 0
        && instance <= static_cast<int64_t>(
               std::numeric_limits<uint32_t>::max());

    bool match = false;
    if (has_kind) {
        match = existing->status_id == kind;
    } else if (has_instance) {
        match = existing->instance_id == static_cast<uint32_t>(instance);
    } else {
        return OpResult::Resolved();
    }
    if (!match) return OpResult::Resolved();

    json payload = {{"target", EntityJson(*target)},
                    {"status_kind", existing->status_id},
                    {"instance", existing->instance_id}};
    store.Remove<ecs::Status>(*target);
    OpResult result = OpResult::Resolved(payload);
    result.events.push_back(MakeEvent("status_removed", payload));
    return result;
}

OpResult OpModifyStatus(ecs::EntityStore& store, const OpArgs& args,
                        OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> target = args.FirstEntity("target");
    if (!target.has_value() || !store.IsAlive(*target)) {
        return OpResult::Resolved();
    }

    std::string kind;
    if (!args.GetString("kind", kind) || kind.empty()) {
        return OpResult::Resolved();
    }
    int64_t raw_delta = 0;
    if (!args.GetInt("delta", raw_delta)) return OpResult::Resolved();
    // INFO: delta is declared IntArg(-1000, 1000); the validator rejects an
    //       out-of-range literal at load time, and this clamps as a runtime
    //       fail-safe so a stray value cannot exceed the declared adjustment
    //       budget.
    const int64_t delta =
        ClampToDeclaredBounds(args.Spec("delta"), raw_delta);

    ecs::Status* status = store.Get<ecs::Status>(*target);
    if (status == nullptr || status->status_id != kind) {
        return OpResult::Resolved();
    }

    int64_t next = static_cast<int64_t>(status->magnitude) + delta;
    if (status->stack_policy == ecs::StackPolicy::kCap && status->cap > 0
        && status->cap <= static_cast<uint32_t>(
               std::numeric_limits<int32_t>::max())
        && next > static_cast<int64_t>(status->cap)) {
        next = static_cast<int64_t>(status->cap);
    }
    if (next > std::numeric_limits<int32_t>::max()) {
        next = std::numeric_limits<int32_t>::max();
    }
    if (next < std::numeric_limits<int32_t>::min()) {
        next = std::numeric_limits<int32_t>::min();
    }
    status->magnitude = static_cast<int32_t>(next);

    return OpResult::Resolved(
        json{{"target", EntityJson(*target)},
             {"status_kind", status->status_id},
             {"magnitude", status->magnitude},
             {"delta", delta},
             {"instance", status->instance_id}});
}

}  // namespace match::ops::detail
