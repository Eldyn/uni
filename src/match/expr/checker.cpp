#include <match/expr/checker.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <match/expr/parser.hpp>

namespace match::expr {

namespace {

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

// INFO: a member set holds itself; the other sets hold their entity.
Type MemberOf(Type set_type) {
    if (set_type == Type::kMemberSet) {
        return Type::kMemberSet;
    }
    return EntityOf(set_type);
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

std::set<std::string> Intersect(const std::set<std::string>& left,
                                const std::set<std::string>& right) {
    std::set<std::string> common;
    for (const std::string& name : left) {
        if (right.count(name) != 0) {
            common.insert(name);
        }
    }
    return common;
}

// INFO: the result of checking one node. `when_true` and `when_false` are the
// variables a guard guarantees to be bound when the node evaluates to true or
// false, so `&&`, `||`, `!` and `if` can narrow the branch that follows them.
struct Narrowed {
    Type type;
    std::set<std::string> when_true;
    std::set<std::string> when_false;
};

class Checker {
public:
    Checker(const Schema& schema, std::optional<Type> expected, std::string_view file,
            std::string_view pointer, const GuardState& initial)
        : schema_(schema),
          expected_(expected),
          file_(file),
          pointer_(pointer),
          initial_(initial) {}

    std::optional<Type> CheckRoot(const Node& node) {
        const std::optional<Narrowed> checked =
            Check(node, Scope::kRoot, Type::kSeat, initial_.bound);
        if (!checked) {
            return std::nullopt;
        }
        bound_when_true_ = checked->when_true;
        bound_when_false_ = checked->when_false;
        if (!expected_) {
            return checked->type;
        }
        if (!Accepts(*expected_, checked->type)) {
            Error(ExpectedMessage(*expected_, checked->type));
            return std::nullopt;
        }
        return checked->type;
    }

    bool failed() const { return !diagnostics_.empty(); }

    // INFO: checks one assignment: the target must be a writable property and
    // the value must fit its declared type.
    std::vector<Diagnostic> CheckAssignment(const Assignment& assignment) {
        const std::optional<Type> property = CheckAssignmentTarget(*assignment.target);
        if (!property) {
            return TakeDiagnostics();
        }
        if (assignment.op != AssignOp::kSet && !IsNumber(*property)) {
            // INFO: only a compound operator reaches here; it reads with a
            // trailing '='.
            Error("operator " + std::string(AssignmentOperatorSymbol(assignment.op)) +
                  "= needs a number property");
            return TakeDiagnostics();
        }
        const std::optional<Narrowed> value =
            Check(*assignment.value, Scope::kRoot, Type::kSeat, initial_.bound);
        if (!value) {
            return TakeDiagnostics();
        }
        if (!Accepts(*property, value->type)) {
            Error(ExpectedMessage(*property, value->type));
        }
        return TakeDiagnostics();
    }

    std::vector<Diagnostic> TakeDiagnostics() { return std::move(diagnostics_); }

    const std::set<std::string>& BoundWhenTrue() const { return bound_when_true_; }

    const std::set<std::string>& BoundWhenFalse() const { return bound_when_false_; }

private:
    void Error(std::string message, std::string suggestion = {}) {
        diagnostics_.push_back(Diagnostic{std::string(file_), std::string(pointer_),
                                          std::move(message), std::move(suggestion)});
    }

    // INFO: a node that does not narrow anything keeps the guard it was given.
    static std::optional<Narrowed> Plain(std::optional<Type> type,
                                         const std::set<std::string>& bound) {
        if (!type) {
            return std::nullopt;
        }
        return Narrowed{*type, bound, bound};
    }

    // INFO: a set-valued selector is writable, so the target's base may be a
    // set; every member changes. A property without a namespace is built-in
    // and read-only.
    std::optional<Type> CheckAssignmentTarget(const Node& target) {
        const std::optional<Narrowed> base =
            Check(*target.children[0], Scope::kRoot, Type::kSeat, initial_.bound);
        if (!base) {
            return std::nullopt;
        }
        Type entity = base->type;
        if (IsSet(entity)) {
            entity = EntityOf(entity);
        }
        if (schema_.Properties(entity) == nullptr) {
            Error("cannot assign to a property of a value of type " + TypeName(base->type));
            return std::nullopt;
        }
        const std::optional<Type> type = schema_.PropertyType(entity, target.text);
        if (!type) {
            Error("unknown property \"" + target.text + "\" on " + TypeName(entity),
                  SuggestClosest(target.text, PropertyNames(schema_, entity)));
            return std::nullopt;
        }
        if (target.text.find(':') == std::string::npos) {
            Error("built-in property " + target.text + " is read-only");
            return std::nullopt;
        }
        return type;
    }

    std::optional<Narrowed> Check(const Node& node, Scope scope, Type candidate,
                                  const std::set<std::string>& bound) {
        switch (node.kind) {
            case NodeKind::kLiteral:
                return Plain(LiteralType(node), bound);
            case NodeKind::kSelector:
                return CheckSelector(node, bound);
            case NodeKind::kVariable:
                return CheckVariable(node, bound);
            case NodeKind::kProperty:
                return CheckProperty(node, scope, candidate, bound);
            case NodeKind::kBareName:
                return Plain(CheckBareName(node, scope, candidate), bound);
            case NodeKind::kUnary:
                return CheckUnary(node, scope, candidate, bound);
            case NodeKind::kBinary:
                return CheckBinary(node, scope, candidate, bound);
            case NodeKind::kCall:
                return CheckCall(node, scope, candidate, bound);
        }
        Error("unsupported expression");
        return std::nullopt;
    }

    std::optional<Narrowed> CheckCall(const Node& node, Scope scope, Type candidate,
                                      const std::set<std::string>& bound) {
        const FunctionInfo* function = FindFunction(node.text);
        if (function == nullptr) {
            Error("unknown function \"" + node.text + "\"",
                  SuggestClosest(node.text, FunctionNames()));
            return std::nullopt;
        }
        const size_t arity = node.children.size();
        if (arity < function->minimum_arguments ||
            (function->maximum_arguments.has_value() && arity > *function->maximum_arguments)) {
            Error(ArityMessage(*function, arity));
            return std::nullopt;
        }
        return (this->*function->check)(node, scope, candidate, bound);
    }

    std::optional<Narrowed> CheckIf(const Node& node, Scope scope, Type candidate,
                                    const std::set<std::string>& bound) {
        const std::optional<Narrowed> condition =
            Check(*node.children[0], scope, candidate, bound);
        if (!condition) {
            return std::nullopt;
        }
        if (condition->type != Type::kBool) {
            Error("if needs a bool condition");
            return std::nullopt;
        }
        const std::optional<Narrowed> then_type =
            Check(*node.children[1], scope, candidate, condition->when_true);
        if (!then_type) {
            return std::nullopt;
        }
        const std::optional<Narrowed> else_type =
            Check(*node.children[2], scope, candidate, condition->when_false);
        if (!else_type) {
            return std::nullopt;
        }
        Type type;
        if (IsNumber(then_type->type) && IsNumber(else_type->type)) {
            type = Widen(then_type->type, else_type->type);
        } else if (then_type->type == else_type->type) {
            type = then_type->type;
        } else {
            Error("if needs matching branch types");
            return std::nullopt;
        }
        // INFO: the taken branch may be the int one of a mixed pair; the
        // evaluator widens it so the checked float type holds at run time.
        if (type == Type::kFloat) {
            node.widen_to_float = true;
        }
        return Narrowed{type, Intersect(then_type->when_true, else_type->when_true),
                        Intersect(then_type->when_false, else_type->when_false)};
    }

    std::optional<Narrowed> CheckNumericArguments(const Node& node, Scope scope, Type candidate,
                                                  const std::set<std::string>& bound) {
        Type result = Type::kInt;
        for (const NodePtr& child : node.children) {
            const std::optional<Narrowed> type = Check(*child, scope, candidate, bound);
            if (!type) {
                return std::nullopt;
            }
            if (!IsNumber(type->type)) {
                Error(node.text + " needs numbers");
                return std::nullopt;
            }
            result = Widen(result, type->type);
        }
        return Narrowed{result, bound, bound};
    }

    std::optional<Narrowed> CheckRounding(const Node& node, Scope scope, Type candidate,
                                          const std::set<std::string>& bound) {
        const std::optional<Narrowed> type = Check(*node.children[0], scope, candidate, bound);
        if (!type) {
            return std::nullopt;
        }
        if (!IsNumber(type->type)) {
            Error(node.text + " needs a number");
            return std::nullopt;
        }
        return Narrowed{Type::kInt, bound, bound};
    }

    // INFO: a set call shares its first argument, the set, and a condition that
    // runs per candidate; the candidate is the entity the set holds. Each
    // aggregation is its own table entry, so every call has a handler and none
    // can fall out of a shared switch.
    std::optional<Type> CheckSetArgument(const Node& node, Scope scope, Type candidate,
                                         const std::set<std::string>& bound) {
        const std::optional<Narrowed> set_type = Check(*node.children[0], scope, candidate, bound);
        if (!set_type) {
            return std::nullopt;
        }
        if (!IsSet(set_type->type)) {
            Error(node.text + " needs a set");
            return std::nullopt;
        }
        return set_type->type;
    }

    std::optional<Narrowed> CheckCount(const Node& node, Scope scope, Type candidate,
                                       const std::set<std::string>& bound) {
        const std::optional<Type> set_type = CheckSetArgument(node, scope, candidate, bound);
        if (!set_type) {
            return std::nullopt;
        }
        // INFO: a member set holds its members; only count and any read it, and
        // only without a condition.
        if (*set_type == Type::kMemberSet) {
            if (node.children.size() > 1) {
                Error("count needs a seat set or card set for a condition");
                return std::nullopt;
            }
            return Narrowed{Type::kInt, bound, bound};
        }
        if (!CheckOptionalCondition(node, 1, EntityOf(*set_type), bound)) {
            return std::nullopt;
        }
        return Narrowed{Type::kInt, bound, bound};
    }

    std::optional<Narrowed> CheckAny(const Node& node, Scope scope, Type candidate,
                                     const std::set<std::string>& bound) {
        const std::optional<Type> set_type = CheckSetArgument(node, scope, candidate, bound);
        if (!set_type) {
            return std::nullopt;
        }
        if (*set_type == Type::kMemberSet) {
            if (node.children.size() > 1) {
                Error("any needs a seat set or card set for a condition");
                return std::nullopt;
            }
            return Narrowed{Type::kBool, bound, bound};
        }
        if (!CheckOptionalCondition(node, 1, EntityOf(*set_type), bound)) {
            return std::nullopt;
        }
        return Narrowed{Type::kBool, bound, bound};
    }

    std::optional<Narrowed> CheckAll(const Node& node, Scope scope, Type candidate,
                                     const std::set<std::string>& bound) {
        const std::optional<Type> set_type = CheckSetArgument(node, scope, candidate, bound);
        if (!set_type) {
            return std::nullopt;
        }
        if (node.children.size() < 2) {
            Error("all needs a condition");
            return std::nullopt;
        }
        if (*set_type == Type::kMemberSet) {
            Error("all needs a seat set or card set for a condition");
            return std::nullopt;
        }
        if (!CheckOptionalCondition(node, 1, EntityOf(*set_type), bound)) {
            return std::nullopt;
        }
        return Narrowed{Type::kBool, bound, bound};
    }

    std::optional<Narrowed> CheckSum(const Node& node, Scope scope, Type candidate,
                                     const std::set<std::string>& bound) {
        const std::optional<Type> set_type = CheckSetArgument(node, scope, candidate, bound);
        if (!set_type) {
            return std::nullopt;
        }
        if (*set_type == Type::kMemberSet) {
            Error("sum needs a seat set or card set for a condition");
            return std::nullopt;
        }
        // INFO: the value expression is checked in the candidate scope so a
        // bare name reads the candidate's property.
        const std::optional<Narrowed> value = Check(*node.children[1], Scope::kCandidate,
                                                    EntityOf(*set_type), bound);
        if (!value) {
            return std::nullopt;
        }
        if (!IsNumber(value->type)) {
            Error("sum needs a number expression");
            return std::nullopt;
        }
        // INFO: an empty float sum is 0.0, so the int result widens when the
        // value expression is float.
        node.widen_to_float = (value->type == Type::kFloat);
        return Narrowed{value->type, bound, bound};
    }

    bool CheckOptionalCondition(const Node& node, size_t index, Type entity,
                                const std::set<std::string>& bound) {
        if (node.children.size() <= index) {
            return true;
        }
        return CheckCondition(*node.children[index], entity, bound, false).has_value();
    }

    std::optional<Narrowed> CheckContains(const Node& node, Scope scope, Type candidate,
                                          const std::set<std::string>& bound) {
        const std::optional<Narrowed> set_type = Check(*node.children[0], scope, candidate, bound);
        if (!set_type) {
            return std::nullopt;
        }
        if (!IsSet(set_type->type)) {
            Error("contains needs a set");
            return std::nullopt;
        }
        const Type member_type = MemberOf(set_type->type);
        const std::optional<Narrowed> member = Check(*node.children[1], scope, candidate, bound);
        if (!member) {
            return std::nullopt;
        }
        if (member->type != member_type) {
            Error("contains needs a " + TypeName(member_type) + " for a " +
                  TypeName(set_type->type));
            return std::nullopt;
        }
        return Narrowed{Type::kBool, bound, bound};
    }

    std::optional<Narrowed> CheckShares(const Node& node, Scope scope, Type candidate,
                                        const std::set<std::string>& bound) {
        for (const NodePtr& child : node.children) {
            const std::optional<Narrowed> type = Check(*child, scope, candidate, bound);
            if (!type) {
                return std::nullopt;
            }
            if (type->type != Type::kMemberSet) {
                Error("shares needs member sets");
                return std::nullopt;
            }
        }
        return Narrowed{Type::kBool, bound, bound};
    }

    std::optional<Narrowed> CheckHasTag(const Node& node, Scope scope, Type candidate,
                                        const std::set<std::string>& bound) {
        const std::optional<Narrowed> target = Check(*node.children[0], scope, candidate, bound);
        if (!target) {
            return std::nullopt;
        }
        if (target->type != Type::kCard && target->type != Type::kRule) {
            Error("has_tag needs a card or a rule");
            return std::nullopt;
        }
        const std::optional<Narrowed> tag = Check(*node.children[1], scope, candidate, bound);
        if (!tag) {
            return std::nullopt;
        }
        if (tag->type != Type::kString) {
            Error("has_tag needs a string tag");
            return std::nullopt;
        }
        return Narrowed{Type::kBool, bound, bound};
    }

    // INFO: `defined($x)` is the guard that binds `$x`; its argument is allowed
    // to be unbound, and when the call is false the variable stays unbound.
    std::optional<Narrowed> CheckDefined(const Node& node, Scope scope, Type candidate,
                                         const std::set<std::string>& bound) {
        if (node.children[0]->kind != NodeKind::kVariable) {
            Error("defined needs a $variable");
            return std::nullopt;
        }
        if (!FindVariable(*node.children[0])) {
            return std::nullopt;
        }
        std::set<std::string> when_true = bound;
        when_true.insert(node.children[0]->text);
        return Narrowed{Type::kBool, std::move(when_true), bound};
    }

    // INFO: `default($x, value)` supplies the value for an unbound `$x`, so its
    // first argument is read without the unbound-variable guard.
    std::optional<Narrowed> CheckDefault(const Node& node, Scope scope, Type candidate,
                                         const std::set<std::string>& bound) {
        if (node.children[0]->kind != NodeKind::kVariable) {
            Error("default needs a $variable");
            return std::nullopt;
        }
        const std::optional<VariableInfo> variable = FindVariable(*node.children[0]);
        if (!variable) {
            return std::nullopt;
        }
        const std::optional<Narrowed> fallback = Check(*node.children[1], scope, candidate, bound);
        if (!fallback) {
            return std::nullopt;
        }
        // INFO: a bound variable and its fallback must agree; two numbers widen
        // to float and the evaluator widens the int side to double.
        Type type;
        if (IsNumber(variable->type) && IsNumber(fallback->type)) {
            type = Widen(variable->type, fallback->type);
            if (type == Type::kFloat) {
                node.widen_to_float = true;
            }
        } else if (variable->type == fallback->type) {
            type = variable->type;
        } else {
            Error("default needs a " + TypeName(variable->type) + " fallback for $" +
                  node.children[0]->text);
            return std::nullopt;
        }
        return Narrowed{type, bound, bound};
    }

    std::optional<Narrowed> CheckStatusMagnitude(const Node& node, Scope scope, Type candidate,
                                                 const std::set<std::string>& bound) {
        const std::optional<Narrowed> seat = Check(*node.children[0], scope, candidate, bound);
        if (!seat) {
            return std::nullopt;
        }
        if (seat->type != Type::kSeat) {
            Error("status_magnitude needs a seat");
            return std::nullopt;
        }
        const std::optional<Narrowed> status = Check(*node.children[1], scope, candidate, bound);
        if (!status) {
            return std::nullopt;
        }
        if (status->type != Type::kString) {
            Error("status_magnitude needs a string status");
            return std::nullopt;
        }
        return Narrowed{Type::kInt, bound, bound};
    }

    // INFO: `random(low, high)` draws from the seeded RNG; two literal bounds
    // that are already inverted can never be a valid range, so the checker
    // rejects them instead of deferring to a run-time `invalid_range`.
    std::optional<Narrowed> CheckRandom(const Node& node, Scope scope, Type candidate,
                                        const std::set<std::string>& bound) {
        for (const NodePtr& child : node.children) {
            const std::optional<Narrowed> type = Check(*child, scope, candidate, bound);
            if (!type) {
                return std::nullopt;
            }
            if (type->type != Type::kInt) {
                Error("random needs ints");
                return std::nullopt;
            }
        }
        const int64_t* low = IntegerLiteral(*node.children[0]);
        const int64_t* high = IntegerLiteral(*node.children[1]);
        if (low != nullptr && high != nullptr && *low > *high) {
            Error("random needs low <= high");
            return std::nullopt;
        }
        return Narrowed{Type::kInt, bound, bound};
    }

    static const int64_t* IntegerLiteral(const Node& node) {
        if (node.kind != NodeKind::kLiteral) {
            return nullptr;
        }
        return std::get_if<int64_t>(&node.literal);
    }

    std::optional<Narrowed> CheckSelector(const Node& node,
                                          const std::set<std::string>& bound) {
        const std::optional<Type> type = SelectorType(node.text);
        if (!type) {
            Error("unknown selector @" + node.text,
                  SuggestClosest(node.text, SelectorNames()));
            return std::nullopt;
        }
        for (const NodePtr& filter : node.children) {
            if (!CheckCondition(*filter, EntityOf(*type), bound, true)) {
                return std::nullopt;
            }
        }
        return Narrowed{*type, bound, bound};
    }

    // INFO: a filter and the condition argument of a set function are both
    // boolean predicates over the candidate, so they share one rule; only the
    // message differs.
    std::optional<Type> CheckCondition(const Node& node, Type candidate,
                                       const std::set<std::string>& bound, bool is_filter) {
        const std::optional<Narrowed> type = Check(node, Scope::kCandidate, candidate, bound);
        if (!type) {
            return std::nullopt;
        }
        if (type->type != Type::kBool) {
            const std::string prefix =
                is_filter ? "filter must be a boolean condition" : "condition must be a boolean";
            Error(prefix + ", got " + TypeName(type->type));
            return std::nullopt;
        }
        return type->type;
    }

    std::optional<VariableInfo> FindVariable(const Node& node) {
        const auto variable = schema_.variables.find(node.text);
        if (variable == schema_.variables.end()) {
            Error("unknown variable $" + node.text,
                  SuggestClosest(node.text, VariableNames(schema_)));
            return std::nullopt;
        }
        return variable->second;
    }

    std::optional<Narrowed> CheckVariable(const Node& node,
                                          const std::set<std::string>& bound) {
        const std::optional<VariableInfo> variable = FindVariable(node);
        if (!variable) {
            return std::nullopt;
        }
        if (variable->optional && bound.count(node.text) == 0) {
            Error("variable $" + node.text + " may be unbound; guard it with defined($" +
                  node.text + ")");
            return std::nullopt;
        }
        return Narrowed{variable->type, bound, bound};
    }

    std::optional<Narrowed> CheckProperty(const Node& node, Scope scope, Type candidate,
                                          const std::set<std::string>& bound) {
        const std::optional<Narrowed> base = Check(*node.children[0], scope, candidate, bound);
        if (!base) {
            return std::nullopt;
        }
        if (IsSet(base->type)) {
            Error(SetReadMessage(base->type));
            return std::nullopt;
        }
        if (base->type == Type::kRule) {
            Error("cannot read properties of a rule");
            return std::nullopt;
        }
        if (schema_.Properties(base->type) == nullptr) {
            Error("cannot read \"" + node.text + "\" on a value of type " + TypeName(base->type));
            return std::nullopt;
        }
        return Plain(LookupProperty(base->type, node.text), bound);
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

    std::optional<Narrowed> CheckUnary(const Node& node, Scope scope, Type candidate,
                                       const std::set<std::string>& bound) {
        const std::optional<Narrowed> operand = Check(*node.children[0], scope, candidate, bound);
        if (!operand) {
            return std::nullopt;
        }
        if (node.text == "-") {
            if (!IsNumber(operand->type)) {
                Error("operator - needs numbers");
                return std::nullopt;
            }
            return Narrowed{operand->type, bound, bound};
        }
        if (operand->type != Type::kBool) {
            Error("operator ! needs a boolean");
            return std::nullopt;
        }
        // INFO: `!a` swaps the bindings that hold when it is true and false.
        return Narrowed{Type::kBool, operand->when_false, operand->when_true};
    }

    std::optional<Narrowed> CheckBinary(const Node& node, Scope scope, Type candidate,
                                        const std::set<std::string>& bound) {
        const std::string& symbol = node.text;
        if (symbol == "&&" || symbol == "||") {
            return CheckLogical(node, scope, candidate, bound, symbol == "&&");
        }
        const std::optional<Narrowed> left = Check(*node.children[0], scope, candidate, bound);
        if (!left) {
            return std::nullopt;
        }
        const std::optional<Narrowed> right = Check(*node.children[1], scope, candidate, bound);
        if (!right) {
            return std::nullopt;
        }

        if (symbol == "+" || symbol == "-" || symbol == "*" || symbol == "/") {
            if (!IsNumber(left->type) || !IsNumber(right->type)) {
                Error("operator " + symbol + " needs numbers");
                return std::nullopt;
            }
            if (symbol == "/" && IsZeroLiteral(*node.children[1])) {
                Error("division by zero");
                return std::nullopt;
            }
            return Narrowed{Widen(left->type, right->type), bound, bound};
        }
        if (symbol == "%") {
            if (left->type != Type::kInt || right->type != Type::kInt) {
                Error("operator % needs ints");
                return std::nullopt;
            }
            if (IsZeroLiteral(*node.children[1])) {
                Error("division by zero");
                return std::nullopt;
            }
            return Narrowed{Type::kInt, bound, bound};
        }
        if (symbol == "==" || symbol == "!=") {
            // INFO: numbers compare across int and float, so only a difference
            // between two other types is a mismatch.
            if (left->type != right->type &&
                (!IsNumber(left->type) || !IsNumber(right->type))) {
                Error("operator " + symbol + " cannot compare " + TypeName(left->type) +
                      " with " + TypeName(right->type));
                return std::nullopt;
            }
            return Narrowed{Type::kBool, bound, bound};
        }
        if (symbol == "<" || symbol == "<=" || symbol == ">" || symbol == ">=") {
            // INFO: v1 orders neither strings (byte order here, UTF-16 code
            // units in the phase 2b client) nor sets, so both need numbers.
            if (!IsNumber(left->type) || !IsNumber(right->type)) {
                if (IsSet(left->type) || IsSet(right->type) ||
                    left->type == Type::kString || right->type == Type::kString) {
                    Error("operator " + symbol + " needs numbers");
                } else {
                    Error("cannot compare " + TypeName(left->type) + " with " +
                          TypeName(right->type));
                }
                return std::nullopt;
            }
            return Narrowed{Type::kBool, bound, bound};
        }
        Error("unsupported operator " + symbol);
        return std::nullopt;
    }

    // INFO: `a && b` checks `b` with the variables `a` binds when true; `a || b`
    // checks `b` with the variables `a` binds when false, so `!defined($x) || ...`
    // binds `$x` for `b`.
    std::optional<Narrowed> CheckLogical(const Node& node, Scope scope, Type candidate,
                                         const std::set<std::string>& bound, bool is_and) {
        const std::optional<Narrowed> left = Check(*node.children[0], scope, candidate, bound);
        if (!left) {
            return std::nullopt;
        }
        if (left->type != Type::kBool) {
            Error("operator " + node.text + " needs booleans");
            return std::nullopt;
        }
        const std::set<std::string>& branch = is_and ? left->when_true : left->when_false;
        const std::optional<Narrowed> right = Check(*node.children[1], scope, candidate, branch);
        if (!right) {
            return std::nullopt;
        }
        if (right->type != Type::kBool) {
            Error("operator " + node.text + " needs booleans");
            return std::nullopt;
        }
        if (is_and) {
            return Narrowed{Type::kBool, right->when_true,
                            Intersect(left->when_false, right->when_false)};
        }
        return Narrowed{Type::kBool, Intersect(left->when_true, right->when_true),
                        right->when_false};
    }

    // INFO: the closed function table; each entry dispatches to a Checker
    // method, so a function missing from the table fails the lookup in
    // CheckCall instead of falling through. `maximum_arguments` is empty for
    // the variadic `min`/`max`. `all` accepts one argument so its missing
    // condition is reported as a signature error, not as a generic arity
    // error.
    struct FunctionInfo {
        std::string_view name;
        size_t minimum_arguments;
        std::optional<size_t> maximum_arguments;
        std::optional<Narrowed> (Checker::*check)(const Node&, Scope, Type,
                                                  const std::set<std::string>&);
    };

    static const std::vector<FunctionInfo>& Functions() {
        static const std::vector<FunctionInfo> functions = {
            {"if", 3, 3, &Checker::CheckIf},
            {"min", 2, std::nullopt, &Checker::CheckNumericArguments},
            {"max", 2, std::nullopt, &Checker::CheckNumericArguments},
            {"clamp", 3, 3, &Checker::CheckNumericArguments},
            {"abs", 1, 1, &Checker::CheckNumericArguments},
            {"floor", 1, 1, &Checker::CheckRounding},
            {"ceil", 1, 1, &Checker::CheckRounding},
            {"round", 1, 1, &Checker::CheckRounding},
            {"count", 1, 2, &Checker::CheckCount},
            {"any", 1, 2, &Checker::CheckAny},
            {"all", 1, 2, &Checker::CheckAll},
            {"sum", 2, 2, &Checker::CheckSum},
            {"contains", 2, 2, &Checker::CheckContains},
            {"shares", 2, 2, &Checker::CheckShares},
            {"has_tag", 2, 2, &Checker::CheckHasTag},
            {"defined", 1, 1, &Checker::CheckDefined},
            {"default", 2, 2, &Checker::CheckDefault},
            {"status_magnitude", 2, 2, &Checker::CheckStatusMagnitude},
            {"random", 2, 2, &Checker::CheckRandom},
        };
        return functions;
    }

    static const FunctionInfo* FindFunction(std::string_view name) {
        for (const FunctionInfo& function : Functions()) {
            if (function.name == name) {
                return &function;
            }
        }
        return nullptr;
    }

    static std::vector<std::string> FunctionNames() {
        std::vector<std::string> names;
        for (const FunctionInfo& function : Functions()) {
            names.emplace_back(function.name);
        }
        return names;
    }

    // INFO: a variadic function reports "at least", an exact-arity function
    // "takes", and a ranged one "takes between"; the variadic check runs
    // first because the variadic maximum is empty.
    static std::string ArityMessage(const FunctionInfo& function, size_t arity) {
        const size_t minimum = function.minimum_arguments;
        const std::string name(function.name);
        if (!function.maximum_arguments.has_value()) {
            return name + " takes at least " + std::to_string(minimum) + " arguments, got " +
                   std::to_string(arity);
        }
        const size_t maximum = *function.maximum_arguments;
        if (minimum == maximum) {
            const std::string noun = minimum == 1 ? " argument, got " : " arguments, got ";
            return name + " takes " + std::to_string(minimum) + noun + std::to_string(arity);
        }
        return name + " takes between " + std::to_string(minimum) + " and " +
               std::to_string(maximum) + " arguments, got " + std::to_string(arity);
    }

    const Schema& schema_;
    std::optional<Type> expected_;
    std::string_view file_;
    std::string_view pointer_;
    GuardState initial_;
    std::set<std::string> bound_when_true_;
    std::set<std::string> bound_when_false_;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace

CheckResult CheckExpression(std::string_view source, const Schema& schema,
                            std::optional<Type> expected, std::string_view file,
                            std::string_view pointer) {
    return CheckExpression(source, schema, expected, file, pointer, GuardState{});
}

CheckResult CheckExpression(std::string_view source, const Schema& schema,
                            std::optional<Type> expected, std::string_view file,
                            std::string_view pointer, const GuardState& initial) {
    const ParseResult parsed = ParseExpression(source);
    if (parsed.error) {
        CheckResult result;
        result.diagnostics.push_back(
            Diagnostic{std::string(file), std::string(pointer), FormatParseError(*parsed.error),
                       ""});
        return result;
    }
    return CheckParsedExpression(*parsed.root, schema, expected, file, pointer, initial);
}

CheckResult CheckParsedExpression(const Node& root, const Schema& schema,
                                  std::optional<Type> expected, std::string_view file,
                                  std::string_view pointer, const GuardState& initial) {
    CheckResult result;
    result.node_count = CountNodes(root);
    if (result.node_count > kMaximumExpressionNodes) {
        result.diagnostics.push_back(
            Diagnostic{std::string(file), std::string(pointer),
                      "expression has " + std::to_string(result.node_count) +
                          " nodes; the limit is " + std::to_string(kMaximumExpressionNodes),
                      ""});
        return result;
    }

    Checker checker(schema, expected, file, pointer, initial);
    const std::optional<Type> type = checker.CheckRoot(root);
    result.type = type;
    result.diagnostics = checker.TakeDiagnostics();
    // INFO: a checker path that returns no type must leave a reason behind; a
    // silent failure would otherwise be reported as a clean expression.
    if (!result.type.has_value() && result.diagnostics.empty()) {
        result.diagnostics.push_back(Diagnostic{std::string(file), std::string(pointer),
                                                "internal checker error", ""});
    }
    result.bound_when_true = checker.BoundWhenTrue();
    result.bound_when_false = checker.BoundWhenFalse();
    return result;
}

std::vector<Diagnostic> CheckAssignment(std::string_view source, const Schema& schema,
                                        std::string_view file, std::string_view pointer) {
    return CheckAssignment(source, schema, file, pointer, GuardState{});
}

std::vector<Diagnostic> CheckAssignment(std::string_view source, const Schema& schema,
                                        std::string_view file, std::string_view pointer,
                                        const GuardState& initial) {
    const AssignmentResult parsed = ParseAssignment(source);
    if (parsed.error) {
        return {Diagnostic{std::string(file), std::string(pointer), parsed.error->message, ""}};
    }
    const size_t node_count =
        CountNodes(*parsed.assignment->target) + CountNodes(*parsed.assignment->value);
    if (node_count > kMaximumExpressionNodes) {
        return {Diagnostic{std::string(file), std::string(pointer),
                           "expression has " + std::to_string(node_count) +
                               " nodes; the limit is " + std::to_string(kMaximumExpressionNodes),
                           ""}};
    }
    Checker checker(schema, std::nullopt, file, pointer, initial);
    return checker.CheckAssignment(*parsed.assignment);
}

}  // namespace match::expr
