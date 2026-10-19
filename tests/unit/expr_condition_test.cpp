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

std::string FirstPointer(const std::vector<match::expr::Diagnostic>& diagnostics) {
    REQUIRE_FALSE(diagnostics.empty());
    return diagnostics.front().pointer;
}

TEST_SUITE("expr condition") {

TEST_CASE("a_leaf_string_is_an_expression_condition") {
    const ConditionParseResult parsed = Parse(json("defined($responder)"));
    REQUIRE(parsed.condition.has_value());
    CHECK(parsed.condition->kind == Condition::Kind::kExpression);
    CHECK(parsed.condition->source == "defined($responder)");
    CHECK(parsed.condition->expression != nullptr);
    CHECK(parsed.diagnostics.empty());
}

TEST_CASE("a_leaf_expression_that_does_not_parse_is_rejected") {
    const ConditionParseResult parsed = Parse(json("1 +"));
    CHECK_FALSE(parsed.condition.has_value());
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(FirstPointer(parsed.diagnostics) == "/when");
}

TEST_CASE("a_leaf_over_the_node_budget_is_rejected") {
    std::string source = "1";
    for (int index = 0; index < 128; ++index) {
        source += " + 1";
    }
    const ConditionParseResult parsed = Parse(json(source));
    CHECK_FALSE(parsed.condition.has_value());
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().message == "expression has 257 nodes; the limit is 256");
    CHECK(parsed.diagnostics.front().pointer == "/when");

    const ConditionParseResult nested = Parse(json{{"all_of", json::array({source})}});
    REQUIRE_FALSE(nested.diagnostics.empty());
    CHECK(nested.diagnostics.front().pointer == "/when/all_of/0");
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
    const ConditionParseResult parsed = Parse(json::parse(R"json({"all_of":[]})json"));
    CHECK_FALSE(parsed.condition.has_value());
    CHECK(FirstMessage(parsed.diagnostics) == "all_of needs at least one condition");
    CHECK(FirstPointer(parsed.diagnostics) == "/when/all_of");
}

TEST_CASE("a_condition_object_holds_exactly_one_group") {
    const ConditionParseResult parsed =
        Parse(json::parse(R"json({"all_of":["x"],"any_of":["y"]})json"));
    CHECK_FALSE(parsed.condition.has_value());
    CHECK(FirstMessage(parsed.diagnostics) ==
          "a condition object holds exactly one of all_of, any_of, none_of");
    CHECK(FirstPointer(parsed.diagnostics) == "/when");
}

TEST_CASE("a_number_leaf_is_an_error") {
    const ConditionParseResult root = Parse(json(5));
    CHECK_FALSE(root.condition.has_value());
    CHECK(FirstMessage(root.diagnostics) == "condition leaves are expression strings");
    CHECK(FirstPointer(root.diagnostics) == "/when");

    const ConditionParseResult nested = Parse(json::parse(R"json({"all_of":[5]})json"));
    CHECK_FALSE(nested.condition.has_value());
    CHECK(FirstMessage(nested.diagnostics) == "condition leaves are expression strings");
    CHECK(FirstPointer(nested.diagnostics) == "/when/all_of/0");
}

TEST_CASE("an_unknown_key_in_a_group_is_an_error") {
    const ConditionParseResult parsed =
        Parse(json::parse(R"json({"all_of":["a"],"typo":1})json"));
    CHECK_FALSE(parsed.condition.has_value());
    REQUIRE(parsed.diagnostics.size() == 1);
    CHECK(parsed.diagnostics.front().message == "unknown key \"typo\"");
    CHECK(parsed.diagnostics.front().pointer == "/when/typo");
}

TEST_CASE("an_unknown_key_close_to_a_group_key_suggests_it") {
    const ConditionParseResult parsed =
        Parse(json::parse(R"json({"all_of":["a"],"none_off":["b"]})json"));
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().message == "unknown key \"none_off\"");
    CHECK(parsed.diagnostics.front().pointer == "/when/none_off");
    CHECK(parsed.diagnostics.front().suggestion == "none_of");
}

TEST_CASE("a_group_value_that_is_not_an_array_is_an_error") {
    const ConditionParseResult text = Parse(json::parse(R"json({"all_of":"a"})json"));
    CHECK_FALSE(text.condition.has_value());
    CHECK(FirstMessage(text.diagnostics) == "all_of must be an array of conditions");
    CHECK(FirstPointer(text.diagnostics) == "/when/all_of");

    const ConditionParseResult number = Parse(json::parse(R"json({"any_of":5})json"));
    CHECK_FALSE(number.condition.has_value());
    CHECK(FirstMessage(number.diagnostics) == "any_of must be an array of conditions");
    CHECK(FirstPointer(number.diagnostics) == "/when/any_of");

    const ConditionParseResult none = Parse(json::parse(R"json({"none_of":5})json"));
    CHECK_FALSE(none.condition.has_value());
    CHECK(FirstMessage(none.diagnostics) == "none_of must be an array of conditions");
    CHECK(FirstPointer(none.diagnostics) == "/when/none_of");
}

TEST_CASE("a_group_nested_to_the_limit_still_parses") {
    json node = json("leaf");
    for (int level = 0; level < 64; ++level) {
        node = json{{"all_of", json::array({node})}};
    }
    const ConditionParseResult parsed = Parse(node);
    REQUIRE(parsed.condition.has_value());
    CHECK(parsed.diagnostics.empty());
}

TEST_CASE("a_group_nested_past_the_limit_is_an_error") {
    json node = json("leaf");
    std::string offending_pointer = "/when";
    for (int level = 0; level < 70; ++level) {
        if (level < 64) {
            offending_pointer += "/all_of/0";
        }
        node = json{{"all_of", json::array({node})}};
    }
    const ConditionParseResult parsed = Parse(node);
    CHECK_FALSE(parsed.condition.has_value());
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().message ==
          "conditions are nested too deeply (more than 64 levels)");
    CHECK(parsed.diagnostics.front().pointer == offending_pointer);
}

TEST_CASE("every_bad_child_of_a_group_is_reported") {
    const ConditionParseResult parsed = Parse(json::parse(R"json({"all_of":[5,6]})json"));
    CHECK_FALSE(parsed.condition.has_value());
    REQUIRE(parsed.diagnostics.size() == 2);
    CHECK(parsed.diagnostics[0].pointer == "/when/all_of/0");
    CHECK(parsed.diagnostics[1].pointer == "/when/all_of/1");
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

TEST_CASE("an_all_of_leaf_without_its_guard_may_be_unbound") {
    const std::vector<match::expr::Diagnostic> diagnostics =
        Check(json::parse(R"json({"all_of":["$responder.hand_size > 0"]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().pointer == "/when/all_of/0");
    CHECK(diagnostics.front().message.find("may be unbound") != std::string::npos);
}

TEST_CASE("an_all_of_guard_after_the_leaf_does_not_narrow_it") {
    const std::vector<match::expr::Diagnostic> diagnostics = Check(json::parse(
        R"json({"all_of":["$responder.hand_size > 0","defined($responder)"]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().pointer == "/when/all_of/0");
    CHECK(diagnostics.front().message.find("may be unbound") != std::string::npos);
}

TEST_CASE("a_none_of_leaf_without_its_guard_may_be_unbound") {
    const std::vector<match::expr::Diagnostic> diagnostics =
        Check(json::parse(R"json({"none_of":["$responder.hand_size == 0"]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().pointer == "/when/none_of/0");
    CHECK(diagnostics.front().message.find("may be unbound") != std::string::npos);
}

TEST_CASE("a_none_of_guard_after_the_leaf_does_not_narrow_it") {
    const std::vector<match::expr::Diagnostic> diagnostics = Check(json::parse(
        R"json({"none_of":["$responder.hand_size == 0","!defined($responder)"]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().pointer == "/when/none_of/0");
    CHECK(diagnostics.front().message.find("may be unbound") != std::string::npos);
}

TEST_CASE("a_guard_inside_a_nested_group_does_not_narrow_later_siblings") {
    const std::vector<match::expr::Diagnostic> diagnostics = Check(json::parse(
        R"json({"all_of":[{"all_of":["defined($responder)"]},"$responder.hand_size > 0"]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().pointer == "/when/all_of/1");
}

TEST_CASE("a_guard_inside_a_none_of_group_does_not_narrow_later_siblings") {
    const std::vector<match::expr::Diagnostic> diagnostics = Check(json::parse(
        R"json({"all_of":[{"none_of":["!defined($responder)"]},"$responder.hand_size > 0"]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().pointer == "/when/all_of/1");
}

TEST_CASE("a_failing_check_points_into_the_nested_group") {
    const std::vector<match::expr::Diagnostic> diagnostics = Check(json::parse(
        R"json({"all_of":["defined($responder)",{"any_of":["@next.hand_size"]}]})json"));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().pointer == "/when/all_of/1/any_of/0");
    CHECK(diagnostics.front().message == "condition must be bool, got int");
}

}  // TEST_SUITE

}  // namespace
