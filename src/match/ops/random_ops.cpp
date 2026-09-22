#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file random_ops.cpp
 * @brief Roll op body.
 *
 * `roll` is deterministic. It reads/updates the `RngState{seed,
 * op_counter}` on the match entity, increments `op_counter`, and draws each
 * outcome from a splitmix64 stream seeded by `seed + op_counter`. With the same
 * seed and counter, the same spec yields the same outcomes; a different counter
 * yields a different stream. The op owns only the roll math: seeding/assembly
 *  and roll logging integration are outside this slice.
 *
 * `spec = {sides, count, keep?}`. `keep` defaults to `count` (keep all); the
 * emitted total (and the value `rolled` consumes) is the sum of the highest
 * `keep` outcomes. Every fail-safe path — missing match or `rng_state`, a
 * malformed/out-of-range spec — is a `kResolved` no-op that leaves the counter
 * untouched, never a crash.
 */

namespace match::ops::detail {
namespace {

using nlohmann::json;

/** @brief Inclusive sane bounds for `spec.sides`. */
constexpr int64_t kMinSides = 1;
constexpr int64_t kMaxSides = 1000000;

/** @brief Inclusive sane bounds for `spec.count`. */
constexpr int64_t kMinCount = 1;
constexpr int64_t kMaxCount = 1000;

/**
 * @brief Read an integer spec field; false on a present non-integer value.
 *
 * A `required` field that is absent also fails. Absence of an optional field
 * leaves `out` untouched.
 */
bool SpecInt(const json& spec, const char* name, bool required, int64_t& out) {
    auto it = spec.find(name);
    if (it == spec.end()) return !required;
    if (!it->is_number_integer()) return false;
    out = it->get<int64_t>();
    return true;
}

/**
 * @brief Parse and bounds-check `spec` into a normalized object.
 *
 * On success `normalized` carries `{sides,count,keep}` with `keep` defaulted to
 * `count`. Returns false for any malformed or out-of-range spec (fail-safe).
 */
bool ParseSpec(const json& raw, json& normalized) {
    int64_t sides = 0;
    int64_t count = 0;
    int64_t keep = 0;
    if (!SpecInt(raw, "sides", true, sides)) return false;
    if (!SpecInt(raw, "count", true, count)) return false;
    const bool has_keep = raw.contains("keep");
    if (!SpecInt(raw, "keep", false, keep)) return false;
    if (!has_keep) keep = count;

    if (sides < kMinSides || sides > kMaxSides) return false;
    if (count < kMinCount || count > kMaxCount) return false;
    if (keep < 1 || keep > count) return false;

    normalized = json{{"sides", sides}, {"count", count}, {"keep", keep}};
    return true;
}

}  // namespace

OpResult OpRoll(ecs::EntityStore& store, const OpArgs& args, OpContext& ctx) {
    const json* raw_spec = args.GetObject("spec");
    if (raw_spec == nullptr || !raw_spec->is_object()) {
        return OpResult::Resolved();
    }

    json spec = json::object();
    if (!ParseSpec(*raw_spec, spec)) return OpResult::Resolved();

    const std::optional<RngDraw> rng = AdvanceRng(store);
    if (!rng.has_value()) return OpResult::Resolved();

    // INFO: / the shared helper increments the counter before the
    //       draw and seeds the stream from seed + the (new) counter, so
    //       identical seed/counter pairs replay identically and each roll
    //       consumes a fresh stream.
    const uint64_t counter = rng->counter;
    uint64_t state = rng->state;

    const int64_t sides = spec.at("sides").get<int64_t>();
    const int64_t count = spec.at("count").get<int64_t>();
    const int64_t keep = spec.at("keep").get<int64_t>();

    std::vector<int64_t> outcomes;
    outcomes.reserve(static_cast<std::size_t>(count));
    const uint64_t side_span = static_cast<uint64_t>(sides);
    for (int64_t i = 0; i < count; ++i) {
        const uint64_t draw = SplitMix64(state);
        outcomes.push_back(static_cast<int64_t>(draw % side_span) + 1);
    }

    // INFO: `keep` keeps the highest outcomes (dice "keep N"); the total is the
    //       sum of those, which is exactly what the `rolled` condition reads.
    std::vector<int64_t> ranked = outcomes;
    std::sort(ranked.begin(), ranked.end(),
              [](int64_t a, int64_t b) { return a > b; });
    int64_t total = 0;
    for (int64_t i = 0; i < keep; ++i) {
        total += ranked[static_cast<std::size_t>(i)];
    }

    const json outcomes_json = outcomes;
    BindLastRoll(ctx.frame, total, outcomes_json);

    OpResult result = OpResult::Resolved(outcomes_json);
    result.events.push_back(MakeEvent(
        "roll_result", json{{"spec", spec},
                            {"outcomes", outcomes_json},
                            {"counter", counter}}));
    return result;
}

}  // namespace match::ops::detail
