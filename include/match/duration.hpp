#pragma once

#include <match/ecs/components.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file duration.hpp
 * @brief Duration model: the clock seam, compound specs and elapsed tests.
 *
 *  durations are `{ "unit": ms|turns|rounds|cards_played,
 * "value": n }`. A compound duration is a non-empty array of legs and means
 * "whichever leg elapses first". This header owns the one parse path and
 * the one elapsed test every timing consumer (statuses, timers, scheduler)
 * shares; it reuses the `DurationUnit`/`DurationSpec` enums rather than
 * redefining them.
 *
 * clock seam: all wall-clock reads go through `NowMs`. Tests inject a fake
 * clock so no timing behaviour needs real waiting.
 */

namespace match {

/**
 * @brief Wall-clock seam: milliseconds since the Unix epoch.
 *
 * `std::function` so a test can substitute a deterministic counter. Never call
 * a clock directly outside this seam.
 */
using NowMs = std::function<int64_t()>;

/**
 * @brief Default clock: `system_clock` epoch milliseconds.
 */
inline NowMs DefaultNowMs() {
    return []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    };
}

/**
 * @brief Map a duration token to its unit; `nullopt` when unknown.
 */
inline std::optional<ecs::DurationUnit> DurationUnitFromToken(
    std::string_view token) {
    if (token == "ms") return ecs::DurationUnit::kMs;
    if (token == "turns") return ecs::DurationUnit::kTurns;
    if (token == "rounds") return ecs::DurationUnit::kRounds;
    if (token == "cards_played") return ecs::DurationUnit::kCardsPlayed;
    return std::nullopt;
}

/**
 * @brief Canonical token for a unit (`"cards_played"` for `kCardsPlayed`).
 */
inline std::string_view DurationUnitToken(ecs::DurationUnit unit) {
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

/**
 * @struct Duration
 * @brief One or more duration legs.
 *
 * A single spec is a one-element `legs`; a compound array is two or more.
 * Elapsed when ANY leg reaches its threshold.
 */
struct Duration {
    std::vector<ecs::DurationSpec> legs;

    /** @brief True when no leg is present (an unarmed / malformed duration). */
    bool Empty() const { return legs.empty(); }
};

/**
 * @brief Parse one `{ "unit", "value" }` leg.
 *
 * Mirrors the semantic validator's duration check: the unit must be a known
 * token and the value a non-negative integer. Unknown units, missing fields
 * and negative values all yield `nullopt`.
 */
inline std::optional<ecs::DurationSpec> ParseDurationSpec(
    const nlohmann::json& raw) {
    if (!raw.is_object()) return std::nullopt;
    auto unit = raw.find("unit");
    auto value = raw.find("value");
    if (unit == raw.end() || !unit->is_string()) return std::nullopt;
    if (value == raw.end() || !value->is_number_integer()) {
        return std::nullopt;
    }
    const std::optional<ecs::DurationUnit> parsed =
        DurationUnitFromToken(unit->get<std::string>());
    if (!parsed.has_value()) return std::nullopt;
    const int64_t amount = value->get<int64_t>();
    if (amount < 0) return std::nullopt;
    ecs::DurationSpec spec;
    spec.unit = *parsed;
    spec.value = amount;
    return spec;
}

/**
 * @brief Parse a duration arg: single object or non-empty compound array.
 *
 * The accepted wire shapes match `semantic_validator`'s `CheckDuration`: an
 * object (one leg) or a non-empty array of objects (compound). Anything else
 * yields `nullopt`.
 */
inline std::optional<Duration> ParseDuration(const nlohmann::json& raw) {
    Duration duration;
    if (raw.is_object()) {
        const std::optional<ecs::DurationSpec> leg = ParseDurationSpec(raw);
        if (!leg.has_value()) return std::nullopt;
        duration.legs.push_back(*leg);
        return duration;
    }
    if (!raw.is_array() || raw.empty()) return std::nullopt;
    for (const nlohmann::json& item : raw) {
        const std::optional<ecs::DurationSpec> leg = ParseDurationSpec(item);
        if (!leg.has_value()) return std::nullopt;
        duration.legs.push_back(*leg);
    }
    return duration;
}

/**
 * @struct DurationProgress
 * @brief Elapsed counters measured since a leg was armed.
 *
 * `elapsed_ms` comes from the clock; the other fields are advanced by the
 * caller's event drivers (`turn_end`, `round_end`, `after:play`) in a later
 * The timer layer slice. A leg elapses when its own counter reaches the declared
 * value.
 */
struct DurationProgress {
    int64_t elapsed_ms = 0;     /**< wall-clock ms since arm. */
    int64_t turns = 0;          /**< owner turns since arm. */
    int64_t rounds = 0;         /**< rounds since arm. */
    int64_t cards_played = 0;   /**< cards played since arm. */
};

/**
 * @brief True when `leg`'s threshold has been reached.
 *
 * A non-positive threshold is already elapsed; a progress below a positive
 * value is not. A negative progress (a clock that went backwards) never
 * elapses a positive leg.
 */
inline bool LegElapsed(const ecs::DurationSpec& leg,
                       const DurationProgress& progress) {
    if (leg.value <= 0) return true;
    switch (leg.unit) {
        case ecs::DurationUnit::kMs:
            return progress.elapsed_ms >= leg.value;
        case ecs::DurationUnit::kTurns:
            return progress.turns >= leg.value;
        case ecs::DurationUnit::kRounds:
            return progress.rounds >= leg.value;
        case ecs::DurationUnit::kCardsPlayed:
            return progress.cards_played >= leg.value;
    }
    return false;
}

/**
 * @brief True when any leg elapsed.
 */
inline bool DurationElapsed(const Duration& duration,
                            const DurationProgress& progress) {
    for (const ecs::DurationSpec& leg : duration.legs) {
        if (LegElapsed(leg, progress)) return true;
    }
    return false;
}

}  // namespace match
