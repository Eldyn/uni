#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
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
// `random_queue` scripts the values Random pops.
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
    std::map<std::string, Value> vars;
    std::map<std::string, Value> bound_variables;

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
            const EntityRef choices[] = {kAna, kBo, kCy};
            return {choices[ClampIndex(NextRandom(0), 3)]};
        }
        if (name == "rand_all") {
            const EntityRef choices[] = {kAna, kBo, kCy, kDi};
            return {choices[ClampIndex(NextRandom(0), 4)]};
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
        if (entity.kind == Type::kSeat && key == "hand_size" && entity.id < hands.size()) {
            return Value(hands[entity.id]);
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
        const std::string key(name);
        auto lookup = [&](const std::map<std::string, Value>& store) {
            const auto found = store.find(key);
            if (found == store.end()) {
                return std::optional<Value>{};
            }
            return std::optional<Value>{found->second};
        };
        if (auto value = lookup(variables)) {
            return value;
        }
        if (auto value = lookup(vars)) {
            return value;
        }
        if (auto value = lookup(bound_variables)) {
            return value;
        }
        return std::nullopt;
    }

    bool HasTag(EntityRef /*entity*/, std::string_view /*tag*/) const override { return false; }

    int64_t Random(int64_t low, int64_t /*high*/) override { return NextRandom(low); }

    int64_t StatusMagnitude(EntityRef /*seat*/, std::string_view /*status*/) const override {
        return 0;
    }

private:
    // INFO: pops the scripted value, or `fallback` once the queue runs dry; a
    // const selector resolution reaches the RNG through this helper.
    int64_t NextRandom(int64_t fallback) const {
        if (random_queue.empty()) {
            return fallback;
        }
        const int64_t value = random_queue.front();
        random_queue.erase(random_queue.begin());
        return value;
    }

    static size_t ClampIndex(int64_t value, size_t size) {
        if (value < 0) {
            return 0;
        }
        if (value >= static_cast<int64_t>(size)) {
            return size - 1;
        }
        return static_cast<size_t>(value);
    }
};

}  // namespace match::expr::testing
