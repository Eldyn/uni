#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/status.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file status_ops.cpp
 * @brief Status op bodies.
 *
 * Every body is a total, bounded function over the store: it reads its
 * declared args through the fail-safe `OpArgs` getters, addresses statuses
 * through the `match::status` helper API (the additive `status_list`
 * component), and emits event descriptors via `MakeEvent`. A missing or
 * unbound selector, a dead entity, a malformed duration or an unknown stack
 * policy is a fail-safe `kResolved` no-op, never a crash.
 *
 * The op layer fix-round-1 rule preserved: a present `stack_policy` arg is
 * authoritative; an absent arg falls back to the stored policy (then
 * `replace`). `independent` now appends a real second instance with its own
 * duration, instead of overwriting the one the store slot.
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

    status::ApplyRequest request;
    request.status_id = kind;
    request.magnitude = MagnitudeFrom(args.GetObject("params"));
    request.duration = duration;

    // INFO: stack_policy is optional; absent falls back to the stored policy
    //       (then "replace") inside `status::Apply`, a present-but-invalid
    //       token is a fail-safe no-op. A present arg is authoritative.
    std::string policy_token;
    if (!args.GetString("stack_policy", policy_token)) {
        if (args.Has("stack_policy")) return OpResult::Resolved();
    } else {
        if (!ParseStackPolicy(policy_token, request.stack_policy,
                              request.cap)) {
            return OpResult::Resolved();
        }
        request.has_stack_policy = true;
    }

    const status::ApplyResult applied = status::Apply(store, *target, request);
    if (!applied.applied) return OpResult::Resolved();

    const std::string duration_unit(UnitToken(applied.duration.unit));
    json payload = {{"target", EntityJson(*target)},
                    {"status_kind", applied.status_id},
                    {"magnitude", applied.magnitude},
                    {"duration_unit", duration_unit},
                    {"instance", applied.instance_id}};
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

    // INFO: catalog `either_of` {status_kind, instance}; `status_kind` wins
    //       when both are present. Negative / out-of-range instance ids are a
    //       fail-safe miss. A kind removes every instance of that kind (the
    //       cleanse semantics under true multi-instance storage); an instance
    //       removes exactly one.
    std::string kind;
    const bool has_kind = args.GetString("status_kind", kind) && !kind.empty();
    int64_t instance = 0;
    const bool has_instance = args.GetInt("instance", instance)
        && instance >= 0
        && instance <= static_cast<int64_t>(
               std::numeric_limits<uint32_t>::max());

    if (has_kind) {
        const std::vector<ecs::Status> removed =
            status::Remove(store, *target, kind);
        if (removed.empty()) return OpResult::Resolved();
        OpResult result = OpResult::Resolved(
            json{{"target", EntityJson(*target)},
                 {"status_kind", removed.front().status_id},
                 {"instance", removed.front().instance_id}});
        for (const ecs::Status& entry : removed) {
            json payload = {{"target", EntityJson(*target)},
                            {"status_kind", entry.status_id},
                            {"instance", entry.instance_id}};
            result.events.push_back(MakeEvent("status_removed", payload));
        }
        return result;
    }
    if (has_instance) {
        const std::optional<ecs::Status> removed =
            status::RemoveByInstance(store, *target,
                                     static_cast<uint32_t>(instance));
        if (!removed.has_value()) return OpResult::Resolved();
        json payload = {{"target", EntityJson(*target)},
                        {"status_kind", removed->status_id},
                        {"instance", removed->instance_id}};
        OpResult result = OpResult::Resolved(payload);
        result.events.push_back(MakeEvent("status_removed", payload));
        return result;
    }
    return OpResult::Resolved();
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

    // INFO: every instance of `kind` is adjusted (cap-clamped); the reported
    //       magnitude and instance id are the first adjusted instance's.
    const std::optional<status::ModifyResult> modified =
        status::Modify(store, *target, kind, delta);
    if (!modified.has_value() || modified->adjusted.empty()) {
        return OpResult::Resolved();
    }
    const ecs::Status& first = modified->adjusted.front();

    return OpResult::Resolved(
        json{{"target", EntityJson(*target)},
             {"status_kind", first.status_id},
             {"magnitude", first.magnitude},
             {"delta", delta},
             {"instance", first.instance_id}});
}

}  // namespace match::ops::detail
