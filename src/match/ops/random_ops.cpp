#include <match/engine/match_assembler.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/resolver.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
using modload::TableDef;
using modload::TableEntry;

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

namespace {

/* INFO: Resolve a `table` ref against the
 *       frozen per-match registry. A token with `:` is exact. A bare token is
 *       scoped to the invoking mod (`ns:ref`) when `ns` is known, matching the
 *       validator's `NormalizeRef`; failing that it tail-matches exactly one
 *       `namespace:id` and is otherwise rejected (ambiguous bare refs never
 *       silently bind another mod's table). Null registry / no match = no-op. */
const TableDef* ResolveTable(const TableRegistry* tables,
                             const std::string& ref,
                             const std::string* invoking_mod) {
    if (tables == nullptr) return nullptr;
    auto exact = tables->find(ref);
    if (exact != tables->end()) return &exact->second;
    if (ref.find(':') != std::string::npos) return nullptr;

    if (invoking_mod != nullptr && !invoking_mod->empty()) {
        auto scoped = tables->find(*invoking_mod + ":" + ref);
        if (scoped != tables->end()) return &scoped->second;
    }

    const TableDef* unique = nullptr;
    for (const auto& entry : *tables) {
        const std::string& full = entry.first;
        const bool tail_match =
            full.size() > ref.size() + 1
            && full.compare(full.size() - ref.size(), ref.size(), ref) == 0
            && full[full.size() - ref.size() - 1] == ':';
        if (!tail_match) continue;
        if (unique != nullptr) return nullptr;  // ambiguous: fail closed
        unique = &entry.second;
    }
    return unique;
}

/* INFO: bind a table's entries against the frame and keep only those whose
 *       `where` (when present) evaluates true (S7). */
bool EntryEligible(ecs::EntityStore& store, const TableEntry& entry,
                   OpContext& ctx) {
    if (!entry.where.has_value()) return true;
    return StoreDefaultConditions().Evaluate(store, *entry.where, ctx);
}

/* INFO: normalize+validate a nested roll spec; false when malformed.
 *       Post-validation it never is, but the op stays total. */
bool ParseNestedRoll(const json& raw, int64_t& sides, int64_t& count,
                     int64_t& keep) {
    if (!raw.is_object()) return false;
    auto s = raw.find("sides");
    auto c = raw.find("count");
    if (s == raw.end() || !s->is_number_integer()
        || c == raw.end() || !c->is_number_integer()) {
        return false;
    }
    sides = s->get<int64_t>();
    count = c->get<int64_t>();
    keep = count;
    auto k = raw.find("keep");
    if (k != raw.end()) {
        if (!k->is_number_integer()) return false;
        keep = k->get<int64_t>();
    }
    return sides >= 1 && sides <= 1000000 && count >= 1 && count <= 1000
        && keep >= 1 && keep <= count;
}

}  // namespace

OpResult OpRollTable(ecs::EntityStore& store, const OpArgs& args,
                     OpContext& ctx) {
    std::string table_ref;
    if (!args.GetString("table", table_ref) || table_ref.empty()) {
        return OpResult::Resolved();
    }

    const TableDef* table = ResolveTable(
        ctx.registries == nullptr ? nullptr : &ctx.registries->tables,
        table_ref, ctx.mod_id);
    if (table == nullptr || table->entries.empty()) {
        return OpResult::Resolved();
    }

    // INFO: validate every nested roll up front so a malformed entry (which
    //       validation should have blocked) can never half-consume the stream.
    for (const TableEntry& entry : table->entries) {
        if (!entry.roll.has_value()) continue;
        int64_t sides = 0;
        int64_t count = 0;
        int64_t keep = 0;
        if (!ParseNestedRoll(*entry.roll, sides, count, keep)) {
            return OpResult::Resolved();
        }
    }

    std::vector<const TableEntry*> eligible;
    uint64_t total_weight = 0;
    for (const TableEntry& entry : table->entries) {
        if (!EntryEligible(store, entry, ctx)) continue;
        // INFO: weights are validated to 1..1000000; saturate rather than
        //       overflow if a hand-built fixture ever violates that.
        const uint64_t weight = static_cast<uint64_t>(entry.weight);
        if (total_weight > UINT64_MAX - weight) total_weight = UINT64_MAX;
        else total_weight += weight;
        eligible.push_back(&entry);
    }
    if (eligible.empty() || total_weight == 0) return OpResult::Resolved();

    const std::optional<RngDraw> pick = AdvanceRng(store);
    if (!pick.has_value()) return OpResult::Resolved();
    const uint64_t pick_counter = pick->counter;
    uint64_t state = pick->state;

    const uint64_t roll = SplitMix64(state) % total_weight;
    const TableEntry* chosen = eligible.back();
    uint64_t cursor = 0;
    for (const TableEntry* entry : eligible) {
        cursor += static_cast<uint64_t>(entry->weight);
        if (roll < cursor) {
            chosen = entry;
            break;
        }
    }

    int64_t value = chosen->value.value_or(0);
    if (chosen->roll.has_value()) {
        int64_t sides = 0;
        int64_t count = 0;
        int64_t keep = 0;
        (void)ParseNestedRoll(*chosen->roll, sides, count, keep);

        const std::optional<RngDraw> nested = AdvanceRng(store);
        // INFO: unreachable in practice - the pick above already proved the
        //       match has `rng_state`. The nested spec was validated up front,
        //       so the only failure left is a vanished store, which cannot
        //       happen mid-op; treat it as a no-op either way.
        if (!nested.has_value()) return OpResult::Resolved();
        state = nested->state;

        std::vector<int64_t> outcomes;
        outcomes.reserve(static_cast<std::size_t>(count));
        for (int64_t i = 0; i < count; ++i) {
            const uint64_t draw = SplitMix64(state);
            outcomes.push_back(static_cast<int64_t>(draw
                                % static_cast<uint64_t>(sides)) + 1);
        }
        std::sort(outcomes.begin(), outcomes.end(),
                  [](int64_t a, int64_t b) { return a > b; });
        value = 0;
        for (int64_t i = 0; i < keep; ++i) {
            value += outcomes[static_cast<std::size_t>(i)];
        }
    }

    const std::string full_table = table->table_id;
    const std::string full_key = table->namespace_id + ":" + chosen->key;

    std::string into(kLastOutcomeFrameKey);
    (void)args.GetString("into", into);
    BindLastOutcome(ctx.frame, full_table, full_key, value, into);

    const json outcome = json{{"table", full_table},
                              {"key", full_key},
                              {"value", value}};
    OpResult result = OpResult::Resolved(outcome);
    result.events.push_back(MakeEvent(
        "outcome_result", json{{"table", full_table},
                               {"key", full_key},
                               {"value", value},
                               {"counter", pick_counter}}));
    return result;
}

}  // namespace match::ops::detail
