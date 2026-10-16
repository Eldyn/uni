#include <match/view/event_tail.hpp>

#include <logger.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>

namespace match::view {

std::size_t EventTail::LimitFromEnv() {
    const char* raw = std::getenv("UNI_EVENT_TAIL_MAX");
    if (raw == nullptr || *raw == '\0') return kDefaultLimit;
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(raw, &end, 10);
    if (end == raw || *end != '\0') {
        Logger::Warn("[EventTail] invalid UNI_EVENT_TAIL_MAX '" +
                     std::string(raw) + "', using " +
                     std::to_string(kDefaultLimit));
        return kDefaultLimit;
    }
    if (parsed < 0) return kDefaultLimit;
    if (errno == ERANGE || parsed > static_cast<long long>(kMaxLimit)) {
        return kMaxLimit;
    }
    return static_cast<std::size_t>(parsed);
}

void EventTail::Record(const nlohmann::json& engine_event) {
    if (!engine_event.is_object()) return;
    const nlohmann::json frame =
        engine_event.value("causal_frame", nlohmann::json::object());
    const uint32_t turn = frame.value("turn", 0u);
    const uint32_t round = frame.value("round", 0u);
    AdvanceCounters(turn, round);

    const std::string type = engine_event.value("type", std::string());
    const nlohmann::json payload =
        engine_event.value("payload", nlohmann::json::object());
    const Retention retention = RetentionOf(type, payload);
    if (retention == Retention::kTransient) return;

    TailEntry entry;
    entry.engine_event = engine_event;
    entry.retention = retention;
    entry.turn = turn;
    entry.round = round;
    entries_.push_back(std::move(entry));
    EnforceLimit(retention);
}

void EventTail::AdvanceCounters(uint32_t turn, uint32_t round) {
    // INFO: compare with the newest event, not with a maximum: the engine may
    //       restart the turn counter at a new round.
    const bool turn_ended = turn != current_turn_;
    const bool round_ended = round != current_round_;
    current_turn_ = turn;
    current_round_ = round;
    if (!turn_ended && !round_ended) return;
    std::erase_if(entries_, [&](const TailEntry& entry) {
        if (entry.retention == Retention::kUntilTurnEnd) {
            return entry.turn != current_turn_ ||
                   entry.round != current_round_;
        }
        if (entry.retention == Retention::kUntilRoundEnd) {
            return entry.round != current_round_;
        }
        return false;
    });
}

void EventTail::EnforceLimit(Retention retention) {
    const auto of_class = [retention](const TailEntry& entry) {
        return entry.retention == retention;
    };
    const std::size_t count = static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(), of_class));
    if (count <= limit_) return;
    std::size_t surplus = count - limit_;
    std::erase_if(entries_, [&](const TailEntry& entry) {
        if (surplus == 0 || !of_class(entry)) return false;
        --surplus;
        return true;
    });
}

}  // namespace match::view
