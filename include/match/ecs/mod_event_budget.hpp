#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>

/**
 * @file mod_event_budget.hpp
 * @brief The limits and the ledger of the mod event budgets.
 *
 * A mod event is a `mod_event` engine event that a mod emits with the
 * `emit_event` op. Three budgets limit it. The engine drops the extra events
 * of a mod, logs each drop, and disarms a mod that breaks a budget in
 * several distinct turns. A core event never counts and is never dropped.
 *
 * | Variable                    | Default | Maximum | Meaning                |
 * |-----------------------------|---------|---------|------------------------|
 * | `UNI_MOD_EVENTS_PER_TURN`   | 64      | 10000   | events of one mod/turn |
 * | `UNI_MOD_EVENT_PAYLOAD_MAX` | 1024    | 65536   | payload bytes of one   |
 * | `UNI_MOD_CAUSE_CHAIN_MAX`   | 16      | 1000    | length of a cause chain|
 * | `UNI_MOD_VIOLATION_TURNS`   | 3       | 1000    | turns before a disarm  |
 *
 * A value with trailing junk or a negative value gives the default. A value
 * above the maximum gives the maximum. `0` is valid for the first three
 * (the mod emits no event, or only an empty payload, or no chain); for
 * `UNI_MOD_VIOLATION_TURNS` it gives `1`.
 */

namespace match::ecs {

/** @brief The budget that a dropped mod event broke. */
enum class ModEventBudget { kPerTurn, kPayloadBytes, kCauseChain };

/** @brief The name of a budget in the log and in the drop counts. */
std::string_view ModEventBudgetName(ModEventBudget budget);

/**
 * @struct ModEventLimits
 * @brief The effective limits, read from the environment.
 */
struct ModEventLimits {
    static constexpr uint32_t kDefaultEventsPerTurn = 64;
    static constexpr uint32_t kMaxEventsPerTurn = 10000;
    static constexpr uint32_t kDefaultPayloadMaxBytes = 1024;
    static constexpr uint32_t kMaxPayloadMaxBytes = 65536;
    static constexpr uint32_t kDefaultCauseChainMax = 16;
    static constexpr uint32_t kMaxCauseChainMax = 1000;
    static constexpr uint32_t kDefaultViolationTurns = 3;
    static constexpr uint32_t kMaxViolationTurns = 1000;

    uint32_t events_per_turn = kDefaultEventsPerTurn;
    uint32_t payload_max_bytes = kDefaultPayloadMaxBytes;
    uint32_t cause_chain_max = kDefaultCauseChainMax;
    uint32_t violation_turns = kDefaultViolationTurns;

    /** @brief Read the four variables; see the table above. */
    static ModEventLimits FromEnv();
};

/**
 * @class ModEventLedger
 * @brief Per mod counters of the mod event budgets for one match.
 *
 * The ledger lives in the `BudgetLedger` of the match. The limits are read
 * from the environment at the first use, unless `SetLimits` sets them.
 */
class ModEventLedger {
public:
    /** @brief The limits; the first call reads the environment. */
    const ModEventLimits& Limits();

    /** @brief Replace the limits (tests, and an embedding server). */
    void SetLimits(const ModEventLimits& limits) { limits_ = limits; }

    /**
     * @brief Count one mod event of `mod_id` in `turn`.
     *
     * The count of a mod restarts when `turn` differs from the turn of its
     * last count. A refused event does not change the count.
     *
     * @return false when the mod already used `events_per_turn` events.
     */
    bool CountEvent(const std::string& mod_id, uint32_t turn);

    /**
     * @brief Record a dropped mod event.
     *
     * Counts the drop and logs it at error level. The log has one line for
     * each mod, budget and turn, so that a flood does not flood the log.
     * Notes `turn` as a violation turn of the mod.
     *
     * @return true when this drop makes the mod reach `violation_turns`
     *         distinct violation turns for the first time.
     */
    bool NoteDrop(const std::string& mod_id, const std::string& event_id,
                  ModEventBudget budget, uint32_t turn);

    /** @brief Distinct turns in which `mod_id` broke a budget. */
    std::size_t ViolationTurnCount(const std::string& mod_id) const;

    /** @brief Drops by `"<mod>|<event>|<budget>"`. */
    const std::map<std::string, uint64_t>& DropCounts() const {
        return drop_counts_;
    }

private:
    struct ModUsage {
        uint32_t turn = 0;
        uint32_t emitted = 0;
        bool has_turn = false;
        bool threshold_reported = false;
        std::set<uint32_t> violation_turns;
    };

    std::optional<ModEventLimits> limits_;
    std::map<std::string, ModUsage> usage_;
    std::map<std::string, uint64_t> drop_counts_;
    std::set<std::tuple<std::string, int, uint32_t>> logged_;
};

}  // namespace match::ecs
