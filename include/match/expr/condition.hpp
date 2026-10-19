#pragma once

#include <memory>
#include <optional>
#include <set>
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
    // INFO: a leaf's parsed expression, built once by ParseCondition so that
    // EvaluateCondition never re-parses. A leaf that fails to parse is rejected
    // by ParseCondition, so a stored value is always usable.
    std::shared_ptr<const Node> expression;
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
// `bound_when_true` carries the variables the whole tree guarantees to be bound
// when it is true, so a `set` guarded by this `when` can be checked with them:
// all_of is the union of its children, any_of the intersection, none_of the
// union of the variables its children bind when false.
struct ConditionCheckResult {
    std::vector<Diagnostic> diagnostics;
    std::set<std::string> bound_when_true;
};

// INFO: checks the tree; evaluating it is the evaluator's job.
ConditionCheckResult CheckCondition(const Condition& condition, const Schema& schema,
                                    std::string_view file, std::string_view pointer);

// INFO: evaluates a condition tree against a context. all_of is AND, any_of is
// OR, none_of is NOT OR; leaves short-circuit left to right. A leaf error is
// returned as the error.
EvalResult EvaluateCondition(const Condition& condition, EvalContext& context);

}  // namespace match::expr
