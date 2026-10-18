#include <match/expr/condition.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <match/expr/checker.hpp>

namespace match::expr {
namespace {

std::string_view GroupKey(Condition::Kind kind) {
    switch (kind) {
        case Condition::Kind::kExpression:
            return "";
        case Condition::Kind::kAllOf:
            return "all_of";
        case Condition::Kind::kAnyOf:
            return "any_of";
        case Condition::Kind::kNoneOf:
            return "none_of";
    }
    return "";
}

// INFO: the guard bindings a checked condition guarantees. A group returns
// nothing of its own; only its leaves guard the leaves that follow them.
struct Narrowed {
    std::set<std::string> when_true;
    std::set<std::string> when_false;
};

class ConditionParser {
public:
    explicit ConditionParser(std::string_view file) : file_(std::string(file)) {}

    ConditionParseResult Parse(const json& value, const std::string& pointer) {
        std::optional<Condition> condition = ParseNode(value, pointer);
        return ConditionParseResult{std::move(condition), std::move(diagnostics_)};
    }

private:
    void Error(const std::string& pointer, std::string message) {
        diagnostics_.push_back(Diagnostic{file_, pointer, std::move(message), ""});
    }

    std::optional<Condition> ParseNode(const json& value, const std::string& pointer) {
        if (value.is_string()) {
            return Condition{Condition::Kind::kExpression, value.get<std::string>(), {}};
        }
        if (!value.is_object()) {
            Error(pointer, "condition leaves are expression strings");
            return std::nullopt;
        }
        const std::optional<std::pair<Condition::Kind, std::string_view>> group = FindGroup(value);
        if (!group) {
            Error(pointer, "a condition object holds exactly one of all_of, any_of, none_of");
            return std::nullopt;
        }
        const json& children = value[group->second];
        if (!children.is_array() || children.empty()) {
            Error(std::string(pointer) + "/" + std::string(group->second),
                  std::string(group->second) + " needs at least one condition");
            return std::nullopt;
        }
        Condition node{group->first, {}, {}};
        bool ok = true;
        for (size_t index = 0; index < children.size(); ++index) {
            const std::string child_pointer =
                std::string(pointer) + "/" + std::string(group->second) + "/" +
                std::to_string(index);
            std::optional<Condition> child = ParseNode(children[index], child_pointer);
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

    std::optional<std::pair<Condition::Kind, std::string_view>> FindGroup(const json& object) {
        static constexpr std::array<std::pair<std::string_view, Condition::Kind>, 3> kGroups = {
            {{"all_of", Condition::Kind::kAllOf},
             {"any_of", Condition::Kind::kAnyOf},
             {"none_of", Condition::Kind::kNoneOf}}};
        std::optional<std::pair<Condition::Kind, std::string_view>> found;
        size_t matches = 0;
        for (const auto& [key, kind] : kGroups) {
            if (object.contains(key)) {
                ++matches;
                found = {kind, key};
            }
        }
        if (matches == 1) {
            return found;
        }
        return std::nullopt;
    }

    std::string file_;
    std::vector<Diagnostic> diagnostics_;
};

class ConditionChecker {
public:
    ConditionChecker(const Schema& schema, std::string_view file,
                     std::vector<Diagnostic>& diagnostics)
        : schema_(schema), file_(std::string_view(file)), diagnostics_(diagnostics) {}

    void Check(const Condition& condition, const std::string& pointer) {
        CheckNode(condition, pointer, {});
    }

private:
    void Error(const std::string& pointer, std::string message) {
        diagnostics_.push_back(Diagnostic{std::string(file_), pointer, std::move(message), ""});
    }

    void Report(const std::vector<Diagnostic>& diagnostics) {
        for (const Diagnostic& diagnostic : diagnostics) {
            diagnostics_.push_back(diagnostic);
        }
    }

    std::optional<Narrowed> CheckNode(const Condition& condition, const std::string& pointer,
                                      const std::set<std::string>& guaranteed) {
        if (condition.kind == Condition::Kind::kExpression) {
            return CheckLeaf(condition, pointer, guaranteed);
        }
        const bool binds_when_true = condition.kind == Condition::Kind::kAllOf;
        std::set<std::string> bound = guaranteed;
        bool ok = true;
        for (size_t index = 0; index < condition.children.size(); ++index) {
            const Condition& child = condition.children[index];
            const std::string child_pointer = std::string(pointer) + "/" +
                                              std::string(GroupKey(condition.kind)) + "/" +
                                              std::to_string(index);
            const std::optional<Narrowed> checked =
                CheckNode(child, child_pointer, bound);
            if (!checked) {
                ok = false;
                continue;
            }
            if (child.kind == Condition::Kind::kExpression) {
                const std::set<std::string>& branch =
                    binds_when_true ? checked->when_true : checked->when_false;
                bound.insert(branch.begin(), branch.end());
            }
        }
        if (!ok) {
            return std::nullopt;
        }
        return Narrowed{};
    }

    std::optional<Narrowed> CheckLeaf(const Condition& condition, const std::string& pointer,
                                      const std::set<std::string>& guaranteed) {
        const CheckResult result = CheckExpression(condition.source, schema_, std::nullopt, file_,
                                                   pointer, GuardState{guaranteed});
        if (!result.diagnostics.empty()) {
            Report(result.diagnostics);
            return std::nullopt;
        }
        if (!result.type.has_value()) {
            return std::nullopt;
        }
        if (*result.type != Type::kBool) {
            Error(pointer, "condition must be bool, got " + TypeName(*result.type));
            return std::nullopt;
        }
        return Narrowed{result.bound_when_true, result.bound_when_false};
    }

    std::string TypeName(Type type) const {
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

    const Schema& schema_;
    std::string_view file_;
    std::vector<Diagnostic>& diagnostics_;
};

}  // namespace

ConditionParseResult ParseCondition(const json& value, std::string_view file,
                                    std::string_view pointer) {
    ConditionParser parser(file);
    return parser.Parse(value, std::string(pointer));
}

std::vector<Diagnostic> CheckCondition(const Condition& condition, const Schema& schema,
                                       std::string_view file, std::string_view pointer) {
    std::vector<Diagnostic> diagnostics;
    ConditionChecker checker(schema, file, diagnostics);
    checker.Check(condition, std::string(pointer));
    return diagnostics;
}

}  // namespace match::expr
