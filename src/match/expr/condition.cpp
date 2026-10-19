#include <match/expr/condition.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <match/expr/checker.hpp>
#include <match/expr/evaluator.hpp>
#include <match/expr/parser.hpp>

namespace match::expr {
namespace {

// INFO: the three group keys and the node kind each one builds; every lookup
// and every child pointer reads this one table.
struct GroupKey {
    std::string_view key;
    Condition::Kind kind;
};

constexpr std::array<GroupKey, 3> kGroupKeys = {
    GroupKey{"all_of", Condition::Kind::kAllOf},
    GroupKey{"any_of", Condition::Kind::kAnyOf},
    GroupKey{"none_of", Condition::Kind::kNoneOf},
};

constexpr size_t kMaximumConditionDepth = 64;

std::string_view GroupKeyName(Condition::Kind kind) {
    for (const GroupKey& group : kGroupKeys) {
        if (group.kind == kind) {
            return group.key;
        }
    }
    return "";
}

bool IsGroupKey(std::string_view key) {
    for (const GroupKey& group : kGroupKeys) {
        if (group.key == key) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> GroupKeyNames() {
    std::vector<std::string> names;
    for (const GroupKey& group : kGroupKeys) {
        names.emplace_back(group.key);
    }
    return names;
}

// INFO: a group object holds exactly one of the three keys; return the key it
// holds, or nothing when it holds none or more than one.
std::optional<GroupKey> FindGroup(const json& object) {
    std::optional<GroupKey> found;
    size_t matches = 0;
    for (const GroupKey& group : kGroupKeys) {
        if (object.contains(group.key)) {
            ++matches;
            found = group;
        }
    }
    if (matches == 1) {
        return found;
    }
    return std::nullopt;
}

// INFO: the guard bindings a checked condition node guarantees. A group
// combines its children: an `all_of` is true when every child is, so it unions
// the children's when_true and intersects their when_false; `any_of` is the
// other way round; `none_of` is NOT OR, so its when_true swaps in the
// children's when_false.
struct Narrowed {
    std::set<std::string> when_true;
    std::set<std::string> when_false;
};

std::set<std::string> Union(std::set<std::string> left, const std::set<std::string>& right) {
    left.insert(right.begin(), right.end());
    return left;
}

std::set<std::string> Intersect(std::set<std::string> left,
                                const std::set<std::string>& right) {
    for (auto entry = left.begin(); entry != left.end();) {
        if (right.count(*entry) == 0) {
            entry = left.erase(entry);
        } else {
            ++entry;
        }
    }
    return left;
}

Narrowed CombineGroup(Condition::Kind kind, const std::vector<Narrowed>& children) {
    Narrowed combined;
    bool first = true;
    for (const Narrowed& child : children) {
        if (kind == Condition::Kind::kAllOf) {
            combined.when_true = Union(combined.when_true, child.when_true);
            combined.when_false = first ? child.when_false
                                        : Intersect(combined.when_false, child.when_false);
        } else if (kind == Condition::Kind::kAnyOf) {
            combined.when_true =
                first ? child.when_true : Intersect(combined.when_true, child.when_true);
            combined.when_false = Union(combined.when_false, child.when_false);
        } else {
            combined.when_true = Union(combined.when_true, child.when_false);
            combined.when_false =
                first ? child.when_true : Intersect(combined.when_false, child.when_true);
        }
        first = false;
    }
    return combined;
}

class ConditionParser {
public:
    explicit ConditionParser(std::string_view file) : file_(std::string(file)) {}

    ConditionParseResult Parse(const json& value, const std::string& pointer) {
        std::optional<Condition> condition = ParseNode(value, pointer, 1);
        return ConditionParseResult{std::move(condition), std::move(diagnostics_)};
    }

private:
    void Error(const std::string& pointer, std::string message, std::string suggestion = {}) {
        diagnostics_.push_back(
            Diagnostic{file_, pointer, std::move(message), std::move(suggestion)});
    }

    // INFO: a group object may hold only its one group key; report every other
    // key instead of dropping it silently.
    bool ReportUnknownKeys(const json& object, const std::string& pointer) {
        bool clean = true;
        for (const auto& entry : object.items()) {
            const std::string& key = entry.key();
            if (IsGroupKey(key)) {
                continue;
            }
            clean = false;
            Error(AppendPointerToken(pointer, key), "unknown key \"" + key + "\"",
                  SuggestClosest(key, GroupKeyNames()));
        }
        return clean;
    }

    std::optional<Condition> ParseNode(const json& value, const std::string& pointer,
                                       size_t depth) {
        if (value.is_string()) {
            const std::string source = value.get<std::string>();
            ParseResult parsed = ParseExpression(source);
            if (parsed.error.has_value() || !parsed.root) {
                Error(pointer, parsed.error.has_value() ? FormatParseError(*parsed.error)
                                                       : "condition expression is empty");
                return std::nullopt;
            }
            const size_t node_count = CountNodes(*parsed.root);
            if (node_count > kMaximumExpressionNodes) {
                Error(pointer, "expression has " + std::to_string(node_count) +
                                   " nodes; the limit is " +
                                   std::to_string(kMaximumExpressionNodes));
                return std::nullopt;
            }
            Condition condition{Condition::Kind::kExpression, source, {}};
            condition.expression = std::shared_ptr<const Node>(std::move(parsed.root));
            return condition;
        }
        if (!value.is_object()) {
            Error(pointer, "condition leaves are expression strings");
            return std::nullopt;
        }
        if (depth > kMaximumConditionDepth) {
            Error(pointer, "conditions are nested too deeply (more than " +
                               std::to_string(kMaximumConditionDepth) + " levels)");
            return std::nullopt;
        }
        const std::optional<GroupKey> group = FindGroup(value);
        if (!group) {
            Error(pointer, "a condition object holds exactly one of all_of, any_of, none_of");
            return std::nullopt;
        }
        if (!ReportUnknownKeys(value, pointer)) {
            return std::nullopt;
        }
        const std::string key(group->key);
        const json& children = value[key];
        if (!children.is_array()) {
            Error(AppendPointerToken(pointer, key), key + " must be an array of conditions");
            return std::nullopt;
        }
        if (children.empty()) {
            Error(AppendPointerToken(pointer, key), key + " needs at least one condition");
            return std::nullopt;
        }
        Condition node{group->kind, {}, {}};
        bool ok = true;
        for (size_t index = 0; index < children.size(); ++index) {
            const std::string child_pointer =
                AppendPointerToken(AppendPointerToken(pointer, key), std::to_string(index));
            std::optional<Condition> child = ParseNode(children[index], child_pointer, depth + 1);
            if (child) {
                node.children.push_back(std::move(*child));
            } else {
                ok = false;
            }
        }
        if (!ok) {
            return std::nullopt;
        }
        return node;
    }

    std::string file_;
    std::vector<Diagnostic> diagnostics_;
};

class ConditionChecker {
public:
    ConditionChecker(const Schema& schema, std::string_view file,
                     std::vector<Diagnostic>& diagnostics)
        : schema_(schema), file_(file), diagnostics_(diagnostics) {}

    std::optional<Narrowed> Check(const Condition& condition, const std::string& pointer) {
        return CheckNode(condition, pointer, {}, 1);
    }

private:
    void Error(const std::string& pointer, std::string message) {
        diagnostics_.push_back(Diagnostic{std::string(file_), pointer, std::move(message), ""});
    }

    void Report(const std::vector<Diagnostic>& diagnostics) {
        diagnostics_.insert(diagnostics_.end(), diagnostics.begin(), diagnostics.end());
    }

    std::optional<Narrowed> CheckNode(const Condition& condition, const std::string& pointer,
                                      const std::set<std::string>& guaranteed, size_t depth) {
        if (condition.kind == Condition::Kind::kExpression) {
            return CheckLeaf(condition, pointer, guaranteed);
        }
        if (depth > kMaximumConditionDepth) {
            Error(pointer, "conditions are nested too deeply (more than " +
                               std::to_string(kMaximumConditionDepth) + " levels)");
            return std::nullopt;
        }
        const bool binds_when_true = condition.kind == Condition::Kind::kAllOf;
        std::set<std::string> bound = guaranteed;
        std::vector<Narrowed> checked_children;
        bool ok = true;
        for (size_t index = 0; index < condition.children.size(); ++index) {
            const Condition& child = condition.children[index];
            const std::string child_pointer =
                AppendPointerToken(AppendPointerToken(pointer, GroupKeyName(condition.kind)),
                                   std::to_string(index));
            const std::optional<Narrowed> checked =
                CheckNode(child, child_pointer, bound, depth + 1);
            if (!checked) {
                ok = false;
                continue;
            }
            checked_children.push_back(*checked);
            if (child.kind == Condition::Kind::kExpression) {
                const std::set<std::string>& branch =
                    binds_when_true ? checked->when_true : checked->when_false;
                bound.insert(branch.begin(), branch.end());
            }
        }
        if (!ok) {
            return std::nullopt;
        }
        return CombineGroup(condition.kind, checked_children);
    }

    std::optional<Narrowed> CheckLeaf(const Condition& condition, const std::string& pointer,
                                      const std::set<std::string>& guaranteed) {
        const CheckResult result =
            CheckParsedExpression(*condition.expression, schema_, std::nullopt, file_, pointer,
                                  GuardState{guaranteed});
        if (!result.diagnostics.empty()) {
            Report(result.diagnostics);
            return std::nullopt;
        }
        if (!result.type.has_value()) {
            Error(pointer, "internal checker error");
            return std::nullopt;
        }
        if (*result.type != Type::kBool) {
            Error(pointer, "condition must be bool, got " + TypeName(*result.type));
            return std::nullopt;
        }
        return Narrowed{result.bound_when_true, result.bound_when_false};
    }

    const Schema& schema_;
    std::string_view file_;
    std::vector<Diagnostic>& diagnostics_;
};

// INFO: a group child must yield a bool; an error passes through and any other
// value is a type_mismatch.
EvalResult ExpectConditionBool(EvalResult result) {
    if (std::holds_alternative<EvalError>(result)) {
        return result;
    }
    const bool* flag = std::get_if<bool>(&std::get<Value>(result));
    if (flag == nullptr) {
        return EvalError{"type_mismatch", "condition must be bool"};
    }
    return Value(*flag);
}

}  // namespace

ConditionParseResult ParseCondition(const json& value, std::string_view file,
                                    std::string_view pointer) {
    ConditionParser parser(file);
    return parser.Parse(value, std::string(pointer));
}

ConditionCheckResult CheckCondition(const Condition& condition, const Schema& schema,
                                    std::string_view file, std::string_view pointer) {
    std::vector<Diagnostic> diagnostics;
    ConditionChecker checker(schema, file, diagnostics);
    const std::optional<Narrowed> narrowed = checker.Check(condition, std::string(pointer));
    ConditionCheckResult result;
    result.diagnostics = std::move(diagnostics);
    if (narrowed) {
        result.bound_when_true = narrowed->when_true;
    }
    return result;
}

EvalResult EvaluateCondition(const Condition& condition, EvalContext& context) {
    if (condition.kind == Condition::Kind::kExpression) {
        // INFO: ParseCondition rejects a leaf that does not parse, so the stored
        // expression is always present and EvaluateCondition never re-parses.
        if (!condition.expression) {
            return EvalError{"type_mismatch", "condition leaf is not parsed"};
        }
        // INFO: a root leaf must be a bool just like a group child.
        return ExpectConditionBool(Evaluate(*condition.expression, context));
    }
    if (condition.kind == Condition::Kind::kAllOf) {
        for (const Condition& child : condition.children) {
            const EvalResult result = ExpectConditionBool(EvaluateCondition(child, context));
            if (std::holds_alternative<EvalError>(result)) {
                return result;
            }
            if (!std::get<bool>(std::get<Value>(result))) {
                return Value(false);
            }
        }
        return Value(true);
    }
    if (condition.kind == Condition::Kind::kAnyOf) {
        for (const Condition& child : condition.children) {
            const EvalResult result = ExpectConditionBool(EvaluateCondition(child, context));
            if (std::holds_alternative<EvalError>(result)) {
                return result;
            }
            if (std::get<bool>(std::get<Value>(result))) {
                return Value(true);
            }
        }
        return Value(false);
    }
    // kNoneOf: NOT OR.
    for (const Condition& child : condition.children) {
        const EvalResult result = ExpectConditionBool(EvaluateCondition(child, context));
        if (std::holds_alternative<EvalError>(result)) {
            return result;
        }
        if (std::get<bool>(std::get<Value>(result))) {
            return Value(false);
        }
    }
    return Value(true);
}

}  // namespace match::expr
