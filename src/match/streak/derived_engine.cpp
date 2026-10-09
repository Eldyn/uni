#include <match/streak/derived_engine.hpp>

#include <match/modload/derived_definition_keys.hpp>

#include <logger.hpp>

namespace match::streak {

namespace {

namespace keys = modload::derived_keys;

bool NamesEvent(const std::vector<modload::DerivedSeatMatcherDef>& list,
                const std::string& event_id) {
    for (const modload::DerivedSeatMatcherDef& entry : list) {
        if (entry.matcher.event == event_id) return true;
    }
    return false;
}

/** @brief The seat of the first entry that matches; empty when none does. */
std::string MatchedSeat(
    const std::vector<modload::DerivedSeatMatcherDef>& list,
    const ClientEventView& event) {
    for (const modload::DerivedSeatMatcherDef& entry : list) {
        if (!MatcherMatches(entry.matcher, event)) continue;
        const auto seat = event.payload.find(entry.seat_field);
        if (seat != event.payload.end() && seat->is_string()) {
            return seat->get<std::string>();
        }
    }
    return std::string();
}

}  // namespace

void DerivedEngine::SetDefinitions(
    const modload::DerivedRegistry& definitions) {
    definitions_.clear();
    windows_.clear();
    last_actor_.clear();
    logged_skips_.clear();
    cost_turn_ = -1;
    for (const auto& [derived_id, definition] : definitions) {
        definitions_[derived_id] = definition;
        if (definition.kind == keys::kWindowKind) {
            windows_[derived_id] = WindowState{};
        }
    }
}

std::vector<Derivation> DerivedEngine::Evaluate(const ClientEventView& event,
                                                const ChargeFunction& charge) {
    std::vector<Derivation> derivations;
    if (event.derived || definitions_.empty()) return derivations;

    if (event.state.turn != cost_turn_) {
        cost_turn_ = event.state.turn;
        logged_skips_.clear();
    }
    std::vector<const modload::DerivedEventDef*> allowed;
    for (const auto& [derived_id, definition] : definitions_) {
        const bool relevant = NamesEvent(definition.open_on, event.id)
                              || NamesEvent(definition.close_on, event.id)
                              || NamesEvent(definition.block_on, event.id);
        if (!relevant) continue;
        if (!charge(event.state.turn, derived_id)) {
            if (logged_skips_.insert(derived_id).second) {
                Logger::Warn("[Derived] skipped '", derived_id, "' in turn ",
                             event.state.turn,
                             ": more than the evaluation limit "
                             "(UNI_STREAK_COST_MAX)");
            }
            continue;
        }
        allowed.push_back(&definition);
    }

    const std::string actor = event.actor.empty() ? last_actor_ : event.actor;

    // INFO: the blocks read the windows as they were BEFORE this event. The
    //       window passes below change them.
    for (const modload::DerivedEventDef* definition : allowed) {
        if (definition->kind != keys::kBlockKind) continue;
        const auto window = windows_.find(definition->window_of);
        if (window == windows_.end() || !window->second.open) continue;
        const std::string seat = MatchedSeat(definition->block_on, event);
        if (seat.empty() || seat != window->second.seat) continue;
        derivations.push_back(Derivation{definition->emits, seat, actor});
    }

    for (const modload::DerivedEventDef* definition : allowed) {
        if (definition->kind != keys::kWindowKind) continue;
        WindowState& window = windows_[definition->derived_id];
        const std::string closing = MatchedSeat(definition->close_on, event);
        if (window.open && !closing.empty() && closing == window.seat) {
            window.open = false;
        }
        const std::string opening = MatchedSeat(definition->open_on, event);
        if (opening.empty()) continue;
        window.seat = opening;
        window.open = true;
        derivations.push_back(Derivation{definition->emits, opening, actor});
    }

    if (!event.actor.empty()) last_actor_ = event.actor;
    return derivations;
}

WindowState DerivedEngine::WindowFor(const std::string& event_id) const {
    for (const auto& [derived_id, definition] : definitions_) {
        if (definition.kind == keys::kWindowKind
            && definition.emits == event_id) {
            return windows_.at(derived_id);
        }
    }
    return WindowState{};
}

nlohmann::json DerivedEngine::WindowsContext() const {
    nlohmann::json context = nlohmann::json::object();
    for (const auto& [derived_id, definition] : definitions_) {
        if (definition.kind != keys::kWindowKind) continue;
        const WindowState& window = windows_.at(derived_id);
        context[definition.emits] = nlohmann::json{
            {"target", window.seat.empty() ? nlohmann::json()
                                           : nlohmann::json(window.seat)},
            {"open", window.open}};
    }
    return context.empty() ? nlohmann::json() : context;
}

}  // namespace match::streak
