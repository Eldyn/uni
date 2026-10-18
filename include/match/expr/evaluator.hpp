#pragma once

#include <string>
#include <variant>

#include <match/expr/ast.hpp>
#include <match/expr/context.hpp>
#include <match/expr/parser.hpp>
#include <match/expr/value.hpp>

namespace match::expr {

// INFO: a runtime error; `code` is stable so the mod logs and the verifier can
// match on it. The codes are: division_by_zero, not_a_number, integer_overflow,
// empty_selector, unbound_variable, unknown_property, type_mismatch.
struct EvalError {
    std::string code;
    std::string message;
};

using EvalResult = std::variant<Value, EvalError>;

// INFO: evaluates `root` against `context`. Set selectors, filters and the set
// functions land in a later task; until then they report type_mismatch.
EvalResult Evaluate(const Node& root, EvalContext& context);

}  // namespace match::expr
