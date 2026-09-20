#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file visibility_ops.cpp
 * @brief Visibility op bodies.
 *
 * `grant_visibility` / `revoke_visibility` maintain the `VisibilityGrant`
 * component on the *target* entity (the entity whose aspects are exposed: a
 * player's hand, a pile or a card). Each entry is `(viewer, aspect_mask,
 * expiry)`. A grant merges per viewer, OR-ing the incoming aspect mask into an
 * existing entry; a revoke may drop single aspects from an entry or remove
 * every entry for the viewer. Aspects are the frozen
 * `modload::VisibilityAspects()` tokens and are validated through
 * `modload::IsVisibilityAspect`.
 *
 * Expiry is the timer layer's: no body reads a clock. A granted duration is
 * collapsed to a single `{unit, value}` leg and its value is recorded in
 * `expires_ms` (0 = never), with the unit reported in the result value. This is
 * the same slotless-duration seam the (`set_turn_timer`) and
 * (`apply_status`) ops use; the timer layer owns the true wall-clock
 * interpretation.
 *
 * Every fail-safe path — an unbound selector, a dead entity, a missing /
 * malformed aspect mask or duration — is a `kResolved` no-op, never a crash.
 */

namespace match::ops::detail {
namespace {

using nlohmann::json;

/** @brief Entity handle as a JSON object. */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/** @brief Map a visibility aspect token to its bit. */
bool AspectBit(const std::string& token, uint32_t& bit) {
    if (!modload::IsVisibilityAspect(token)) return false;
    if (token == "count") {
        bit = static_cast<uint32_t>(ecs::Aspect::kCount);
    } else if (token == "color") {
        bit = static_cast<uint32_t>(ecs::Aspect::kColor);
    } else if (token == "value") {
        bit = static_cast<uint32_t>(ecs::Aspect::kValue);
    } else if (token == "identity") {
        bit = static_cast<uint32_t>(ecs::Aspect::kIdentity);
    } else if (token == "position") {
        bit = static_cast<uint32_t>(ecs::Aspect::kPosition);
    } else {
        return false;
    }
    return true;
}

/** @brief Canonical aspect token list for a mask. */
json AspectsFromMask(uint32_t mask) {
    json out = json::array();
    for (const std::string& token : modload::VisibilityAspects()) {
        uint32_t bit = 0;
        if (AspectBit(token, bit) && (mask & bit) != 0) {
            out.push_back(token);
        }
    }
    return out;
}

/** @brief Tri-state result of reading the optional `aspects` arg. */
enum class AspectParse {
    kAbsent,  /**< raw key not present; `revoke` means "all aspects". */
    kOk,      /**< parsed; `mask` is valid (0 when the array is empty). */
    kBad,     /**< present but malformed; callers fail safe. */
};

/**
 * @brief Read the `aspects` arg as a single token or an array of tokens.
 *
 * Rejects a present-but-malformed value (wrong JSON kind, non-string element
 * or unknown token) via `kBad` so callers do not silently treat it as absent.
 */
AspectParse ParseAspects(const OpArgs& args, uint32_t& mask) {
    mask = 0;
    if (!args.Has("aspects")) return AspectParse::kAbsent;

    const json* array = args.GetArray("aspects");
    if (array != nullptr) {
        for (const json& item : *array) {
            if (!item.is_string()) return AspectParse::kBad;
            uint32_t bit = 0;
            if (!AspectBit(item.get<std::string>(), bit)) {
                return AspectParse::kBad;
            }
            mask |= bit;
        }
        return AspectParse::kOk;
    }

    std::string token;
    if (args.GetString("aspects", token)) {
        uint32_t bit = 0;
        if (!AspectBit(token, bit)) return AspectParse::kBad;
        mask = bit;
        return AspectParse::kOk;
    }
    return AspectParse::kBad;
}

// --- duration parsing -------------------------------

/** @brief Parse an unit token into its enum; false on an unknown token. */
bool ParseUnit(const std::string& token, ecs::DurationUnit& out) {
    if (!modload::IsDurationUnit(token)) return false;
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

/** @brief Wire token for a duration unit (result payload). */
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
 * `VisibilityGrant::Entry` has one `int64_t` expiry slot, so a compound
 * duration collapses to the leg that would elapse first: the smallest `ms`
 * leg when one exists, otherwise the smallest value among the remaining
 * units. Every leg must be well formed or the whole parse fails (fail-safe).
 * The timer layer owns true compound evaluation.
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
 * @brief Read the optional `duration` arg.
 *
 * @return false when `duration` is present but malformed (wrong JSON kind or
 *         an invalid leg); `has_duration` distinguishes absent from present.
 */
bool ReadDuration(const OpArgs& args, bool& has_duration,
                  ecs::DurationSpec& out) {
    has_duration = false;
    if (!args.Has("duration")) return true;
    const json* duration = args.GetObject("duration");
    if (duration == nullptr) duration = args.GetArray("duration");
    if (duration == nullptr) return false;
    if (!ParseDuration(*duration, out)) return false;
    has_duration = true;
    return true;
}

// --- grant component access ------------------------------------------------

/** @brief The mutable `visibility_grant` on `target`, adding one if absent. */
ecs::VisibilityGrant* EnsureGrant(ecs::EntityStore& store, ecs::Entity target) {
    ecs::VisibilityGrant* grant = store.Get<ecs::VisibilityGrant>(target);
    if (grant != nullptr) return grant;
    store.Add(target, ecs::VisibilityGrant{});
    return store.Get<ecs::VisibilityGrant>(target);
}

/**
 * @brief Union two expiry slots (`0 = never`).
 *
 * A never-expiring grant dominates a finite one, so if either side is 0 the
 * result is 0; otherwise the larger raw value wins. Units are not resolved
 * here, so a mixed-unit merge is only a best-effort union.
 */
int64_t MergeExpiry(int64_t existing, int64_t incoming) {
    if (existing == 0 || incoming == 0) return 0;
    return existing > incoming ? existing : incoming;
}

}  // namespace

OpResult OpGrantVisibility(ecs::EntityStore& store, const OpArgs& args,
                           OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> viewer = args.FirstEntity("viewer");
    const std::optional<ecs::Entity> target = args.FirstEntity("target");
    if (!viewer.has_value() || !store.IsAlive(*viewer)) {
        return OpResult::Resolved();
    }
    if (!target.has_value() || !store.IsAlive(*target)) {
        return OpResult::Resolved();
    }

    uint32_t mask = 0;
    const AspectParse aspects = ParseAspects(args, mask);
    if (aspects != AspectParse::kOk || mask == 0) return OpResult::Resolved();

    bool has_duration = false;
    ecs::DurationSpec duration{};
    if (!ReadDuration(args, has_duration, duration)) {
        return OpResult::Resolved();
    }
    const int64_t expires = has_duration ? duration.value : 0;

    ecs::VisibilityGrant* grant = EnsureGrant(store, *target);
    if (grant == nullptr) return OpResult::Resolved();

    bool merged = false;
    for (ecs::VisibilityGrant::Entry& entry : grant->entries) {
        if (entry.viewer != *viewer) continue;
        entry.aspect_mask |= mask;
        entry.expires_ms = MergeExpiry(entry.expires_ms, expires);
        merged = true;
        break;
    }
    if (!merged) {
        ecs::VisibilityGrant::Entry entry;
        entry.viewer = *viewer;
        entry.aspect_mask = mask;
        entry.expires_ms = expires;
        grant->entries.push_back(entry);
    }

    const json aspects_json = AspectsFromMask(mask);
    const json payload = {{"viewer", EntityJson(*viewer)},
                          {"target", EntityJson(*target)},
                          {"aspects", aspects_json}};
    // INFO: The unit has no the store slot, so it rides the result value;
    //       the event descriptor keeps the frozen `{viewer,target,aspects}`.
    json value = payload;
    if (has_duration) {
        value["duration_unit"] = std::string(UnitToken(duration.unit));
        value["duration_value"] = duration.value;
    }
    OpResult result = OpResult::Resolved(value);
    result.events.push_back(MakeEvent("visibility_granted", payload));
    return result;
}

OpResult OpRevokeVisibility(ecs::EntityStore& store, const OpArgs& args,
                            OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> viewer = args.FirstEntity("viewer");
    const std::optional<ecs::Entity> target = args.FirstEntity("target");
    if (!viewer.has_value() || !store.IsAlive(*viewer)) {
        return OpResult::Resolved();
    }
    if (!target.has_value() || !store.IsAlive(*target)) {
        return OpResult::Resolved();
    }

    uint32_t requested = 0;
    const AspectParse aspects = ParseAspects(args, requested);
    if (aspects == AspectParse::kBad) return OpResult::Resolved();
    // INFO: absent aspects = revoke every entry for the viewer; an explicit
    //       empty mask revokes nothing.
    const bool revoke_all = aspects == AspectParse::kAbsent;
    if (!revoke_all && requested == 0) return OpResult::Resolved();

    ecs::VisibilityGrant* grant = store.Get<ecs::VisibilityGrant>(*target);
    if (grant == nullptr) return OpResult::Resolved();

    uint32_t removed_mask = 0;
    std::vector<ecs::VisibilityGrant::Entry> kept;
    kept.reserve(grant->entries.size());
    for (ecs::VisibilityGrant::Entry& entry : grant->entries) {
        if (entry.viewer != *viewer) {
            kept.push_back(entry);
            continue;
        }
        const uint32_t before = entry.aspect_mask;
        if (revoke_all) {
            removed_mask |= before;
            continue;
        }
        entry.aspect_mask &= ~requested;
        removed_mask |= before & requested;
        if (entry.aspect_mask != 0) kept.push_back(entry);
    }
    if (removed_mask == 0) return OpResult::Resolved();

    grant->entries = std::move(kept);

    const json aspects_json = AspectsFromMask(removed_mask);
    const json payload = {{"viewer", EntityJson(*viewer)},
                          {"target", EntityJson(*target)},
                          {"aspects", aspects_json}};
    OpResult result = OpResult::Resolved(payload);
    result.events.push_back(MakeEvent("visibility_revoked", payload));
    return result;
}

}  // namespace match::ops::detail
