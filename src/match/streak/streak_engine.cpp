#include <match/streak/streak_engine.hpp>

#include <match/modload/streak_definition_keys.hpp>

#include <logger.hpp>

#include <cerrno>
#include <cstdlib>

namespace match::streak {

namespace {

namespace keys = modload::streak_keys;

uint32_t ReadLimit(const char* name, uint32_t fallback, uint32_t maximum) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return fallback;
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(raw, &end, 10);
    if (end == raw || *end != '\0') {
        Logger::Warn("[Streak] invalid ", name, " '", raw, "', using ",
                     fallback);
        return fallback;
    }
    if (parsed < 0) return fallback;
    if (errno == ERANGE || parsed > static_cast<long long>(maximum)) {
        return maximum;
    }
    return static_cast<uint32_t>(parsed);
}

nlohmann::json Initial(const modload::StreakDef& definition) {
    return definition.type == keys::kLastValue
               ? nlohmann::json{{"last", nullptr}, {"previous", nullptr}}
               : nlohmann::json(0);
}

}  // namespace

StreakLimits StreakLimits::FromEnv() {
    StreakLimits limits;
    limits.cost_max_per_turn =
        ReadLimit("UNI_STREAK_COST_MAX", kDefaultCost, kMaxCost);
    limits.context_bytes_max =
        ReadLimit("UNI_CONTEXT_SIZE_MAX", kDefaultContextBytes,
                  kMaxContextBytes);
    return limits;
}

void StreakEngine::SetDefinitions(const modload::StreakRegistry& definitions) {
    runtimes_.clear();
    for (const auto& [streak_id, definition] : definitions) {
        Runtime runtime;
        runtime.definition = definition;
        runtimes_[streak_id] = std::move(runtime);
    }
    cost_turn_ = -1;
    evaluations_ = 0;
    logged_skips_.clear();
}

bool StreakEngine::Has(const std::string& streak_id) const {
    return runtimes_.count(streak_id) != 0;
}

std::string StreakEngine::GroupKey(const modload::StreakDef& definition,
                                   const ClientEventView& event) {
    // INFO: the group key is JSON text (a string field keeps its text).
    if (definition.group_by.empty()) return std::string();
    const auto found = event.payload.find(definition.group_by);
    if (found == event.payload.end()) return std::string();
    return found->is_string() ? found->get<std::string>() : found->dump();
}

void StreakEngine::Apply(Runtime& runtime, const ClientEventView& event) const {
    const modload::StreakDef& definition = runtime.definition;
    if (MatcherMatches(definition.matcher, event)) {
        if (definition.type == keys::kLastValue) {
            runtime.previous = runtime.last;
            const auto found = event.payload.find(definition.value_field);
            runtime.last =
                found == event.payload.end() ? nlohmann::json() : *found;
            return;
        }
        const std::string group = GroupKey(definition, event);
        if (definition.type == keys::kTotal) {
            if (definition.group_by.empty()) {
                ++runtime.count;
            } else {
                runtime.groups[group] = runtime.groups.value(group, 0) + 1;
            }
            return;
        }
        const bool same_group = definition.group_by.empty()
                                || runtime.last_group == group;
        runtime.count = same_group ? runtime.count + 1 : 1;
        runtime.last_group = group;
        return;
    }
    if (definition.type != keys::kRun) return;
    for (const modload::StreakMatcherDef& reset : definition.reset_on) {
        if (MatcherMatches(reset, event)) {
            runtime.count = 0;
            runtime.last_group.reset();
            return;
        }
    }
}

void StreakEngine::Observe(const ClientEventView& event) {
    if (event.derived) return;
    if (event.state.turn != cost_turn_) {
        cost_turn_ = event.state.turn;
        evaluations_ = 0;
        logged_skips_.clear();
    }
    for (auto& [streak_id, runtime] : runtimes_) {
        const modload::StreakDef& definition = runtime.definition;
        bool relevant = definition.matcher.event == event.id;
        for (const modload::StreakMatcherDef& reset : definition.reset_on) {
            relevant = relevant || reset.event == event.id;
        }
        if (!relevant) continue;
        if (evaluations_ >= limits_.cost_max_per_turn) {
            if (logged_skips_.insert(streak_id).second) {
                Logger::Warn("[Streak] skipped '", streak_id, "' in turn ",
                             cost_turn_, ": more than ",
                             limits_.cost_max_per_turn,
                             " evaluations (UNI_STREAK_COST_MAX)");
            }
            continue;
        }
        ++evaluations_;
        Apply(runtime, event);
    }
}

nlohmann::json StreakEngine::ValueOf(const std::string& streak_id) const {
    const auto found = runtimes_.find(streak_id);
    if (found == runtimes_.end()) return nlohmann::json();
    const Runtime& runtime = found->second;
    const modload::StreakDef& definition = runtime.definition;
    if (definition.type == keys::kLastValue) {
        return nlohmann::json{{"last", runtime.last},
                              {"previous", runtime.previous}};
    }
    if (definition.type == keys::kTotal && !definition.group_by.empty()) {
        return runtime.groups;
    }
    return runtime.count;
}

nlohmann::json StreakEngine::ContextFor(
    const std::vector<std::string>& streak_ids) const {
    nlohmann::json context = nlohmann::json::object();
    for (const std::string& streak_id : streak_ids) {
        if (!Has(streak_id) || logged_skips_.count(streak_id) != 0) continue;
        nlohmann::json candidate = context;
        candidate[streak_id] = ValueOf(streak_id);
        if (candidate.dump().size() > limits_.context_bytes_max) {
            Logger::Warn("[Streak] left '", streak_id,
                         "' out of the context: more than ",
                         limits_.context_bytes_max,
                         " bytes (UNI_CONTEXT_SIZE_MAX)");
            continue;
        }
        context = std::move(candidate);
    }
    return context.empty() ? nlohmann::json() : context;
}

nlohmann::json StreakEngine::AllValues(const std::string& namespace_id) const {
    nlohmann::json values = nlohmann::json::object();
    for (const auto& [streak_id, runtime] : runtimes_) {
        if (runtime.definition.namespace_id == namespace_id) {
            values[streak_id] = ValueOf(streak_id);
        }
    }
    return values;
}

nlohmann::json StreakEngine::AllValues() const {
    nlohmann::json values = nlohmann::json::object();
    for (const auto& [streak_id, runtime] : runtimes_) {
        values[streak_id] = ValueOf(streak_id);
    }
    return values;
}

uint32_t StreakEngine::EvaluationsInTurn(int64_t turn) const {
    return turn == cost_turn_ ? evaluations_ : 0;
}

}  // namespace match::streak
