#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

// INFO: the user-facing spelling of a type, shared by every diagnostic that
// names one.
std::string TypeName(Type type);

// INFO: the static type a selector name denotes, or nullopt for an unknown
// name. A set selector (a seat set) always evaluates to a vector of entities; a
// single selector (a seat, the match or the settings) evaluates to one entity.
std::optional<Type> SelectorType(std::string_view selector);

// INFO: the type names a schema may declare, in one table so the schema loader
// and the loader's suggestions cannot drift apart.
inline constexpr std::pair<std::string_view, Type> kTypeNames[] = {
    {"bool", Type::kBool},     {"int", Type::kInt},
    {"float", Type::kFloat},   {"string", Type::kString},
    {"member_set", Type::kMemberSet}, {"seat", Type::kSeat},
    {"seat_set", Type::kSeatSet}, {"card", Type::kCard},
    {"card_set", Type::kCardSet},
};

// INFO: the type a schema type name denotes, or nullopt for an unknown name.
inline std::optional<Type> TypeFromName(std::string_view name) {
    for (const auto& [text, type] : kTypeNames) {
        if (text == name) {
            return type;
        }
    }
    return std::nullopt;
}

}  // namespace match::expr
