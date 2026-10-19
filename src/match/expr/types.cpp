#include <match/expr/types.hpp>

namespace match::expr {

std::string TypeName(Type type) {
    switch (type) {
        case Type::kBool:
            return "bool";
        case Type::kInt:
            return "int";
        case Type::kFloat:
            return "float";
        case Type::kString:
            return "string";
        case Type::kMemberSet:
            return "member_set";
        case Type::kSeat:
            return "seat";
        case Type::kSeatSet:
            return "seat_set";
        case Type::kCard:
            return "card";
        case Type::kCardSet:
            return "card_set";
        case Type::kMatch:
            return "match";
        case Type::kSettings:
            return "settings";
        case Type::kRule:
            return "rule";
    }
    return "unknown";
}

std::optional<Type> SelectorType(std::string_view selector) {
    if (selector == "self" || selector == "next" || selector == "prev" || selector == "rand" ||
        selector == "rand_all") {
        return Type::kSeat;
    }
    if (selector == "all" || selector == "others") {
        return Type::kSeatSet;
    }
    if (selector == "match") {
        return Type::kMatch;
    }
    if (selector == "settings") {
        return Type::kSettings;
    }
    return std::nullopt;
}

}  // namespace match::expr
