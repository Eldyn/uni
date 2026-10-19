#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <match/expr/context.hpp>

namespace match::expr::testing {

// INFO: the write log and the entity names share one spelling so a recorded
// write reads like the property it targeted.
inline std::string EntityText(EntityRef entity) {
    if (entity.kind == Type::kSeat) {
        return "seat " + std::to_string(entity.id);
    }
    return TypeName(entity.kind) + " " + std::to_string(entity.id);
}

inline std::string ValueText(const Value& value) {
    if (const bool* alternative = std::get_if<bool>(&value)) {
        return *alternative ? "true" : "false";
    }
    if (const int64_t* alternative = std::get_if<int64_t>(&value)) {
        return std::to_string(*alternative);
    }
    if (const double* alternative = std::get_if<double>(&value)) {
        return std::to_string(*alternative);
    }
    if (const std::string* alternative = std::get_if<std::string>(&value)) {
        return "'" + *alternative + "'";
    }
    return "value";
}

// INFO: a deterministic EvalContext for the expression tests. Four named seats
// in turn order (Ana, Bo, Cy, Di); self is Di, so next is Ana and prev is Cy.
// `unbound_next` empties @next, `write_log` records every write, and
// `random_queue` scripts the values Random pops. Di's hand holds `hand_cards`;
// `played_card` carries `card_colors` and `card_tags`; the match reads
// `match_colors`; `status_magnitudes` scripts status_magnitude; `rand_entity`
// scripts what @rand resolves to.
class FakeContext : public EvalContext {
public:
    static constexpr EntityRef kAna{Type::kSeat, 0};
    static constexpr EntityRef kBo{Type::kSeat, 1};
    static constexpr EntityRef kCy{Type::kSeat, 2};
    static constexpr EntityRef kDi{Type::kSeat, 3};
    static constexpr EntityRef kMatch{Type::kMatch, 0};
    static constexpr EntityRef kSettings{Type::kSettings, 0};

    std::array<int64_t, 4> hands{5, 2, 4, 1};
    bool unbound_next = false;
    std::vector<std::string> write_log;
    mutable std::vector<int64_t> random_queue;
    std::map<std::string, Value> variables;

    EntityRef played_card{Type::kCard, 0};
    std::vector<EntityRef> hand_cards{played_card};
    std::set<std::string> card_colors{"uni:red", "uni:blue"};
    std::set<std::string> match_colors{"uni:red"};
    std::set<std::string> card_tags{"uni:wild"};
    std::map<std::string, int64_t> status_magnitudes;
    EntityRef rand_entity{kBo};

    std::vector<EntityRef> ResolveSelector(std::string_view name) const override {
        if (name == "self") {
            return {kDi};
        }
        if (name == "next") {
            // INFO: the `unbound` switch; an empty result means "none".
            return unbound_next ? std::vector<EntityRef>{} : std::vector<EntityRef>{kAna};
        }
        if (name == "prev") {
            return {kCy};
        }
        if (name == "rand") {
            return {rand_entity};
        }
        if (name == "all") {
            return {kAna, kBo, kCy, kDi};
        }
        if (name == "others") {
            return {kAna, kBo, kCy};
        }
        if (name == "match") {
            return {kMatch};
        }
        if (name == "settings") {
            return {kSettings};
        }
        return {};
    }

    std::optional<Value> ReadProperty(EntityRef entity, std::string_view key) const override {
        if (entity.kind == Type::kSeat) {
            if (key == "hand_size" && entity.id < hands.size()) {
                return Value(hands[entity.id]);
            }
            if (key == "hand" && entity == kDi) {
                return Value(hand_cards);
            }
        }
        if (entity.kind == Type::kCard && entity == played_card && key == "color") {
            return Value(card_colors);
        }
        if (entity.kind == Type::kMatch && key == "uni:active_color") {
            return Value(match_colors);
        }
        return std::nullopt;
    }

    bool WriteProperty(EntityRef entity, std::string_view key, const Value& value) override {
        write_log.push_back(std::string(key) + " = " + ValueText(value) + " on " +
                            EntityText(entity));
        if (entity.kind == Type::kSeat && key == "hand_size" && entity.id < hands.size() &&
            std::holds_alternative<int64_t>(value)) {
            hands[entity.id] = std::get<int64_t>(value);
        }
        return true;
    }

    std::optional<Value> GetVariable(std::string_view name) const override {
        const auto found = variables.find(std::string(name));
        if (found == variables.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    bool HasTag(EntityRef entity, std::string_view tag) const override {
        if (entity != played_card) {
            return false;
        }
        return card_tags.count(std::string(tag)) > 0;
    }

    int64_t Random(int64_t low, int64_t /*high*/) override { return NextRandom(low); }

    int64_t StatusMagnitude(EntityRef /*seat*/, std::string_view status) const override {
        const auto found = status_magnitudes.find(std::string(status));
        if (found == status_magnitudes.end()) {
            return 0;
        }
        return found->second;
    }

private:
    // INFO: pops the scripted value, or `fallback` once the queue runs dry.
    int64_t NextRandom(int64_t fallback) const {
        if (random_queue.empty()) {
            return fallback;
        }
        const int64_t value = random_queue.front();
        random_queue.erase(random_queue.begin());
        return value;
    }
};

}  // namespace match::expr::testing
