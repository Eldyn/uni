#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace match::expr {

enum class Type {
    kBool,
    kInt,
    kFloat,
    kString,
    kMemberSet,
    kSeat,
    kSeatSet,
    kCard,
    kCardSet,
    kMatch,
    kSettings,
    kRule,
};

struct VariableInfo {
    Type type;
    bool optional;
};

struct Schema {
    std::map<std::string, Type> seat_properties, card_properties, match_properties, settings;
    std::map<std::string, Type> card_slots;
    std::map<std::string, VariableInfo> variables;

    // INFO: a card carries its slots next to its properties; every other entity
    // only has the one map.
    std::optional<Type> PropertyType(Type entity, std::string_view key) const {
        const std::map<std::string, Type>* properties = &seat_properties;
        if (entity == Type::kCard) {
            properties = &card_properties;
        } else if (entity == Type::kMatch) {
            properties = &match_properties;
        } else if (entity == Type::kSettings) {
            properties = &settings;
        }
        const auto property = properties->find(std::string(key));
        if (property != properties->end()) {
            return property->second;
        }
        if (entity == Type::kCard) {
            const auto slot = card_slots.find(std::string(key));
            if (slot != card_slots.end()) {
                return slot->second;
            }
        }
        return std::nullopt;
    }
};

}  // namespace match::expr
