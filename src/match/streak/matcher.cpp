#include <match/streak/matcher.hpp>

#include <match/modload/streak_definition_keys.hpp>
#include <match/streak/state_values.hpp>

#include <algorithm>

namespace match::streak {

namespace {

namespace keys = modload::streak_keys;

/** @brief The value a test reads; nullopt when it cannot be read. */
std::optional<nlohmann::json> ReadSubject(const modload::StreakTestDef& test,
                                          const ClientEventView& event) {
    if (!test.state.empty()) {
        if (test.state == kStateTurn) return nlohmann::json(event.state.turn);
        if (test.state == kStateRound) {
            return nlohmann::json(event.state.round);
        }
        if (test.state == kStateHandSize && event.state.hand_size.has_value()) {
            return nlohmann::json(*event.state.hand_size);
        }
        return std::nullopt;
    }
    const auto found = event.payload.find(test.field);
    if (found == event.payload.end()) return nlohmann::json();
    return *found;
}

bool PayloadHasField(const modload::StreakTestDef& test,
                     const ClientEventView& event) {
    if (!test.state.empty()) return ReadSubject(test, event).has_value();
    return event.payload.contains(test.field);
}

/** @brief The result of one test; nullopt when the test cannot decide. */
std::optional<bool> EvaluateTest(const modload::StreakTestDef& test,
                                 const ClientEventView& event) {
    if (test.test == keys::kHasTag) {
        const auto tags = event.payload.find(std::string(keys::kTagsField));
        if (tags == event.payload.end() || !tags->is_array()) return false;
        return std::find(tags->begin(), tags->end(), nlohmann::json(test.tag))
               != tags->end();
    }
    if (test.test == keys::kExists) return PayloadHasField(test, event);
    if (test.test != keys::kEquals && test.test != keys::kIn) {
        return std::nullopt;
    }
    const std::optional<nlohmann::json> subject = ReadSubject(test, event);
    if (!subject.has_value()) return std::nullopt;
    if (test.test == keys::kEquals) {
        return PayloadHasField(test, event) && *subject == test.value;
    }
    return PayloadHasField(test, event)
           && std::find(test.values.begin(), test.values.end(), *subject)
                  != test.values.end();
}

}  // namespace

bool MatcherMatches(const modload::StreakMatcherDef& matcher,
                    const ClientEventView& event) {
    if (matcher.event != event.id) return false;
    for (const modload::StreakTestDef& test : matcher.tests) {
        const std::optional<bool> result = EvaluateTest(test, event);
        if (!result.has_value()) return false;
        if (*result == test.negate) return false;
    }
    return true;
}

}  // namespace match::streak
