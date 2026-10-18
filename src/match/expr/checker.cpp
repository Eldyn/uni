#include <match/expr/checker.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <match/expr/parser.hpp>

namespace match::expr {
namespace {

constexpr size_t kMaximumNodes = 256;

// INFO: inside `[ ]` and inside the condition argument of a set function a bare
// name is a property of the candidate; the candidate entity is only meaningful
// in that scope.
enum class Scope { kRoot, kCandidate };

bool IsNumber(Type type) {
    return type == Type::kInt || type == Type::kFloat;
}

bool IsSet(Type type) {
    return type == Type::kSeatSet || type == Type::kCardSet || type == Type::kMemberSet;
}

Type EntityOf(Type type) {
    if (type == Type::kSeatSet) {
        return Type::kSeat;
    }
    if (type == Type::kCardSet) {
        return Type::kCard;
    }
    return type;
}

std::string TypeName(Type type) {
    switch (type) {
        case Type::kBool:
            return "bool";
        case Type::kInt:
            return "int";
        case Type::kFloat:
            return "float";
        case Type::kString:
            return "string";
        case Type::kMemberSet:
            return "member_set";
        case Type::kSeat:
            return "seat";
        case Type::kSeatSet:
            return "seat_set";
        case Type::kCard:
            return "card";
        case Type::kCardSet:
            return "card_set";
        case Type::kMatch:
            return "match";
        case Type::kSettings:
            return "settings";
        case Type::kRule:
            return "rule";
    }
    return "unknown";
}

// INFO: a set has no single value to read; the message names the aggregations
// that work on it.
std::string SetReadMessage(Type set_type) {
    if (set_type == Type::kSeatSet) {
        return "selects many seats; use count(), any(), all(), sum() or a single-seat "
               "selector";
    }
    if (set_type == Type::kCardSet) {
        return "selects many cards; use count(), any(), all(), sum() or a single-card "
               "selector";
    }
    return "reading a property of a member set is not allowed; use count(), any(), all(), "
           "sum(), contains() or shares()";
}

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

const std::vector<std::string>& SelectorNames() {
    static const std::vector<std::string> names = {"self",    "next",    "prev",
                                                   "rand_all", "rand",    "all",
                                                   "others",  "match",   "settings"};
    return names;
}

std::vector<std::string> PropertyNames(const Schema& schema, Type entity) {
    std::vector<std::string> names;
    const std::map<std::string, Type>* properties = schema.Properties(entity);
    if (properties == nullptr) {
        return names;
    }
    for (const auto& entry : *properties) {
        names.push_back(entry.first);
    }
    if (entity == Type::kCard) {
        for (const auto& entry : schema.card_slots) {
            names.push_back(entry.first);
        }
    }
    return names;
}

std::vector<std::string> VariableNames(const Schema& schema) {
    std::vector<std::string> names;
    for (const auto& entry : schema.variables) {
        names.push_back(entry.first);
    }
    return names;
}

std::optional<Type> LiteralType(const Node& node) {
    if (std::holds_alternative<bool>(node.literal)) {
        return Type::kBool;
    }
    if (std::holds_alternative<int64_t>(node.literal)) {
        return Type::kInt;
    }
    if (std::holds_alternative<double>(node.literal)) {
        return Type::kFloat;
    }
    return Type::kString;
}

bool IsZeroLiteral(const Node& node) {
    if (node.kind != NodeKind::kLiteral) {
        return false;
    }
    if (const auto* value = std::get_if<int64_t>(&node.literal)) {
        return *value == 0;
    }
    if (const auto* value = std::get_if<double>(&node.literal)) {
        return *value == 0.0;
    }
    return false;
}

Type Widen(Type left, Type right) {
    return left == Type::kFloat || right == Type::kFloat ? Type::kFloat : Type::kInt;
}

// INFO: there is no implicit float to int, an int widens to a float.
bool Accepts(Type expected, Type actual) {
    if (expected == actual) {
        return true;
    }
    return expected == Type::kFloat && IsNumber(actual);
}

std::string ExpectedMessage(Type expected, Type actual) {
    if (expected == Type::kInt && actual == Type::kFloat) {
        return "expected int, got float (use floor(), ceil() or round())";
    }
    return "expected " + TypeName(expected) + ", got " + TypeName(actual);
}

class Checker {
public:
    Checker(const Schema& schema, std::optional<Type> expected, std::string_view file,
            std::string_view pointer)
        : schema_(schema), expected_(expected), file_(file), pointer_(pointer) {}

    std::optional<Type> CheckRoot(const Node& node) {
        std::optional<Type> type = Check(node, Scope::kRoot, Type::kSeat);
        if (!type || !expected_) {
            return type;
        }
        if (!Accepts(*expected_, *type)) {
            Error(ExpectedMessage(*expected_, *type));
            return std::nullopt;
        }
        return type;
    }

    bool failed() const { return !diagnostics_.empty(); }

    std::vector<Diagnostic> TakeDiagnostics() { return std::move(diagnostics_); }

private:
    void Error(std::string message, std::string suggestion = {}) {
        diagnostics_.push_back(Diagnostic{std::string(file_), std::string(pointer_),
                                          std::move(message), std::move(suggestion)});
    }

    std::optional<Type> Check(const Node& node, Scope scope, Type candidate) {
        switch (node.kind) {
            case NodeKind::kLiteral:
                return LiteralType(node);
            case NodeKind::kSelector:
                return CheckSelector(node);
            case NodeKind::kVariable:
                return CheckVariable(node);
            case NodeKind::kProperty:
                return CheckProperty(node, scope, candidate);
            case NodeKind::kBareName:
                return CheckBareName(node, scope, candidate);
            case NodeKind::kUnary:
                return CheckUnary(node, scope, candidate);
            case NodeKind::kBinary:
                return CheckBinary(node, scope, candidate);
            case NodeKind::kCall:
                Error("unknown function \"" + node.text + "\"");
                return std::nullopt;
        }
        Error("unsupported expression");
        return std::nullopt;
    }

    std::optional<Type> CheckSelector(const Node& node) {
        const std::optional<Type> type = SelectorType(node.text);
        if (!type) {
            Error("unknown selector @" + node.text,
                  SuggestClosest(node.text, SelectorNames()));
            return std::nullopt;
        }
        for (const NodePtr& filter : node.children) {
            if (!CheckCondition(*filter, EntityOf(*type))) {
                return std::nullopt;
            }
        }
        return type;
    }

    // INFO: a filter and the condition argument of a set function are both
    // boolean predicates over the candidate, so they share one rule.
    std::optional<Type> CheckCondition(const Node& node, Type candidate) {
        const std::optional<Type> type = Check(node, Scope::kCandidate, candidate);
        if (!type) {
            return std::nullopt;
        }
        if (*type != Type::kBool) {
            Error("filter must be a boolean condition, got " + TypeName(*type));
            return std::nullopt;
        }
        return type;
    }

    std::optional<Type> CheckVariable(const Node& node) {
        const auto variable = schema_.variables.find(node.text);
        if (variable == schema_.variables.end()) {
            Error("unknown variable $" + node.text,
                  SuggestClosest(node.text, VariableNames(schema_)));
            return std::nullopt;
        }
        return variable->second.type;
    }

    std::optional<Type> CheckProperty(const Node& node, Scope scope, Type candidate) {
        const std::optional<Type> base = Check(*node.children[0], scope, candidate);
        if (!base) {
            return std::nullopt;
        }
        if (IsSet(*base)) {
            Error(SetReadMessage(*base));
            return std::nullopt;
        }
        if (*base == Type::kRule) {
            Error("cannot read properties of a rule");
            return std::nullopt;
        }
        if (schema_.Properties(*base) == nullptr) {
            Error("cannot read \"" + node.text + "\" on a value of type " + TypeName(*base));
            return std::nullopt;
        }
        return LookupProperty(*base, node.text);
    }

    std::optional<Type> LookupProperty(Type entity, const std::string& key) {
        const std::optional<Type> type = schema_.PropertyType(entity, key);
        if (!type) {
            Error("unknown property \"" + key + "\" on " + TypeName(entity),
                  SuggestClosest(key, PropertyNames(schema_, entity)));
            return std::nullopt;
        }
        return type;
    }

    std::optional<Type> CheckBareName(const Node& node, Scope scope, Type candidate) {
        if (scope != Scope::kCandidate) {
            Error("bare name '" + node.text +
                  "' is only valid inside [ ] or a set function condition");
            return std::nullopt;
        }
        return LookupProperty(candidate, node.text);
    }

    std::optional<Type> CheckUnary(const Node& node, Scope scope, Type candidate) {
        const std::optional<Type> operand = Check(*node.children[0], scope, candidate);
        if (!operand) {
            return std::nullopt;
        }
        if (node.text == "-") {
            if (!IsNumber(*operand)) {
                Error("operator - needs numbers");
                return std::nullopt;
            }
            return operand;
        }
        if (*operand != Type::kBool) {
            Error("operator ! needs a boolean");
            return std::nullopt;
        }
        return Type::kBool;
    }

    std::optional<Type> CheckBinary(const Node& node, Scope scope, Type candidate) {
        const std::optional<Type> left = Check(*node.children[0], scope, candidate);
        if (!left) {
            return std::nullopt;
        }
        const std::optional<Type> right = Check(*node.children[1], scope, candidate);
        if (!right) {
            return std::nullopt;
        }
        const std::string& symbol = node.text;

        if (symbol == "+" || symbol == "-" || symbol == "*" || symbol == "/") {
            if (!IsNumber(*left) || !IsNumber(*right)) {
                Error("operator " + symbol + " needs numbers");
                return std::nullopt;
            }
            if (symbol == "/" && IsZeroLiteral(*node.children[1])) {
                Error("division by zero");
                return std::nullopt;
            }
            return Widen(*left, *right);
        }
        if (symbol == "%") {
            if (*left != Type::kInt || *right != Type::kInt) {
                Error("operator % needs ints");
                return std::nullopt;
            }
            if (IsZeroLiteral(*node.children[1])) {
                Error("division by zero");
                return std::nullopt;
            }
            return Type::kInt;
        }
        if (symbol == "==" || symbol == "!=") {
            // INFO: numbers compare across int and float, so only a difference
            // between two other types is a mismatch.
            if (*left != *right && (!IsNumber(*left) || !IsNumber(*right))) {
                Error("operator " + symbol + " cannot compare " + TypeName(*left) +
                      " with " + TypeName(*right));
                return std::nullopt;
            }
            return Type::kBool;
        }
        if (symbol == "<" || symbol == "<=" || symbol == ">" || symbol == ">=") {
            if (!IsNumber(*left) || !IsNumber(*right)) {
                if (IsSet(*left) || IsSet(*right)) {
                    Error("operator " + symbol + " needs numbers");
                } else {
                    Error("cannot compare " + TypeName(*left) + " with " + TypeName(*right));
                }
                return std::nullopt;
            }
            return Type::kBool;
        }
        if (symbol == "&&" || symbol == "||") {
            if (*left != Type::kBool || *right != Type::kBool) {
                Error("operator " + symbol + " needs booleans");
                return std::nullopt;
            }
            return Type::kBool;
        }
        Error("unsupported operator " + symbol);
        return std::nullopt;
    }

    const Schema& schema_;
    std::optional<Type> expected_;
    std::string_view file_;
    std::string_view pointer_;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace

CheckResult CheckExpression(std::string_view source, const Schema& schema,
                            std::optional<Type> expected, std::string_view file,
                            std::string_view pointer) {
    CheckResult result;
    const ParseResult parsed = ParseExpression(source);
    if (parsed.error) {
        result.diagnostics.push_back(
            Diagnostic{std::string(file), std::string(pointer), parsed.error->message, ""});
        return result;
    }
    result.node_count = CountNodes(*parsed.root);
    if (result.node_count > kMaximumNodes) {
        result.diagnostics.push_back(
            Diagnostic{std::string(file), std::string(pointer),
                      "expression has " + std::to_string(result.node_count) +
                          " nodes; the limit is " + std::to_string(kMaximumNodes),
                      ""});
        return result;
    }

    Checker checker(schema, expected, file, pointer);
    const std::optional<Type> type = checker.CheckRoot(*parsed.root);
    result.type = type;
    result.diagnostics = checker.TakeDiagnostics();
    return result;
}

}  // namespace match::expr
