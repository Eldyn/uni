#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <match/expr/diagnostic.hpp>
#include <match/expr/evaluator.hpp>
#include <match/expr/types.hpp>

namespace match::expr {

using json = nlohmann::json;

struct Condition {
    enum class Kind { kExpression, kAllOf, kAnyOf, kNoneOf };
    Kind kind = Kind::kExpression;
    std::string source;
    std::vector<Condition> children;
};

struct ConditionParseResult {
    std::optional<Condition> condition;
    std::vector<Diagnostic> diagnostics;
};

// INFO: `value` is a leaf expression string, or an object that holds exactly
// one of `all_of`, `any_of`, `none_of` with a non-empty array of conditions.
// Nested groups are parsed recursively; a parse error leaves `condition`
// empty.
ConditionParseResult ParseCondition(const json& value, std::string_view file,
                                    std::string_view pointer);

// INFO: every leaf must check as a bool. A leaf is checked with the variables
// the earlier siblings guarantee: in `all_of` the variables the leaves before
// it bind when true, in `any_of` and `none_of` the ones they bind when false.
// Evaluating the tree is the evaluator's job.
std::vector<Diagnostic> CheckCondition(const Condition& condition, const Schema& schema,
                                       std::string_view file, std::string_view pointer);

// INFO: evaluates a condition tree against a context. all_of is AND, any_of is
// OR, none_of is NOT OR; leaves short-circuit left to right. A leaf error is
// returned as the error.
EvalResult EvaluateCondition(const Condition& condition, EvalContext& context);

}  // namespace match::expr
