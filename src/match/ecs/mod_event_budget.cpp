#include <match/ecs/mod_event_budget.hpp>

#include <match/modload/event_definition_keys.hpp>

#include <logger.hpp>

#include <cerrno>
#include <cstdlib>

namespace match::ecs {

static_assert(ModEventLimits::kDefaultPayloadMaxBytes
              == modload::event_keys::kMaxEventPayloadBytes,
              "the default payload limit is the ticket 12 constant");

namespace {

/**
 * @brief Parse one limit variable strictly.
 *
 * Unset, empty, trailing junk and a negative value give `fallback`. A value
 * above `maximum`, or out of range, gives `maximum`.
 */
uint32_t ReadLimit(const char* name, uint32_t fallback, uint32_t maximum) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return fallback;
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(raw, &end, 10);
    if (end == raw || *end != '\0') {
        Logger::Warn("[ModEventBudget] invalid ", name, " '", raw,
                     "', using ", fallback);
        return fallback;
    }
    if (parsed < 0) return fallback;
    if (errno == ERANGE || parsed > static_cast<long long>(maximum)) {
        return maximum;
    }
    return static_cast<uint32_t>(parsed);
}

}  // namespace

std::string_view ModEventBudgetName(ModEventBudget budget) {
    switch (budget) {
        case ModEventBudget::kPerTurn:
            return "per_turn";
        case ModEventBudget::kPayloadBytes:
            return "payload_bytes";
        case ModEventBudget::kCauseChain:
            return "cause_chain";
    }
    return "unknown";
}

ModEventLimits ModEventLimits::FromEnv() {
    ModEventLimits limits;
    limits.events_per_turn =
        ReadLimit("UNI_MOD_EVENTS_PER_TURN", kDefaultEventsPerTurn,
                  kMaxEventsPerTurn);
    limits.payload_max_bytes =
        ReadLimit("UNI_MOD_EVENT_PAYLOAD_MAX", kDefaultPayloadMaxBytes,
                  kMaxPayloadMaxBytes);
    limits.cause_chain_max =
        ReadLimit("UNI_MOD_CAUSE_CHAIN_MAX", kDefaultCauseChainMax,
                  kMaxCauseChainMax);
    limits.violation_turns =
        ReadLimit("UNI_MOD_VIOLATION_TURNS", kDefaultViolationTurns,
                  kMaxViolationTurns);
    if (limits.violation_turns == 0) limits.violation_turns = 1;
    return limits;
}

const ModEventLimits& ModEventLedger::Limits() {
    if (!limits_.has_value()) limits_ = ModEventLimits::FromEnv();
    return *limits_;
}

bool ModEventLedger::CountEvent(const std::string& mod_id, uint32_t turn) {
    const uint32_t allowed = Limits().events_per_turn;
    ModUsage& usage = usage_[mod_id];
    if (!usage.has_turn || usage.turn != turn) {
        usage.turn = turn;
        usage.emitted = 0;
        usage.has_turn = true;
    }
    if (usage.emitted >= allowed) return false;
    ++usage.emitted;
    return true;
}

bool ModEventLedger::NoteDrop(const std::string& mod_id,
                              const std::string& event_id,
                              ModEventBudget budget, uint32_t turn) {
    const std::string_view budget_name = ModEventBudgetName(budget);
    ++drop_counts_[mod_id + "|" + event_id + "|" + std::string(budget_name)];

    const bool first_in_turn =
        logged_.emplace(mod_id, static_cast<int>(budget), turn).second;
    if (first_in_turn) {
        Logger::Error("[ModEventBudget] dropped event '", event_id,
                      "' of mod '", mod_id, "': ", budget_name,
                      " budget exceeded in turn ", turn);
    }

    ModUsage& usage = usage_[mod_id];
    usage.violation_turns.insert(turn);
    if (usage.threshold_reported
        || usage.violation_turns.size() < Limits().violation_turns) {
        return false;
    }
    usage.threshold_reported = true;
    return true;
}

std::size_t ModEventLedger::ViolationTurnCount(
    const std::string& mod_id) const {
    const auto found = usage_.find(mod_id);
    return found == usage_.end() ? 0 : found->second.violation_turns.size();
}

}  // namespace match::ecs
