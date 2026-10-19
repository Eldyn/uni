#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <match/expr/diagnostic.hpp>
#include <match/expr/parser.hpp>
#include <match/expr/types.hpp>

namespace match::expr {

// INFO: the variables a guard has already bound. A leaf in a condition tree is
// checked with the bindings its earlier siblings guarantee.
struct GuardState {
    std::set<std::string> bound;
};

struct CheckResult {
    std::optional<Type> type;
    std::vector<Diagnostic> diagnostics;
    size_t node_count = 0;
    std::set<std::string> bound_when_true;
    std::set<std::string> bound_when_false;
};

// INFO: parses `source` and type-checks it against `schema`; a parse error
// becomes a diagnostic like any other. `type` stays empty when the check
// failed. `expected` is the declared type of the slot that holds the
// expression; an int expression is accepted where a float is expected, the
// other way round is an error.
CheckResult CheckExpression(std::string_view source, const Schema& schema,
                            std::optional<Type> expected, std::string_view file,
                            std::string_view pointer);

// INFO: like the overload above, but starts from the variables `initial` has
// already bound; the returned `bound_when_true`/`bound_when_false` carry the
// bindings the expression guarantees for the sibling that follows it.
CheckResult CheckExpression(std::string_view source, const Schema& schema,
                            std::optional<Type> expected, std::string_view file,
                            std::string_view pointer, const GuardState& initial);

// INFO: checks an expression that was already parsed. `CheckExpression` parses
// its source and delegates here. The checker records a widening hint on the
// tree itself (`Node::widen_to_float`), so a caller that evaluates the same
// tree sees the checked type of an `if` or `default` branch.
CheckResult CheckParsedExpression(const Node& root, const Schema& schema,
                                  std::optional<Type> expected, std::string_view file,
                                  std::string_view pointer, const GuardState& initial);

// INFO: parses `source` as an assignment and checks it against `schema`. The
// target must be a writable property; a property without a namespace is
// built-in and read-only, and a set-valued selector writes to every member.
// The value must fit the declared type of the property. `initial` supplies the
// variables a preceding guard, such as a `when`, has already bound.
std::vector<Diagnostic> CheckAssignment(std::string_view source, const Schema& schema,
                                        std::string_view file, std::string_view pointer);

std::vector<Diagnostic> CheckAssignment(std::string_view source, const Schema& schema,
                                        std::string_view file, std::string_view pointer,
                                        const GuardState& initial);

}  // namespace match::expr
