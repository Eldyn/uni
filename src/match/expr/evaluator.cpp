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

EvalResult EvaluateNode(const Node& node, EvalContext& context,
                       std::optional<EntityRef> candidate = std::nullopt);

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

EvalResult SelectorValue(const Node& node, EvalContext& context,
                        std::optional<EntityRef> candidate) {
    const std::vector<EntityRef> resolved = context.ResolveSelector(node.text);
    if (node.children.empty()) {
        if (resolved.size() == 1) {
            return Value(resolved.front());
        }
        // INFO: a multi-seat selector stays a set value; reading a property of
        // it is rejected by PropertyValue, which asks for an aggregation.
        return Value(resolved);
    }
    // INFO: a filter runs once per candidate; a bare name reads the candidate's
    // property, while other selectors keep their outer meaning.
    if (resolved.size() <= 1) {
        if (resolved.empty()) {
            return Value(resolved);
        }
        const EvalResult filter_result =
            EvaluateNode(*node.children[0], context, resolved.front());
        if (std::holds_alternative<EvalError>(filter_result)) {
            return filter_result;
        }
        const bool* flag = std::get_if<bool>(&std::get<Value>(filter_result));
        if (flag == nullptr) {
            return Fail("type_mismatch", "filter on @" + node.text + " must be a boolean");
        }
        if (*flag) {
            return Value(resolved.front());
        }
        return Value(std::vector<EntityRef>{});
    }
    std::vector<EntityRef> result;
    for (const EntityRef entity : resolved) {
        const EvalResult filter_result = EvaluateNode(*node.children[0], context, entity);
        if (std::holds_alternative<EvalError>(filter_result)) {
            return filter_result;
        }
        const bool* flag = std::get_if<bool>(&std::get<Value>(filter_result));
        if (flag == nullptr) {
            return Fail("type_mismatch", "filter on @" + node.text + " must be a boolean");
        }
        if (*flag) {
            result.push_back(entity);
        }
    }
    return Value(result);
}

EvalResult ReadEntityProperty(EntityRef entity, const std::string& key) {
    return Fail("unknown_property",
                "unknown property \"" + key + "\" on " + TypeName(entity.kind));
}

EvalResult PropertyValue(const Node& node, EvalContext& context,
                        std::optional<EntityRef> candidate = std::nullopt) {
    const EvalResult base = EvaluateNode(*node.children[0], context, candidate);
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
        if (variable.has_value()) {
            return *variable;
        }
        return EvaluateNode(*node.children[1], context, candidate);
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
        return EvaluateNode(*node.children[*flag ? 1 : 2], context, candidate);
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
        for (size_t i = 1; i < node.children.size(); ++i) {
            const EvalResult next = EvaluateNode(*node.children[i], context, candidate);
            if (std::holds_alternative<EvalError>(next)) {
                return next;
            }
            const Value left_value = std::get<Value>(result);
            const Value right_value = std::get<Value>(next);
            if (!IsNumber(left_value) || !IsNumber(right_value)) {
                return Fail("type_mismatch", name + " needs numbers");
            }
            bool choose_right = false;
            if (IsFloat(left_value) || IsFloat(right_value)) {
                const double l = AsFloat(left_value);
                const double r = AsFloat(right_value);
                choose_right = (name == "min") ? (r < l) : (r > l);
            } else {
                const int64_t l = std::get<int64_t>(left_value);
                const int64_t r = std::get<int64_t>(right_value);
                choose_right = (name == "min") ? (r < l) : (r > l);
            }
            if (choose_right) {
                result = next;
            }
        }
        return std::get<Value>(result);
    }
    if (name == "clamp") {
        if (node.children.size() != 3) {
            return Fail("type_mismatch", "clamp takes 3 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        EvalResult a = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(a)) return a;
        EvalResult lo = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(lo)) return lo;
        EvalResult hi = EvaluateNode(*node.children[2], context, candidate);
        if (std::holds_alternative<EvalError>(hi)) return hi;
        const Value av = std::get<Value>(a);
        const Value lov = std::get<Value>(lo);
        const Value hiv = std::get<Value>(hi);
        if (!IsNumber(av) || !IsNumber(lov) || !IsNumber(hiv)) {
            return Fail("type_mismatch", "clamp needs numbers");
        }
        if (IsFloat(av) || IsFloat(lov) || IsFloat(hiv)) {
            double v = AsFloat(av);
            const double l = AsFloat(lov);
            const double h = AsFloat(hiv);
            if (v < l) v = l;
            if (v > h) v = h;
            return CheckedFloat(v);
        }
        int64_t v = std::get<int64_t>(av);
        const int64_t l = std::get<int64_t>(lov);
        const int64_t h = std::get<int64_t>(hiv);
        if (v < l) v = l;
        if (v > h) v = h;
        return Value(v);
    }
    if (name == "abs") {
        if (node.children.size() != 1) {
            return Fail("type_mismatch", "abs takes 1 argument, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult r = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(r)) return r;
        const Value v = std::get<Value>(r);
        if (const int64_t* n = std::get_if<int64_t>(&v)) {
            if (*n == std::numeric_limits<int64_t>::min()) {
                return Fail("integer_overflow", "integer overflow");
            }
            return Value(*n < 0 ? -*n : *n);
        }
        if (const double* n = std::get_if<double>(&v)) {
            return CheckedFloat(std::fabs(*n));
        }
        return Fail("type_mismatch", "abs needs numbers");
    }
    if (name == "floor" || name == "ceil" || name == "round") {
        if (node.children.size() != 1) {
            return Fail("type_mismatch", name + " takes 1 argument, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult r = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(r)) return r;
        const Value v = std::get<Value>(r);
        if (const int64_t* n = std::get_if<int64_t>(&v)) {
            return Value(*n);
        }
        if (const double* n = std::get_if<double>(&v)) {
            double result = 0;
            if (name == "floor") result = std::floor(*n);
            else if (name == "ceil") result = std::ceil(*n);
            else result = std::round(*n);
            // INFO: converting double to int64; large values are out of range.
            if (result < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
                result > static_cast<double>(std::numeric_limits<int64_t>::max())) {
                return Fail("integer_overflow", "integer overflow");
            }
            return Value(static_cast<int64_t>(result));
        }
        return Fail("type_mismatch", name + " needs numbers");
    }

    if (name == "count" || name == "any" || name == "all" || name == "sum") {
        if (node.children.empty()) {
            return Fail("type_mismatch", name + " needs a set");
        }
        const EvalResult set_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(set_result)) {
            return set_result;
        }
        const Value set_value = std::get<Value>(set_result);
        // INFO: collect candidate entities.
        std::vector<EntityRef> entities;
        bool is_member_set = std::holds_alternative<std::set<std::string>>(set_value);
        if (const EntityRef* e = std::get_if<EntityRef>(&set_value)) {
            entities.push_back(*e);
        } else if (const std::vector<EntityRef>* v = std::get_if<std::vector<EntityRef>>(&set_value)) {
            entities = *v;
        } else if (!is_member_set) {
            return Fail("type_mismatch", name + " needs a set");
        }

        if (name == "count") {
            if (node.children.size() == 1) {
                if (is_member_set) {
                    return Value(static_cast<int64_t>(std::get<std::set<std::string>>(set_value).size()));
                }
                return Value(static_cast<int64_t>(entities.size()));
            }
            // condition
            int64_t cnt = 0;
            for (const EntityRef entity : entities) {
                const EvalResult cond = EvaluateNode(*node.children[1], context, entity);
                if (std::holds_alternative<EvalError>(cond)) {
                    return cond;
                }
                const bool* flag = std::get_if<bool>(&std::get<Value>(cond));
                if (flag == nullptr) {
                    return Fail("type_mismatch", name + " condition must be bool");
                }
                if (*flag) {
                    ++cnt;
                }
            }
            // INFO: member sets with a condition are not supported; checker rejects them.
            if (is_member_set) {
                return Fail("type_mismatch", name + " needs a set");
            }
            return Value(cnt);
        }
        if (name == "any") {
            if (node.children.size() == 1) {
                if (is_member_set) {
                    return Value(!std::get<std::set<std::string>>(set_value).empty());
                }
                return Value(!entities.empty());
            }
            for (const EntityRef entity : entities) {
                const EvalResult cond = EvaluateNode(*node.children[1], context, entity);
                if (std::holds_alternative<EvalError>(cond)) {
                    return cond;
                }
                const bool* flag = std::get_if<bool>(&std::get<Value>(cond));
                if (flag == nullptr) {
                    return Fail("type_mismatch", name + " condition must be bool");
                }
                if (*flag) {
                    return Value(true);
                }
            }
            if (is_member_set) {
                return Fail("type_mismatch", name + " needs a set");
            }
            return Value(false);
        }
        if (name == "all") {
            if (node.children.size() < 2) {
                return Fail("type_mismatch", "all needs a condition");
            }
            if (entities.empty() && !is_member_set) {
                // INFO: vacuous truth for empty set.
                return Value(true);
            }
            for (const EntityRef entity : entities) {
                const EvalResult cond = EvaluateNode(*node.children[1], context, entity);
                if (std::holds_alternative<EvalError>(cond)) {
                    return cond;
                }
                const bool* flag = std::get_if<bool>(&std::get<Value>(cond));
                if (flag == nullptr) {
                    return Fail("type_mismatch", name + " condition must be bool");
                }
                if (!*flag) {
                    return Value(false);
                }
            }
            return Value(true);
        }
        if (name == "sum") {
            if (node.children.size() < 2) {
                return Fail("type_mismatch", "sum needs a value expression");
            }
            bool is_float = false;
            int64_t int_sum = 0;
            double float_sum = 0.0;
            bool has_any = false;
            for (const EntityRef entity : entities) {
                const EvalResult val = EvaluateNode(*node.children[1], context, entity);
                if (std::holds_alternative<EvalError>(val)) {
                    return val;
                }
                const Value v = std::get<Value>(val);
                if (!IsNumber(v)) {
                    return Fail("type_mismatch", "sum needs a number expression");
                }
                if (IsFloat(v)) {
                    if (!is_float) {
                        float_sum = static_cast<double>(int_sum);
                        is_float = true;
                    }
                    float_sum += AsFloat(v);
                } else {
                    if (is_float) {
                        float_sum += static_cast<double>(std::get<int64_t>(v));
                    } else {
                        const std::optional<int64_t> added =
                            Plus(int_sum, std::get<int64_t>(v));
                        if (!added.has_value()) {
                            return Fail("integer_overflow", "integer overflow");
                        }
                        int_sum = *added;
                    }
                }
                has_any = true;
            }
            if (is_float) {
                return CheckedFloat(float_sum);
            }
            return Value(int_sum);
        }
    }

    if (name == "contains") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "contains takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult set_result = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(set_result)) return set_result;
        const EvalResult member_result = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(member_result)) return member_result;
        const Value set_value = std::get<Value>(set_result);
        const Value member_value = std::get<Value>(member_result);
        if (const std::vector<EntityRef>* v = std::get_if<std::vector<EntityRef>>(&set_value)) {
            const EntityRef* m = std::get_if<EntityRef>(&member_value);
            if (m == nullptr) {
                return Fail("type_mismatch", "contains needs an entity");
            }
            for (const EntityRef e : *v) {
                if (e == *m) return Value(true);
            }
            return Value(false);
        }
        if (const EntityRef* e = std::get_if<EntityRef>(&set_value)) {
            const EntityRef* m = std::get_if<EntityRef>(&member_value);
            if (m == nullptr || *e != *m) {
                // INFO: single entity contains itself only when equal.
            }
            if (m != nullptr) {
                return Value(*e == *m);
            }
            return Fail("type_mismatch", "contains needs an entity");
        }
        if (const std::set<std::string>* s = std::get_if<std::set<std::string>>(&set_value)) {
            const std::string* m = std::get_if<std::string>(&member_value);
            if (m == nullptr) {
                return Fail("type_mismatch", "contains needs a member");
            }
            return Value(s->count(*m) > 0);
        }
        return Fail("type_mismatch", "contains needs a set");
    }

    if (name == "shares") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "shares takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult a = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(a)) return a;
        const EvalResult b = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(b)) return b;
        const Value av = std::get<Value>(a);
        const Value bv = std::get<Value>(b);
        const std::set<std::string>* as = std::get_if<std::set<std::string>>(&av);
        const std::set<std::string>* bs = std::get_if<std::set<std::string>>(&bv);
        if (as == nullptr || bs == nullptr) {
            return Fail("type_mismatch", "shares needs member sets");
        }
        for (const std::string& m : *as) {
            if (bs->count(m) > 0) {
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
        const EvalResult target = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(target)) return target;
        const EvalResult tag = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(tag)) return tag;
        const Value tv = std::get<Value>(target);
        const Value tg = std::get<Value>(tag);
        const EntityRef* entity = std::get_if<EntityRef>(&tv);
        const std::string* tag_str = std::get_if<std::string>(&tg);
        if (entity == nullptr || tag_str == nullptr) {
            return Fail("type_mismatch", "has_tag needs a card or a rule");
        }
        return Value(context.HasTag(*entity, *tag_str));
    }

    if (name == "status_magnitude") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "status_magnitude takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult seat = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(seat)) return seat;
        const EvalResult status = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(status)) return status;
        const Value sv = std::get<Value>(seat);
        const Value st = std::get<Value>(status);
        const EntityRef* entity = std::get_if<EntityRef>(&sv);
        const std::string* status_str = std::get_if<std::string>(&st);
        if (entity == nullptr || status_str == nullptr) {
            return Fail("type_mismatch", "status_magnitude needs a seat");
        }
        return Value(context.StatusMagnitude(*entity, *status_str));
    }

    if (name == "random") {
        if (node.children.size() != 2) {
            return Fail("type_mismatch", "random takes 2 arguments, got " +
                                            std::to_string(node.children.size()));
        }
        const EvalResult lo = EvaluateNode(*node.children[0], context, candidate);
        if (std::holds_alternative<EvalError>(lo)) return lo;
        const EvalResult hi = EvaluateNode(*node.children[1], context, candidate);
        if (std::holds_alternative<EvalError>(hi)) return hi;
        const Value lv = std::get<Value>(lo);
        const Value hv = std::get<Value>(hi);
        const int64_t* l = std::get_if<int64_t>(&lv);
        const int64_t* h = std::get_if<int64_t>(&hv);
        if (l == nullptr || h == nullptr) {
            return Fail("type_mismatch", "random needs ints");
        }
        return Value(context.Random(*l, *h));
    }

    if (!IsKnownFunction(name)) {
        return Fail("type_mismatch", "unknown function \"" + name + "\"");
    }
    return Fail("type_mismatch", "function \"" + name + "\" is not evaluated yet");
}

EvalResult EvaluateNode(const Node& node, EvalContext& context,
                       std::optional<EntityRef> candidate) {
    switch (node.kind) {
        case NodeKind::kLiteral:
            return LiteralValue(node);
        case NodeKind::kSelector:
            return SelectorValue(node, context, candidate);
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
            if (!candidate.has_value()) {
                return Fail("type_mismatch", "bare name '" + node.text +
                                                "' is only valid inside [ ] or a set function "
                                                "condition");
            }
            {
                const std::optional<Value> property =
                    context.ReadProperty(*candidate, node.text);
                if (!property.has_value()) {
                    return Fail("unknown_property",
                                "unknown property \"" + node.text + "\" on " +
                                    TypeName(candidate->kind));
                }
                return *property;
            }
    }
    return Fail("type_mismatch", "unsupported expression");
}

}  // namespace

EvalResult Evaluate(const Node& root, EvalContext& context) {
    return EvaluateNode(root, context, std::nullopt);
}

}  // namespace match::expr
