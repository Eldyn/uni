#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <match/expr/ast.hpp>
#include <match/expr/context.hpp>
#include <match/expr/parser.hpp>
#include <match/expr/value.hpp>

namespace match::expr {

// INFO: a runtime error; `code` is stable so the mod logs and the verifier can
// match on it. The codes are: division_by_zero, not_a_number, integer_overflow,
// float_overflow, empty_selector, unbound_variable, unknown_property,
// invalid_range, type_mismatch.
struct EvalError {
    std::string code;
    std::string message;
};

using EvalResult = std::variant<Value, EvalError>;

// INFO: evaluates `root` against `context`. Set selectors, filters and the set
// functions land in a later task; until then they report type_mismatch.
EvalResult Evaluate(const Node& root, EvalContext& context);

// INFO: applies `assignment` to `context`. For `+= -= *=` it reads the
// current value, applies the operator, then writes the result to every entity
// the target selects. An empty selector is `empty_selector` and writes
// nothing.
std::optional<EvalError> ApplyAssignment(const Assignment& assignment, EvalContext& context);

// INFO: the property keys an assignment writes, deduplicated. A target with a
// filter still writes a single property of its candidates.
std::vector<std::string> CollectWrittenKeys(const Assignment& assignment);

}  // namespace match::expr
