#include <match/expr/evaluator.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace match::expr {
namespace {

EvalResult Fail(std::string code, std::string message) {
    return EvalError{std::move(code), std::move(message)};
}

EvalResult EvaluateNode(const Node& node, EvalContext& context);

bool IsNumber(const Value& value) {
    return std::holds_alternative<int64_t>(value) || std::holds_alternative<double>(value);
}

bool IsFloat(const Value& value) { return std::holds_alternative<double>(value); }

// INFO: an int widens to a float; there is no implicit float to int.
double AsFloat(const Value& value) {
    return IsFloat(value) ? std::get<double>(value)
                          : static_cast<double>(std::get<int64_t>(value));
}

std::string ValueName(const Value& value) {
    if (const EntityRef* entity = std::get_if<EntityRef>(&value)) {
        return TypeName(entity->kind);
    }
    if (std::holds_alternative<bool>(value)) {
        return "bool";
    }
    if (std::holds_alternative<int64_t>(value)) {
        return "int";
    }
    if (IsFloat(value)) {
        return "float";
    }
    if (std::holds_alternative<std::string>(value)) {
        return "string";
    }
    if (std::holds_alternative<std::set<std::string>>(value)) {
        return "member_set";
    }
    return "set";
}

// INFO: every int operation checks overflow so the result is an error, never
// wrapped or undefined behavior.
std::optional<int64_t> Plus(int64_t left, int64_t right) {
    int64_t result = 0;
    if (__builtin_add_overflow(left, right, &result)) {
        return std::nullopt;
    }
    return result;
}

std::optional<int64_t> Minus(int64_t left, int64_t right) {
    int64_t result = 0;
    if (__builtin_sub_overflow(left, right, &result)) {
        return std::nullopt;
    }
    return result;
}

std::optional<int64_t> Times(int64_t left, int64_t right) {
    int64_t result = 0;
    if (__builtin_mul_overflow(left, right, &result)) {
        return std::nullopt;
    }
    return result;
}

std::optional<int64_t> Negate(int64_t value) { return Minus(0, value); }

// INFO: every float result is checked: NaN is not_a_number, an infinity is a
// float overflow. Applied to each float-producing operation.
EvalResult CheckedFloat(double value) {
    if (std::isnan(value)) {
        return Fail("not_a_number", "arithmetic produced not a number");
    }
    if (std::isinf(value)) {
        return Fail("float_overflow", "float overflow");
    }
    return Value(value);
}

EvalResult LiteralValue(const Node& node) {
    return std::visit([](const auto& alternative) { return Value(alternative); }, node.literal);
}

EvalResult VariableValue(const Node& node, EvalContext& context) {
    const std::optional<Value> variable = context.GetVariable(node.text);
    if (!variable.has_value()) {
        return Fail("unbound_variable", "variable $" + node.text + " is not bound");
    }
    return *variable;
}

EvalResult SelectorValue(const Node& node, EvalContext& context) {
    if (!node.children.empty()) {
        return Fail("type_mismatch", "filter on @" + node.text + " is not evaluated yet");
    }
    const std::vector<EntityRef> resolved = context.ResolveSelector(node.text);
    if (resolved.size() > 1) {
        return Fail("type_mismatch",
                    "@" + node.text + " selects many seats; use count(), any(), all(), "
                    "sum() or a single-seat selector");
    }
    if (resolved.size() == 1) {
        return Value(resolved.front());
    }
    // INFO: an empty set stays a value; a property read reports empty_selector.
    return Value(resolved);
}

EvalResult ReadEntityProperty(EntityRef entity, const std::string& key) {
    return Fail("unknown_property",
                "unknown property \"" + key + "\" on " + TypeName(entity.kind));
}

EvalResult PropertyValue(const Node& node, EvalContext& context) {
    const EvalResult base = EvaluateNode(*node.children[0], context);
    if (std::holds_alternative<EvalError>(base)) {
        return base;
    }
    const Value value = std::get<Value>(base);
    const std::string& key = node.text;
    std::optional<EntityRef> entity;
    if (const EntityRef* found = std::get_if<EntityRef>(&value)) {
        entity = *found;
    } else if (const std::vector<EntityRef>* set = std::get_if<std::vector<EntityRef>>(&value)) {
        if (set->empty()) {
            const std::string name =
                node.children[0]->kind == NodeKind::kSelector
                    ? "@" + node.children[0]->text
                    : "the selector";
            return Fail("empty_selector", name + " selects no seats");
        }
        if (set->size() > 1) {
            return Fail("type_mismatch",
                        "reading a property of a seat set; use count(), any(), all(), "
                        "sum() or a single-seat selector");
        }
        entity = set->front();
    } else {
        return Fail("type_mismatch", "cannot read \"" + key + "\" on a value of type " +
                                      ValueName(value));
    }
    const std::optional<Value> property = context.ReadProperty(*entity, key);
    if (!property.has_value()) {
        return ReadEntityProperty(*entity, key);
    }
    return *property;
}

EvalResult UnaryValue(const Node& node, EvalContext& context) {
    const EvalResult operand_result = EvaluateNode(*node.children[0], context);
    if (std::holds_alternative<EvalError>(operand_result)) {
        return operand_result;
    }
    const Value operand = std::get<Value>(operand_result);
    if (node.text == "-") {
        if (const int64_t* number = std::get_if<int64_t>(&operand)) {
            const std::optional<int64_t> negated = Negate(*number);
            if (!negated.has_value()) {
                return Fail("integer_overflow", "integer overflow");
            }
            return Value(*negated);
        }
        if (const double* number = std::get_if<double>(&operand)) {
            return CheckedFloat(-*number);
        }
        return Fail("type_mismatch", "operator - needs numbers");
    }
    if (node.text == "!") {
        const bool* boolean = std::get_if<bool>(&operand);
        if (boolean == nullptr) {
            return Fail("type_mismatch", "operator ! needs a boolean");
        }
        return Value(!*boolean);
    }
    return Fail("type_mismatch", "unsupported operator " + node.text);
}

// INFO: `&&` and `||` evaluate the right operand only when the left one does
// not decide the result, so a guarded read after them never runs.
EvalResult LogicalValue(const Node& node, EvalContext& context) {
    const std::string& symbol = node.text;
    const EvalResult left_result = EvaluateNode(*node.children[0], context);
    if (std::holds_alternative<EvalError>(left_result)) {
        return left_result;
    }
    const bool* left_flag = std::get_if<bool>(&std::get<Value>(left_result));
    if (left_flag == nullptr) {
        return Fail("type_mismatch", "operator " + symbol + " needs booleans");
    }
    if ((symbol == "&&" && !*left_flag) || (symbol == "||" && *left_flag)) {
        return Value(*left_flag);
    }
    const EvalResult right_result = EvaluateNode(*node.children[1], context);
    if (std::holds_alternative<EvalError>(right_result)) {
        return right_result;
    }
    const bool* right_flag = std::get_if<bool>(&std::get<Value>(right_result));
    if (right_flag == nullptr) {
        return Fail("type_mismatch", "operator " + symbol + " needs booleans");
    }
    return Value(*right_flag);
}

template <typename LeftType, typename RightType>
EvalResult OrderedCompare(const std::string& symbol, const LeftType& left_value,
                         const RightType& right_value) {
    if (symbol == "<") {
        return Value(left_value < right_value);
    }
    if (symbol == "<=") {
        return Value(left_value <= right_value);
    }
    if (symbol == ">") {
        return Value(left_value > right_value);
    }
    return Value(left_value >= right_value);
}

EvalResult Compare(const std::string& symbol, const Value& left, const Value& right) {
    if (symbol == "==" || symbol == "!=") {
        bool equal = false;
        if (std::holds_alternative<int64_t>(left) && std::holds_alternative<int64_t>(right)) {
            equal = std::get<int64_t>(left) == std::get<int64_t>(right);
        } else if (IsNumber(left) && IsNumber(right)) {
            equal = AsFloat(left) == AsFloat(right);
        } else if (left.index() == right.index()) {
            equal = left == right;
        } else {
            return Fail("type_mismatch", "operator " + symbol + " cannot compare " +
                                            ValueName(left) + " with " + ValueName(right));
        }
        return Value(symbol == "==" ? equal : !equal);
    }
    if (std::holds_alternative<int64_t>(left) && std::holds_alternative<int64_t>(right)) {
        const int64_t left_value = std::get<int64_t>(left);
        const int64_t right_value = std::get<int64_t>(right);
        return OrderedCompare(symbol, left_value, right_value);
    }
    if (IsNumber(left) && IsNumber(right)) {
        const double left_value = AsFloat(left);
        const double right_value = AsFloat(right);
        return OrderedCompare(symbol, left_value, right_value);
    }
    if (std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::string>(right)) {
        const std::string& left_value = std::get<std::string>(left);
        const std::string& right_value = std::get<std::string>(right);
        return OrderedCompare(symbol, left_value, right_value);
    }
    return Fail("type_mismatch", "operator " + symbol + " needs numbers or strings");
}

EvalResult ApplyBinary(const std::string& symbol, const Value& left, const Value& right) {
    if (symbol == "%") {
        const int64_t* left_int = std::get_if<int64_t>(&left);
        const int64_t* right_int = std::get_if<int64_t>(&right);
        if (left_int == nullptr || right_int == nullptr) {
            return Fail("type_mismatch", "operator % needs ints");
        }
        if (*right_int == 0) {
            return Fail("division_by_zero", "division by zero");
        }
        if (*left_int == std::numeric_limits<int64_t>::min() && *right_int == -1) {
            return Fail("integer_overflow", "integer overflow");
        }
        return Value(*left_int % *right_int);
    }
    if (symbol == "/") {
        const int64_t* left_int = std::get_if<int64_t>(&left);
        const int64_t* right_int = std::get_if<int64_t>(&right);
        if (left_int != nullptr && right_int != nullptr) {
            if (*right_int == 0) {
                return Fail("division_by_zero", "division by zero");
            }
            if (*left_int == std::numeric_limits<int64_t>::min() && *right_int == -1) {
                return Fail("integer_overflow", "integer overflow");
            }
            return Value(*left_int / *right_int);
        }
        if (!IsNumber(left) || !IsNumber(right)) {
            return Fail("type_mismatch", "operator / needs numbers");
        }
        const double left_float = AsFloat(left);
        const double right_float = AsFloat(right);
        if (right_float == 0.0) {
            return Fail("division_by_zero", "division by zero");
        }
        return CheckedFloat(left_float / right_float);
    }
    if (symbol == "+" || symbol == "-" || symbol == "*") {
        if (!IsNumber(left) || !IsNumber(right)) {
            return Fail("type_mismatch", "operator " + symbol + " needs numbers");
        }
        if (IsFloat(left) || IsFloat(right)) {
            const double left_value = AsFloat(left);
            const double right_value = AsFloat(right);
            if (symbol == "+") {
                return CheckedFloat(left_value + right_value);
            }
            if (symbol == "-") {
                return CheckedFloat(left_value - right_value);
            }
            return CheckedFloat(left_value * right_value);
        }
        const int64_t left_value = std::get<int64_t>(left);
        const int64_t right_value = std::get<int64_t>(right);
        std::optional<int64_t> result;
        if (symbol == "+") {
            result = Plus(left_value, right_value);
        } else if (symbol == "-") {
            result = Minus(left_value, right_value);
        } else {
            result = Times(left_value, right_value);
        }
        if (!result.has_value()) {
            return Fail("integer_overflow", "integer overflow");
        }
        return Value(*result);
    }
    return Compare(symbol, left, right);
}

EvalResult BinaryValue(const Node& node, EvalContext& context) {
    if (node.text == "&&" || node.text == "||") {
        return LogicalValue(node, context);
    }
    const EvalResult left_result = EvaluateNode(*node.children[0], context);
    if (std::holds_alternative<EvalError>(left_result)) {
        return left_result;
    }
    const EvalResult right_result = EvaluateNode(*node.children[1], context);
    if (std::holds_alternative<EvalError>(right_result)) {
        return right_result;
    }
    return ApplyBinary(node.text, std::get<Value>(left_result),
                       std::get<Value>(right_result));
}

bool IsKnownFunction(std::string_view name) {
    static const std::vector<std::string_view> names = {
        "min",   "max",    "clamp", "abs",   "floor", "ceil",  "round", "count",
        "any",   "all",    "sum",   "contains", "shares", "has_tag", "status_magnitude",
        "random"};
    for (const std::string_view known : names) {
        if (name == known) {
            return true;
        }
    }
    return false;
}

EvalResult CallValue(const Node& node, EvalContext& context) {
    const std::string& name = node.text;
    if (name == "defined") {
        if (node.children.size() != 1 || node.children[0]->kind != NodeKind::kVariable) {
            return Fail("type_mismatch", "defined needs a $variable");
        }
        return Value(context.GetVariable(node.children[0]->text).has_value());
    }
    if (name == "default") {
        if (node.children.size() != 2 || node.children[0]->kind != NodeKind::kVariable) {
            return Fail("type_mismatch", "default needs a $variable");
        }
        const std::optional<Value> variable = context.GetVariable(node.children[0]->text);
        if (variable.has_value()) {
            return *variable;
        }
        return EvaluateNode(*node.children[1], context);
    }
    if (name == "if") {
        if (node.children.size() != 3) {
            return Fail("type_mismatch", "if takes a condition and two branches");
        }
        const EvalResult condition = EvaluateNode(*node.children[0], context);
        if (std::holds_alternative<EvalError>(condition)) {
            return condition;
        }
        const bool* flag = std::get_if<bool>(&std::get<Value>(condition));
        if (flag == nullptr) {
            return Fail("type_mismatch", "if needs a bool condition");
        }
        // INFO: only the taken branch runs, so an unguarded read in the other
        // branch stays a verify-time error, not a runtime one.
        return EvaluateNode(*node.children[*flag ? 1 : 2], context);
    }
    if (!IsKnownFunction(name)) {
        return Fail("type_mismatch", "unknown function \"" + name + "\"");
    }
    return Fail("type_mismatch", "function \"" + name + "\" is not evaluated yet");
}

EvalResult EvaluateNode(const Node& node, EvalContext& context) {
    switch (node.kind) {
        case NodeKind::kLiteral:
            return LiteralValue(node);
        case NodeKind::kSelector:
            return SelectorValue(node, context);
        case NodeKind::kVariable:
            return VariableValue(node, context);
        case NodeKind::kProperty:
            return PropertyValue(node, context);
        case NodeKind::kCall:
            return CallValue(node, context);
        case NodeKind::kUnary:
            return UnaryValue(node, context);
        case NodeKind::kBinary:
            return BinaryValue(node, context);
        case NodeKind::kBareName:
            return Fail("type_mismatch", "bare name '" + node.text +
                                            "' is only valid inside [ ] or a set function "
                                            "condition");
    }
    return Fail("type_mismatch", "unsupported expression");
}

}  // namespace

EvalResult Evaluate(const Node& root, EvalContext& context) {
    return EvaluateNode(root, context);
}

}  // namespace match::expr
