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

    // INFO: the property table of an entity, or nullptr for a value that
    // carries no properties of its own (a rule, a number, a set).
    const std::map<std::string, Type>* Properties(Type entity) const {
        switch (entity) {
            case Type::kSeat:
                return &seat_properties;
            case Type::kCard:
                return &card_properties;
            case Type::kMatch:
                return &match_properties;
            case Type::kSettings:
                return &settings;
            default:
                return nullptr;
        }
    }

    // INFO: a card carries its slots next to its properties; every other entity
    // only has the one map.
    std::optional<Type> PropertyType(Type entity, std::string_view key) const {
        const std::map<std::string, Type>* properties = Properties(entity);
        if (properties == nullptr) {
            return std::nullopt;
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
