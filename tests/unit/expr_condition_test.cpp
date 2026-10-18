#include <doctest/doctest.h>

#include <match/expr/condition.hpp>
#include <match/expr/diagnostic.hpp>
#include <match/expr/types.hpp>

#include <nlohmann/json.hpp>

#include <string_view>
#include <vector>

#include "expr_test_support.hpp"

namespace {

using json = nlohmann::json;
using match::expr::Condition;
using match::expr::ConditionParseResult;

constexpr const char* kFile = "mods/a/rules/x.json";

ConditionParseResult Parse(const json& value) {
    return match::expr::ParseCondition(value, kFile, "/when");
}

// INFO: parse tests and check tests share the pointer `/when` so a failure
// points at the rule slot that holds the condition.
std::vector<match::expr::Diagnostic> Check(const json& value) {
    const ConditionParseResult parsed = Parse(value);
    REQUIRE(parsed.condition.has_value());
    return match::expr::CheckCondition(*parsed.condition, match::expr::testing::MakeTestSchema(),
                                       kFile, "/when");
}

std::string FirstMessage(const std::vector<match::expr::Diagnostic>& diagnostics) {
    REQUIRE_FALSE(diagnostics.empty());
    return diagnostics.front().message;
}

TEST_SUITE("expr condition") {

TEST_CASE("a_leaf_string_is_an_expression_condition") {
    const ConditionParseResult parsed = Parse(json("defined($responder)"));
    REQUIRE(parsed.condition.has_value());
    CHECK(parsed.condition->kind == Condition::Kind::kExpression);
    CHECK(parsed.condition->source == "defined($responder)");
    CHECK(parsed.diagnostics.empty());
}

TEST_CASE("an_all_of_parses_to_one_child_per_entry") {
    const ConditionParseResult parsed =
        Parse(json::parse(R"json({"all_of":["a","b"]})json"));
    REQUIRE(parsed.condition.has_value());
    CHECK(parsed.condition->kind == Condition::Kind::kAllOf);
    REQUIRE(parsed.condition->children.size() == 2);
    CHECK(parsed.condition->children[0].kind == Condition::Kind::kExpression);
    CHECK(parsed.condition->children[0].source == "a");
    CHECK(parsed.condition->children[1].kind == Condition::Kind::kExpression);
    CHECK(parsed.condition->children[1].source == "b");
    CHECK(parsed.diagnostics.empty());
}

TEST_CASE("an_empty_group_needs_at_least_one_condition") {
    CHECK(FirstMessage(Parse(json::parse(R"json({"all_of":[]})json")).diagnostics) ==
          "all_of needs at least one condition");
}

TEST_CASE("a_condition_object_holds_exactly_one_group") {
    CHECK(FirstMessage(
              Parse(json::parse(R"json({"all_of":["x"],"any_of":["y"]})json")).diagnostics) ==
          "a condition object holds exactly one of all_of, any_of, none_of");
}

TEST_CASE("a_number_leaf_is_an_error") {
    CHECK(FirstMessage(Parse(json(5)).diagnostics) == "condition leaves are expression strings");
    CHECK(FirstMessage(Parse(json::parse(R"json({"all_of":[5]})json")).diagnostics) ==
          "condition leaves are expression strings");
}

TEST_CASE("all_of_guards_the_leaves_after_it") {
    CHECK(Check(json::parse(
                    R"json({"all_of":["defined($responder)","$responder.hand_size > 0"]})json"))
              .empty());
}

TEST_CASE("any_of_and_none_of_guard_the_leaves_after_them") {
    const std::vector<match::expr::Diagnostic> diagnostics =
        Check(json::parse(
            R"json({"any_of":["$responder.hand_size > 5","defined($responder)"]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().pointer == "/when/any_of/0");

    CHECK(Check(json::parse(
                    R"json({"any_of":["!defined($responder)","$responder.hand_size > 5"]})json"))
              .empty());
    CHECK(Check(json::parse(
                    R"json({"none_of":["!defined($responder)","$responder.hand_size == 0"]})json"))
              .empty());
}

TEST_CASE("a_nested_group_inherits_the_guards_of_its_earlier_siblings") {
    const json spec_example = json::parse(R"json(
        {"all_of": ["defined($responder)",
                    {"any_of": ["$responder.hand_size > 5", "@match.turn > 20"]}]}
    )json");
    CHECK(Check(spec_example).empty());
}

TEST_CASE("a_condition_leaf_must_be_bool") {
    const std::vector<match::expr::Diagnostic> diagnostics =
        Check(json::parse(R"json({"all_of":["@next.hand_size"]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().message == "condition must be bool, got int");
    CHECK(diagnostics.front().pointer == "/when/all_of/0");
}

}  // TEST_SUITE

}  // namespace
