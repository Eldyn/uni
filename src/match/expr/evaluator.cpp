#include <match/expr/evaluator.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <match/expr/ast.hpp>
#include <match/expr/parser.hpp>
#include <match/expr/types.hpp>

namespace match::expr {
namespace {

EvalResult Fail(std::string code, std::string message) {
    return EvalError{std::move(code), std::move(message)};
}

EvalResult EvaluateNode(const Node& node, EvalContext& context,
                       std::optional<EntityRef> candidate = std::nullopt);

bool IsNumber(const Value& value) {
    return std::holds_alternative<int64_t>(value) || std::holds_alternative<double>(value);
}

bool IsFloat(const Value& value) { return std::holds_alternative<double>(value); }

bool IsNotANumber(const Value& value) {
    return IsFloat(value) && std::isnan(std::get<double>(value));
}

// INFO: an int widens to a float; there is no implicit float to int.
double AsFloat(const Value& value) {
    return IsFloat(value) ? std::get<double>(value)
                          : static_cast<double>(std::get<int64_t>(value));
}

// INFO: applies the checker's widening hint to a value that may be an int.
Value WidenIntToFloat(Value value) {
    if (const int64_t* number = std::get_if<int64_t>(&value)) {
        return Value(static_cast<double>(*number));
    }
    return value;
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
    if (const std::vector<EntityRef>* entities = std::get_if<std::vector<EntityRef>>(&value)) {
        if (entities->empty()) {
            return "set";
        }
        return entities->front().kind == Type::kSeat ? "seat set" : "card set";
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

// INFO: a per-candidate predicate. A leaf error is returned as-is; a
// non-boolean result reports the caller's `mismatch_message`, with the value's
// type appended when `describe_value` is set.
EvalResult EvaluatePredicate(const Node& predicate, EvalContext& context, EntityRef entity,
                             const std::string& mismatch_message, bool describe_value) {
    const EvalResult result = EvaluateNode(predicate, context, entity);
    if (std::holds_alternative<EvalError>(result)) {
        return result;
    }
    const bool* flag = std::get_if<bool>(&std::get<Value>(result));
    if (flag == nullptr) {
        if (describe_value) {
            return Fail("type_mismatch", mismatch_message + ", got " +
                                            ValueName(std::get<Value>(result)));
        }
        return Fail("type_mismatch", mismatch_message);
    }
    return Value(*flag);
}

EvalResult SelectorValue(const Node& node, EvalContext& context) {
    const std::vector<EntityRef> resolved = context.ResolveSelector(node.text);
    // INFO: a filter runs once per candidate; a bare name reads the candidate's
    // property, while other selectors keep their outer meaning.
    std::vector<EntityRef> result;
    for (const EntityRef entity : resolved) {
        if (node.children.empty()) {
            result.push_back(entity);
            continue;
        }
        const EvalResult predicate =
            EvaluatePredicate(*node.children[0], context, entity,
                              "filter on @" + node.text + " must be a boolean", false);
        if (std::holds_alternative<EvalError>(predicate)) {
            return predicate;
        }
        if (std::get<bool>(std::get<Value>(predicate))) {
            result.push_back(entity);
        }
    }
    // INFO: the static selector type decides: a set selector is always a vector
    // even when it holds one entity, while a single selector is one entity or
    // empty_selector when it finds nothing.
    const std::optional<Type> type = SelectorType(node.text);
    if (type == Type::kSeatSet || type == Type::kCardSet) {
        return Value(result);
    }
    if (result.empty()) {
        return Fail("empty_selector", "@" + node.text + " selects no seats");
    }
    return Value(result.front());
}

// INFO: a single-entity argument must yield exactly one entity. An empty
// selector reports empty_selector; several entities and any other value report
// the caller's `mismatch_message`.
EvalResult ExpectSingleEntity(const Value& value, const Node& node,
                              const std::string& mismatch_message) {
    if (const EntityRef* entity = std::get_if<EntityRef>(&value)) {
        return Value(*entity);
    }
    if (const std::vector<EntityRef>* entities = std::get_if<std::vector<EntityRef>>(&value)) {
        if (entities->empty()) {
            const std::string name =
                node.kind == NodeKind::kSelector ? "@" + node.text : "the selector";
            return Fail("empty_selector", name + " selects no seats");
        }
        if (entities->size() > 1) {
            return Fail("type_mismatch", mismatch_message);
        }
        return Value(entities->front());
    }
    return Fail("type_mismatch", mismatch_message);
}

EvalResult PropertyValue(const Node& node, EvalContext& context,
                        std::optional<EntityRef> candidate = std::nullopt) {
    const EvalResult base = EvaluateNode(*node.children[0], context, candidate);
    if (std::holds_alternative<EvalError>(base)) {
        return base;
    }
    const Value value = std::get<Value>(base);
    const std::string& key = node.text;
    if (!std::holds_alternative<EntityRef>(value) &&
        !std::holds_alternative<std::vector<EntityRef>>(value)) {
        return Fail("type_mismatch", "cannot read \"" + key + "\" on a value of type " +
                                       ValueName(value));
    }
    const EvalResult single = ExpectSingleEntity(
        value, *node.children[0],
        "reading a property of a seat set; use count(), any(), all(), sum() or a "
        "single-seat selector");
    if (std::holds_alternative<EvalError>(single)) {
        return single;
    }
    const EntityRef entity = std::get<EntityRef>(std::get<Value>(single));
    const std::optional<Value> property = context.ReadProperty(entity, key);
    if (!property.has_value()) {
        return Fail("unknown_property",
                    "unknown property \"" + key + "\" on " + TypeName(entity.kind));
    }
    return *property;
}

EvalResult UnaryValue(const Node& node, EvalContext& context,
                     std::optional<EntityRef> candidate = std::nullopt) {
    const EvalResult operand_result = EvaluateNode(*node.children[0], context, candidate);
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
EvalResult LogicalValue(const Node& node, EvalContext& context,
                       std::optional<EntityRef> candidate = std::nullopt) {
    const std::string& symbol = node.text;
    const EvalResult left_result = EvaluateNode(*node.children[0], context, candidate);
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
    const EvalResult right_result = EvaluateNode(*node.children[1], context, candidate);
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

// INFO: seat sets and card sets compare as sets, not in context order. The
// inputs are copied so the caller's vectors stay in their own order.
bool EntitySetsEqual(std::vector<EntityRef> left, std::vector<EntityRef> right) {
    if (left.size() != right.size()) {
        return false;
    }
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    return left == right;
}

EvalResult Compare(const std::string& symbol, const Value& left, const Value& right) {
    if (symbol == "==" || symbol == "!=") {
        bool equal = false;
        if (std::holds_alternative<int64_t>(left) && std::holds_alternative<int64_t>(right)) {
            equal = std::get<int64_t>(left) == std::get<int64_t>(right);
        } else if (IsNumber(left) && IsNumber(right)) {
            equal = AsFloat(left) == AsFloat(right);
        } else if (left.index() == right.index()) {
            if (const std::vector<EntityRef>* left_entities =
                    std::get_if<std::vector<EntityRef>>(&left)) {
                equal = EntitySetsEqual(*left_entities,
                                        std::get<std::vector<EntityRef>>(right));
            } else {
                equal = left == right;
            }
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
    return Fail("type_mismatch", "operator " + symbol + " needs numbers");
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

EvalResult BinaryValue(const Node& node, EvalContext& context,
                      std::optional<EntityRef> candidate = std::nullopt) {
    if (node.text == "&&" || node.text == "||") {
        return LogicalValue(node, context, candidate);
    }
    const EvalResult left_result = EvaluateNode(*node.children[0], context, candidate);
    if (std::holds_alternative<EvalError>(left_result)) {
        return left_result;
    }
    const EvalResult right_result = EvaluateNode(*node.children[1], context, candidate);
    if (std::holds_alternative<EvalError>(right_result)) {
        return right_result;
    }
    return ApplyBinary(node.text, std::get<Value>(left_result),
                       std::get<Value>(right_result));
}

// INFO: count, any, all and sum share the first argument, a set. A member set
// only supports count and any, without a condition; a seat set or card set
// supports all four, with or without a condition.
EvalResult EvaluateSetFunction(const std::string& name, const Node& node, EvalContext& context,
                              std::optional<EntityRef> candidate) {
    if (node.children.empty()) {
        return Fail("type_mismatch", name + " needs a set");
    }
    const EvalResult set_result = EvaluateNode(*node.children[0], context, candidate);
    if (std::holds_alternative<EvalError>(set_result)) {
        return set_result;
    }
    const Value set_value = std::get<Value>(set_result);
    if (const std::set<std::string>* members = std::get_if<std::set<std::string>>(&set_value)) {
        if (name == "count" && node.children.size() == 1) {
            return Value(static_cast<int64_t>(members->size()));
        }
        if (name == "any" && node.children.size() == 1) {
            return Value(!members->empty());
        }
        return Fail("type_mismatch", name + " needs a seat set or card set for a condition");
    }
    const std::vector<EntityRef>* seats = std::get_if<std::vector<EntityRef>>(&set_value);
    if (seats == nullptr) {
        return Fail("type_mismatch", name + " needs a set");
    }
    const std::vector<EntityRef>& entities = *seats;
    const std::string condition_message = "condition must be a boolean";

    if (name == "count") {
        if (node.children.size() == 1) {
            return Value(static_cast<int64_t>(entities.size()));
        }
        int64_t matches = 0;
        for (const EntityRef entity : entities) {
            const EvalResult predicate = EvaluatePredicate(*node.children[1], context, entity,
                                                           condition_message, true);
            if (std::holds_alternative<EvalError>(predicate)) {
                return predicate;
            }
            if (std::get<bool>(std::get<Value>(predicate))) {
                ++matches;
            }
        }
        return Value(matches);
    }
    if (name == "any") {
        if (node.children.size() == 1) {
            return Value(!entities.empty());
        }
        for (const EntityRef entity : entities) {
            const EvalResult predicate = EvaluatePredicate(*node.children[1], context, entity,
                                                           condition_message, true);
            if (std::holds_alternative<EvalError>(predicate)) {
                return predicate;
            }
            if (std::get<bool>(std::get<Value>(predicate))) {
                return Value(true);
            }
        }
        return Value(false);
    }
    if (name == "all") {
        if (node.children.size() < 2) {
            return Fail("type_mismatch", "all needs a condition");
        }
        for (const EntityRef entity : entities) {
            const EvalResult predicate = EvaluatePredicate(*node.children[1], context, entity,
                                                           condition_message, true);
            if (std::holds_alternative<EvalError>(predicate)) {
                return predicate;
            }
            if (!std::get<bool>(std::get<Value>(predicate))) {
                return Value(false);
            }
        }
        // INFO: an empty set is vacuously true.
        return Value(true);
    }
    // INFO: sum accumulates ints exactly and widens to float on the first float.
    if (node.children.size() < 2) {
        return Fail("type_mismatch", "sum needs a value expression");
    }
    // INFO: a float value type makes even an empty sum 0.0.
    bool is_float = node.widen_to_float;
    int64_t int_sum = 0;
    double float_sum = 0.0;
    for (const EntityRef entity : entities) {
        const EvalResult value_result = EvaluateNode(*node.children[1], context, entity);
        if (std::holds_alternative<EvalError>(value_result)) {
            return value_result;
        }
        const Value value = std::get<Value>(value_result);
        if (!IsNumber(value)) {
            return Fail("type_mismatch", "sum needs a number expression");
        }
        if (IsFloat(value)) {
            if (!is_float) {
                float_sum = static_cast<double>(int_sum);
                is_float = true;
            }
            float_sum += AsFloat(value);
        } else if (is_float) {
            float_sum += static_cast<double>(std::get<int64_t>(value));
        } else {
            const std::optional<int64_t> added = Plus(int_sum, std::get<int64_t>(value));
            if (!added.has_value()) {
                return Fail("integer_overflow", "integer overflow");
            }
            int_sum = *added;
        }
    }
    if (is_float) {
        return CheckedFloat(float_sum);
    }
    return Value(int_sum);
}

EvalResult CallValue(const Node& node, EvalContext& context,
                    std::optional<EntityRef> candidate = std::nullopt) {
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
        EvalResult result;
        if (variable.has_value()) {
            result = *variable;
        } else {
            result = EvaluateNode(*node.children[1], context, candidate);
        }
        if (std::holds_alternative<EvalError>(result)) {
            return result;
        }
        const Value value = std::get<Value>(result);
        // INFO: the checker widens a mixed pair to float, so the int side of it
        // becomes a double here.
        if (node.widen_to_float) {
            if (const int64_t* number = std::get_if<int64_t>(&value)) {
                return Value(static_cast<double>(*number));
            }
        }
        return value;
    }
    if (name == "if") {
        if (node.children.size() != 3) {
            return Fail("type_mismatch", "if takes a condition and two branches");
        }
        const EvalResult condition = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(condition)) {
            return condition;
        }
        const bool* flag = std::get_if<bool>(&std::get<Value>(condition));
        if (flag == nullptr) {
            return Fail("type_mismatch", "if needs a bool condition");
        }
        // INFO: only the taken branch runs, so an unguarded read in the other
        // branch stays a verify-time error, not a runtime one.
        const EvalResult branch = EvaluateNode(*node.children[*flag ? 1 : 2], context, candidate);
        if (std::holds_alternative<EvalError>(branch)) {
            return branch;
        }
        const Value value = std::get<Value>(branch);
        // INFO: a mixed pair checks as float; an int branch widens to double so
        // the checked type and the runtime value agree.
        if (node.widen_to_float) {
            if (const int64_t* number = std::get_if<int64_t>(&value)) {
                return Value(static_cast<double>(*number));
            }
        }
        return value;
    }
    // INFO: numeric helpers
    if (name == "min" || name == "max") {
        if (node.children.size() < 2) {
            return Fail("type_mismatch", name + " takes at least 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        EvalResult result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(result)) {
            return result;
        }
        for (size_t index = 1; index < node.children.size(); ++index) {
            const EvalResult next = EvaluateNode(*node.children[index], context, candidate);
            if (std::holds_alternative<EvalError>(next)) {
                return next;
            }
            const Value left_value = std::get<Value>(result);
            const Value right_value = std::get<Value>(next);
            if (!IsNumber(left_value) || !IsNumber(right_value)) {
                return Fail("type_mismatch", name + " needs numbers");
            }
            // INFO: a NaN operand makes min/max not a number whatever its
            // position; the comparison below would otherwise ignore it.
            if (IsNotANumber(left_value) || IsNotANumber(right_value)) {
                return Fail("not_a_number", name + " got not a number");
            }
            const bool mixed = IsFloat(left_value) || IsFloat(right_value);
            bool choose_right = false;
            if (mixed) {
                const double left_float = AsFloat(left_value);
                const double right_float = AsFloat(right_value);
                choose_right = (name == "min") ? (right_float < left_float)
                                                : (right_float > left_float);
            } else {
                const int64_t left_int = std::get<int64_t>(left_value);
                const int64_t right_int = std::get<int64_t>(right_value);
                choose_right = (name == "min") ? (right_int < left_int)
                                                : (right_int > left_int);
            }
            if (choose_right) {
                result = next;
            }
            // INFO: the checker widens a mixture to float, so the result follows.
            if (mixed) {
                result = Value(AsFloat(std::get<Value>(result)));
            }
        }
        return std::get<Value>(result);
    }
    if (name == "clamp") {
        if (node.children.size() != 3) {
            return Fail("type_mismatch", "clamp takes 3 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult value_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(value_result)) {
            return value_result;
        }
        const EvalResult low_result = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(low_result)) {
            return low_result;
        }
        const EvalResult high_result = EvaluateNode(*node.children[2], context, candidate);
        if (std::holds_alternative<EvalError>(high_result)) {
            return high_result;
        }
        const Value value = std::get<Value>(value_result);
        const Value low = std::get<Value>(low_result);
        const Value high = std::get<Value>(high_result);
        if (!IsNumber(value) || !IsNumber(low) || !IsNumber(high)) {
            return Fail("type_mismatch", "clamp needs numbers");
        }
        // INFO: a NaN bound or value makes clamp not a number whatever its
        // position; the comparisons below would otherwise ignore it.
        if (IsNotANumber(value) || IsNotANumber(low) || IsNotANumber(high)) {
            return Fail("not_a_number", "clamp got not a number");
        }
        if (IsFloat(value) || IsFloat(low) || IsFloat(high)) {
            double clamped = AsFloat(value);
            const double low_float = AsFloat(low);
            const double high_float = AsFloat(high);
            if (clamped < low_float) {
                clamped = low_float;
            }
            if (clamped > high_float) {
                clamped = high_float;
            }
            return CheckedFloat(clamped);
        }
        int64_t clamped = std::get<int64_t>(value);
        const int64_t low_int = std::get<int64_t>(low);
        const int64_t high_int = std::get<int64_t>(high);
        if (clamped < low_int) {
            clamped = low_int;
        }
        if (clamped > high_int) {
            clamped = high_int;
        }
        return Value(clamped);
    }
    if (name == "abs") {
        if (node.children.size() != 1) {
            return Fail("type_mismatch", "abs takes 1 argument, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult operand_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(operand_result)) {
            return operand_result;
        }
        const Value operand = std::get<Value>(operand_result);
        if (const int64_t* number = std::get_if<int64_t>(&operand)) {
            if (*number == std::numeric_limits<int64_t>::min()) {
                return Fail("integer_overflow", "integer overflow");
            }
            return Value(*number < 0 ? -*number : *number);
        }
        if (const double* number = std::get_if<double>(&operand)) {
            return CheckedFloat(std::fabs(*number));
        }
        return Fail("type_mismatch", "abs needs numbers");
    }
    if (name == "floor" || name == "ceil" || name == "round") {
        if (node.children.size() != 1) {
            return Fail("type_mismatch", name + " takes 1 argument, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult operand_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(operand_result)) {
            return operand_result;
        }
        const Value operand = std::get<Value>(operand_result);
        if (const int64_t* number = std::get_if<int64_t>(&operand)) {
            return Value(*number);
        }
        if (const double* number = std::get_if<double>(&operand)) {
            double result = 0.0;
            if (name == "floor") {
                result = std::floor(*number);
            } else if (name == "ceil") {
                result = std::ceil(*number);
            } else {
                result = std::round(*number);
            }
            // INFO: a NaN result cannot convert to an int; report it as a not a
            // number, not as an overflow.
            if (std::isnan(result)) {
                return Fail("not_a_number", "rounding produced not a number");
            }
            // INFO: a double outside [INT64_MIN, 2^63) cannot be an int64; the
            // upper bound is exclusive because 2^63 itself is out of range.
            if (!(result >= -9223372036854775808.0 && result < 9223372036854775808.0)) {
                return Fail("integer_overflow", "integer overflow");
            }
            return Value(static_cast<int64_t>(result));
        }
        return Fail("type_mismatch", name + " needs numbers");
    }

    if (name == "count" || name == "any" || name == "all" || name == "sum") {
        return EvaluateSetFunction(name, node, context, candidate);
    }

    if (name == "contains") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "contains takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult set_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(set_result)) {
            return set_result;
        }
        const EvalResult member_result = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(member_result)) {
            return member_result;
        }
        const Value set_value = std::get<Value>(set_result);
        const Value member_value = std::get<Value>(member_result);
        if (const std::vector<EntityRef>* members =
                std::get_if<std::vector<EntityRef>>(&set_value)) {
            const EvalResult single = ExpectSingleEntity(member_value, *node.children[1],
                                                         "contains needs an entity");
            if (std::holds_alternative<EvalError>(single)) {
                return single;
            }
            const EntityRef wanted = std::get<EntityRef>(std::get<Value>(single));
            for (const EntityRef entity : *members) {
                if (entity == wanted) {
                    return Value(true);
                }
            }
            return Value(false);
        }
        if (const std::set<std::string>* members = std::get_if<std::set<std::string>>(&set_value)) {
            const std::set<std::string>* subset = std::get_if<std::set<std::string>>(&member_value);
            if (subset == nullptr) {
                return Fail("type_mismatch", "contains needs a member_set");
            }
            for (const std::string& member : *subset) {
                if (members->count(member) == 0) {
                    return Value(false);
                }
            }
            return Value(true);
        }
        return Fail("type_mismatch", "contains needs a set");
    }

    if (name == "shares") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "shares takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult left_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(left_result)) {
            return left_result;
        }
        const EvalResult right_result = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(right_result)) {
            return right_result;
        }
        const Value left = std::get<Value>(left_result);
        const Value right = std::get<Value>(right_result);
        const std::set<std::string>* left_members = std::get_if<std::set<std::string>>(&left);
        const std::set<std::string>* right_members = std::get_if<std::set<std::string>>(&right);
        if (left_members == nullptr || right_members == nullptr) {
            return Fail("type_mismatch", "shares needs member sets");
        }
        for (const std::string& member : *left_members) {
            if (right_members->count(member) > 0) {
                return Value(true);
            }
        }
        return Value(false);
    }

    if (name == "has_tag") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "has_tag takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult target_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(target_result)) {
            return target_result;
        }
        const EvalResult tag_result = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(tag_result)) {
            return tag_result;
        }
        const Value target = std::get<Value>(target_result);
        const Value tag = std::get<Value>(tag_result);
        const std::string* tag_name = std::get_if<std::string>(&tag);
        if (tag_name == nullptr) {
            return Fail("type_mismatch", "has_tag needs a string tag");
        }
        const EvalResult entity_result =
            ExpectSingleEntity(target, *node.children[0], "has_tag needs a card or a rule");
        if (std::holds_alternative<EvalError>(entity_result)) {
            return entity_result;
        }
        const EntityRef entity = std::get<EntityRef>(std::get<Value>(entity_result));
        return Value(context.HasTag(entity, *tag_name));
    }

    if (name == "status_magnitude") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "status_magnitude takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult seat_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(seat_result)) {
            return seat_result;
        }
        const EvalResult status_result = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(status_result)) {
            return status_result;
        }
        const Value seat = std::get<Value>(seat_result);
        const Value status = std::get<Value>(status_result);
        const std::string* status_name = std::get_if<std::string>(&status);
        if (status_name == nullptr) {
            return Fail("type_mismatch", "status_magnitude needs a string status");
        }
        const EvalResult entity_result =
            ExpectSingleEntity(seat, *node.children[0], "status_magnitude needs a seat");
        if (std::holds_alternative<EvalError>(entity_result)) {
            return entity_result;
        }
        const EntityRef entity = std::get<EntityRef>(std::get<Value>(entity_result));
        return Value(context.StatusMagnitude(entity, *status_name));
    }

    if (name == "random") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "random takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult low_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(low_result)) {
            return low_result;
        }
        const EvalResult high_result = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(high_result)) {
            return high_result;
        }
        const Value low_value = std::get<Value>(low_result);
        const Value high_value = std::get<Value>(high_result);
        const int64_t* low = std::get_if<int64_t>(&low_value);
        const int64_t* high = std::get_if<int64_t>(&high_value);
        if (low == nullptr || high == nullptr) {
            return Fail("type_mismatch", "random needs ints");
        }
        // INFO: an empty range is a runtime error; the context RNG never runs.
        if (*low > *high) {
            return Fail("invalid_range", "random needs a low bound not above its high bound");
        }
        return Value(context.Random(*low, *high));
    }

    return Fail("type_mismatch", "unknown function \"" + name + "\"");
}

EvalResult BareNameValue(const Node& node, EvalContext& context,
                         std::optional<EntityRef> candidate) {
    if (!candidate.has_value()) {
        return Fail("type_mismatch", "bare name '" + node.text +
                                        "' is only valid inside [ ] or a set function "
                                        "condition");
    }
    const std::optional<Value> property = context.ReadProperty(*candidate, node.text);
    if (!property.has_value()) {
        return Fail("unknown_property", "unknown property \"" + node.text + "\" on " +
                                           TypeName(candidate->kind));
    }
    return *property;
}

EvalResult EvaluateNode(const Node& node, EvalContext& context,
                       std::optional<EntityRef> candidate) {
    switch (node.kind) {
        case NodeKind::kLiteral:
            return LiteralValue(node);
        case NodeKind::kSelector:
            return SelectorValue(node, context);
        case NodeKind::kVariable:
            return VariableValue(node, context);
        case NodeKind::kProperty:
            return PropertyValue(node, context, candidate);
        case NodeKind::kCall:
            return CallValue(node, context, candidate);
        case NodeKind::kUnary:
            return UnaryValue(node, context, candidate);
        case NodeKind::kBinary:
            return BinaryValue(node, context, candidate);
        case NodeKind::kBareName:
            return BareNameValue(node, context, candidate);
    }
    return Fail("type_mismatch", "unsupported expression");
}

}  // namespace

EvalResult Evaluate(const Node& root, EvalContext& context) {
    // INFO: a caller may evaluate a tree the checker never saw, so the node
    // budget is applied here too, counted once for the whole tree.
    const size_t node_count = CountNodes(root);
    if (node_count > kMaximumExpressionNodes) {
        return EvalError{"expression_too_large",
                         "expression has " + std::to_string(node_count) +
                             " nodes; the limit is " +
                             std::to_string(kMaximumExpressionNodes)};
    }
    EvalResult result = EvaluateNode(root, context, std::nullopt);
    if (root.widen_to_float && std::holds_alternative<Value>(result)) {
        result = WidenIntToFloat(std::get<Value>(result));
    }
    return result;
}

std::optional<EvalError> ApplyAssignment(const Assignment& assignment, EvalContext& context) {
    const Node& target = *assignment.target;
    const EvalResult resolved = EvaluateNode(*target.children[0], context);
    if (std::holds_alternative<EvalError>(resolved)) {
        return std::get<EvalError>(resolved);
    }
    std::vector<EntityRef> entities;
    const Value& resolved_value = std::get<Value>(resolved);
    if (const EntityRef* entity = std::get_if<EntityRef>(&resolved_value)) {
        entities.push_back(*entity);
    } else if (const std::vector<EntityRef>* seats =
                   std::get_if<std::vector<EntityRef>>(&resolved_value)) {
        entities = *seats;
    } else {
        return EvalError{"type_mismatch", "an assignment target must select entities"};
    }
    if (entities.empty()) {
        const std::string name = target.children[0]->kind == NodeKind::kSelector
                                     ? "@" + target.children[0]->text
                                     : "the selector";
        return EvalError{"empty_selector", name + " selects no seats"};
    }
    // INFO: the value expression runs once, whatever the target; the write is
    // repeated per entity.
    const EvalResult value_result = EvaluateNode(*assignment.value, context);
    if (std::holds_alternative<EvalError>(value_result)) {
        return std::get<EvalError>(value_result);
    }
    Value value = std::get<Value>(value_result);
    if (assignment.value->widen_to_float) {
        value = WidenIntToFloat(value);
    }
    const std::string& key = target.text;
    const std::string symbol(AssignmentOperatorSymbol(assignment.op));
    // INFO: pass one computes every new value without writing, so an error on
    // any entity stops the step with no partial writes.
    std::vector<std::pair<EntityRef, Value>> pending;
    pending.reserve(entities.size());
    for (const EntityRef entity : entities) {
        if (assignment.op == AssignOp::kSet) {
            // INFO: a float slot receives a double, so an int value widens when
            // the property already holds a double.
            const std::optional<Value> current = context.ReadProperty(entity, key);
            if (current.has_value() && std::holds_alternative<double>(*current) &&
                std::holds_alternative<int64_t>(value)) {
                pending.emplace_back(entity, Value(static_cast<double>(std::get<int64_t>(value))));
            } else {
                pending.emplace_back(entity, value);
            }
            continue;
        }
        const std::optional<Value> current = context.ReadProperty(entity, key);
        if (!current.has_value()) {
            return EvalError{"unknown_property",
                             "unknown property \"" + key + "\" on " + TypeName(entity.kind)};
        }
        const EvalResult combined = ApplyBinary(symbol, *current, value);
        if (std::holds_alternative<EvalError>(combined)) {
            return std::get<EvalError>(combined);
        }
        pending.emplace_back(entity, std::get<Value>(combined));
    }
    // INFO: pass two writes the computed values. A context must not fail a
    // write it accepted in ReadProperty, so a refusal is still an error.
    for (const auto& [entity, new_value] : pending) {
        if (!context.WriteProperty(entity, key, new_value)) {
            return EvalError{"write_failed", "the context refused a write to \"" + key + "\""};
        }
    }
    return std::nullopt;
}

std::vector<std::string> CollectWrittenKeys(const Assignment& assignment) {
    std::vector<std::string> keys;
    // INFO: the checker guarantees a property target, so this is the one key.
    if (assignment.target->kind == NodeKind::kProperty) {
        keys.push_back(assignment.target->text);
    }
    return keys;
}

}  // namespace match::expr
