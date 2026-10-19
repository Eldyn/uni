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
// invalid_range, expression_too_large, type_mismatch, write_failed.
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
// the target selects. Every new value is computed before any write, so an error
// on any entity stops the step with no partial writes. An empty selector is
// `empty_selector` and writes nothing. A `WriteProperty` that returns false is
// `write_failed`; because the writes cannot be undone, a context must not fail
// a write it accepted in `ReadProperty`.
std::optional<EvalError> ApplyAssignment(const Assignment& assignment, EvalContext& context);

// INFO: the property key an assignment writes. A target with a filter still
// writes a single property of its candidates.
std::vector<std::string> CollectWrittenKeys(const Assignment& assignment);

}  // namespace match::expr
