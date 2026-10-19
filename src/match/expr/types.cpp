#include <match/expr/types.hpp>

namespace match::expr {

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
