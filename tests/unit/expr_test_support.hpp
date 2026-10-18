#pragma once

#include <match/expr/types.hpp>

namespace match::expr::testing {

// INFO: the schema the expression tests share; it is small and hand written so
// a test failure points at the rule under test, not at a fixture.
inline Schema MakeTestSchema() {
    Schema schema;
    schema.seat_properties = {
        {"hand_size", Type::kInt},
        {"seat", Type::kInt},
        {"name", Type::kString},
        {"hand", Type::kCardSet},
        {"axe:stunned", Type::kBool},
    };
    schema.card_properties = {
        {"kind", Type::kString},
    };
    schema.card_slots = {
        {"color", Type::kMemberSet},
        {"value", Type::kMemberSet},
    };
    schema.match_properties = {
        {"turn", Type::kInt},
        {"top", Type::kCard},
        {"tree:growth", Type::kInt},
        {"uni:active_color", Type::kMemberSet},
    };
    schema.settings = {
        {"uni:window_ms", Type::kInt},
    };
    schema.variables = {
        {"played", VariableInfo{Type::kCard, false}},
        {"responder", VariableInfo{Type::kSeat, true}},
        {"choice", VariableInfo{Type::kMemberSet, true}},
        {"rule", VariableInfo{Type::kRule, false}},
    };
    return schema;
}

}  // namespace match::expr::testing
