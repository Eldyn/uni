#pragma once

#include <match/modload/artifacts.hpp>
#include <match/streak/matcher.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

/**
 * @file streak_engine.hpp
 * @brief The server streak engine: one evaluation pass for each base client
 *        event, with a bounded cost.
 *
 * The engine holds the value of every streak of the match. `Observe` runs the
 * one pass for one base client event. The streaks run in the sorted order of
 * their ids, so the values depend on the events only.
 *
 * Rules (spec "Streaks and derived events"):
 *
 * - A derived event does not feed a streak: `Observe` ignores a view with
 *   `derived` set. The engine therefore cannot loop.
 * - The engine reads a `ClientEventView`, which holds fields with the audience
 *   `all` only. A streak value is public to every seat.
 * - The cost is bounded. `UNI_STREAK_COST_MAX` is the most streak evaluations
 *   in one turn (default 2048). A streak past the limit is skipped for the
 *   event and a line goes to the log. A core event is never dropped.
 * - `UNI_CONTEXT_SIZE_MAX` is the most bytes of one `context` object (default
 *   512). A value that does not fit is left out and a line goes to the log.
 *
 * Values: `run` and ungrouped `total` are integers. A grouped `total` is an
 * object from group to count. A `last_value` is `{last, previous}`.
 */

namespace match::streak {

/** @brief The cost and size limits of the streak engine. */
struct StreakLimits {
    static constexpr uint32_t kDefaultCost = 2048;
    static constexpr uint32_t kMaxCost = 1000000;
    static constexpr uint32_t kDefaultContextBytes = 512;
    static constexpr uint32_t kMaxContextBytes = 65536;

    uint32_t cost_max_per_turn = kDefaultCost;
    uint32_t context_bytes_max = kDefaultContextBytes;

    /**
     * @brief Read `UNI_STREAK_COST_MAX` and `UNI_CONTEXT_SIZE_MAX`.
     *
     * Unset, empty, malformed (`12abc`), and negative values give the
     * default; a malformed value also logs a warning. A value above the
     * maximum is clamped to it.
     */
    static StreakLimits FromEnv();
};

/** @class StreakEngine */
class StreakEngine {
   public:
    /** @brief An engine with the limits of the environment. */
    StreakEngine() : limits_(StreakLimits::FromEnv()) {}

    explicit StreakEngine(StreakLimits limits) : limits_(limits) {}

    /** @brief Replace the definitions and reset every value. */
    void SetDefinitions(const modload::StreakRegistry& definitions);

    /** @brief True when at least one streak is defined. */
    bool Empty() const { return runtimes_.empty(); }

    /** @brief True when `streak_id` names a defined streak. */
    bool Has(const std::string& streak_id) const;

    /** @brief Run the one evaluation pass for one base client event. */
    void Observe(const ClientEventView& event);

    /** @brief The value of one streak; null for an unknown id. */
    nlohmann::json ValueOf(const std::string& streak_id) const;

    /**
     * @brief The `context` object for the requested streaks.
     *
     * Unknown ids are left out. A streak that the cost limit skipped in the
     * current turn is left out too (its value is stale). A value that would
     * pass the byte limit is left out and logged. Null when no value remains.
     */
    nlohmann::json ContextFor(
        const std::vector<std::string>& streak_ids) const;

    /** @brief The values of all streaks of one namespace, keyed by id. */
    nlohmann::json AllValues(const std::string& namespace_id) const;

    /** @brief The values of every loaded streak, keyed by id. */
    nlohmann::json AllValues() const;

    /** @brief The evaluations that the pass made in `turn`. */
    uint32_t EvaluationsInTurn(int64_t turn) const;

    const StreakLimits& Limits() const { return limits_; }

   private:
    struct Runtime {
        modload::StreakDef definition;
        int64_t count = 0;
        std::optional<std::string> last_group;
        nlohmann::json groups = nlohmann::json::object();
        nlohmann::json last;
        nlohmann::json previous;
    };

    void Apply(Runtime& runtime, const ClientEventView& event) const;
    static std::string GroupKey(const modload::StreakDef& definition,
                                const ClientEventView& event);

    StreakLimits limits_;
    std::map<std::string, Runtime> runtimes_;
    int64_t cost_turn_ = -1;
    uint32_t evaluations_ = 0;
    std::set<std::string> logged_skips_;
};

}  // namespace match::streak
