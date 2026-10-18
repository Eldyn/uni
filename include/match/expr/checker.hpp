#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <match/expr/diagnostic.hpp>
#include <match/expr/types.hpp>

namespace match::expr {

struct CheckResult {
    std::optional<Type> type;
    std::vector<Diagnostic> diagnostics;
    size_t node_count;
};

// INFO: parses `source` and type-checks it against `schema`; a parse error
// becomes a diagnostic like any other. `type` stays empty when the check
// failed. `expected` is the declared type of the slot that holds the
// expression; an int expression is accepted where a float is expected, the
// other way round is an error.
CheckResult CheckExpression(std::string_view source, const Schema& schema,
                            std::optional<Type> expected, std::string_view file,
                            std::string_view pointer);

}  // namespace match::expr
