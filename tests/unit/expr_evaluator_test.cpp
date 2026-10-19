#include <doctest/doctest.h>

#include <match/expr/condition.hpp>
#include <match/expr/evaluator.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <set>
#include <string>

#include "expr_fake_context.hpp"

namespace {

using match::expr::ConditionParseResult;
using match::expr::EntityRef;
using match::expr::EvalError;
using match::expr::EvalResult;
using match::expr::Evaluate;
using match::expr::EvaluateCondition;
using match::expr::ParseCondition;
using match::expr::ParseExpression;
using match::expr::Type;
using match::expr::Value;
using nlohmann::json;

Value ValueOf(std::string_view source, match::expr::testing::FakeContext& context) {
    const match::expr::ParseResult parsed = ParseExpression(source);
    REQUIRE_FALSE(parsed.error.has_value());
    REQUIRE(parsed.root != nullptr);
    const EvalResult result = Evaluate(*parsed.root, context);
    REQUIRE(std::holds_alternative<Value>(result));
    return std::get<Value>(result);
}

EvalError ErrorOf(std::string_view source, match::expr::testing::FakeContext& context) {
    const match::expr::ParseResult parsed = ParseExpression(source);
    REQUIRE_FALSE(parsed.error.has_value());
    REQUIRE(parsed.root != nullptr);
    const EvalResult result = Evaluate(*parsed.root, context);
    REQUIRE(std::holds_alternative<EvalError>(result));
    return std::get<EvalError>(result);
}

}  // namespace

TEST_SUITE("expr evaluator") {

TEST_CASE("arithmetic_follows_precedence_and_int_division_truncates") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<int64_t>(ValueOf("1 + 2 * 3", context)) == 7);
    CHECK(std::get<int64_t>(ValueOf("7 / 2", context)) == 3);
    CHECK(std::get<int64_t>(ValueOf("-7 / 2", context)) == -3);
    CHECK(std::get<double>(ValueOf("7.0 / 2", context)) == 3.5);
    CHECK(std::get<int64_t>(ValueOf("10 % 3", context)) == 1);
}

TEST_CASE("division_by_zero_on_a_zero_hand") {
    match::expr::testing::FakeContext context;
    context.hands[0] = 0;  // the next seat holds 0 cards
    const EvalError error = ErrorOf("@self.hand_size / @next.hand_size", context);
    CHECK(error.code == "division_by_zero");
}

TEST_CASE("integer_overflow_is_reported_not_wrapped") {
    match::expr::testing::FakeContext context;
    const EvalError error = ErrorOf("9223372036854775807 + 1", context);
    CHECK(error.code == "integer_overflow");
}

TEST_CASE("float_zero_divisor_and_overflow") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("0.0 / 0.0", context).code == "division_by_zero");
    const std::string huge = "1" + std::string(308, '0') + ".0";
    CHECK(ErrorOf(huge + " * 10.0", context).code == "float_overflow");
}

TEST_CASE("float_nan_without_division") {
    match::expr::testing::FakeContext context;
    context.variables["x"] = Value{std::numeric_limits<double>::quiet_NaN()};
    CHECK(ErrorOf("$x + 0.0", context).code == "not_a_number");
    CHECK(ErrorOf("-$x", context).code == "not_a_number");
    CHECK(ErrorOf("$x * 1.0", context).code == "not_a_number");
}

TEST_CASE("logical_operators_short_circuit") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<bool>(ValueOf("false && $nope.x > 1", context)) == false);
    CHECK(std::get<bool>(ValueOf("true || $nope.x", context)) == true);
}

TEST_CASE("unbound_variables_and_their_guards") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("$nope", context).code == "unbound_variable");
    CHECK(std::get<bool>(ValueOf("defined($nope)", context)) == false);
    CHECK(std::get<int64_t>(ValueOf("default($nope, 3)", context)) == 3);
}

TEST_CASE("string_equality_and_seat_property_comparison") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<bool>(ValueOf("'a' == 'a'", context)) == true);
    CHECK(std::get<bool>(ValueOf("@next.hand_size > 1", context)) == true);
}

TEST_CASE("an_unbound_selector_reports_empty_selector") {
    match::expr::testing::FakeContext context;
    context.unbound_next = true;
    const EvalError error = ErrorOf("@next.hand_size", context);
    CHECK(error.code == "empty_selector");
}


TEST_CASE("int_min_overflow_cases") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<int64_t>(ValueOf("-9223372036854775807 - 1", context)) ==
          std::numeric_limits<int64_t>::min());
    CHECK(ErrorOf("(-9223372036854775807 - 1) / -1", context).code == "integer_overflow");
    CHECK(ErrorOf("(-9223372036854775807 - 1) % -1", context).code == "integer_overflow");
    CHECK(ErrorOf("-(-9223372036854775807 - 1)", context).code == "integer_overflow");
    CHECK(ErrorOf("10 % 0", context).code == "division_by_zero");
}

TEST_CASE("type_mismatch_paths") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("1.0 % 2", context).code == "type_mismatch");
    CHECK(ErrorOf("!1", context).code == "type_mismatch");
    CHECK(ErrorOf("1 && true", context).code == "type_mismatch");
    CHECK(ErrorOf("1 + 'a'", context).code == "type_mismatch");
    CHECK(ErrorOf("1 == 'a'", context).code == "type_mismatch");
    CHECK(ErrorOf("1.x", context).code == "type_mismatch");
    CHECK(ErrorOf("@all.hand_size", context).code == "type_mismatch");
}

TEST_CASE("unknown_property_is_reported") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("@self.nope", context).code == "unknown_property");
}

TEST_CASE("bound_variable_reads_and_guards") {
    match::expr::testing::FakeContext context;
    context.variables["x"] = Value{7};
    CHECK(std::get<int64_t>(ValueOf("$x", context)) == 7);
    CHECK(std::get<bool>(ValueOf("defined($x)", context)) == true);
    CHECK(std::get<int64_t>(ValueOf("default($x, 9)", context)) == 7);
}

TEST_CASE("if_evaluates_only_taken_branch") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<int64_t>(ValueOf("if(true, 1, $nope.x)", context)) == 1);
    CHECK(std::get<int64_t>(ValueOf("if(false, $nope.x, 2)", context)) == 2);
}

TEST_CASE("string_ordering_and_member_set_equality") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<bool>(ValueOf("'a' < 'b'", context)) == true);
    context.variables["a"] = Value{std::set<std::string>{"x", "y"}};
    context.variables["b"] = Value{std::set<std::string>{"x", "y"}};
    CHECK(std::get<bool>(ValueOf("$a == $b", context)) == true);
    CHECK(std::get<bool>(ValueOf("$a != $b", context)) == false);
}

TEST_CASE("logical_evaluation_of_right_side_when_needed") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("true && $nope", context).code == "unbound_variable");
    CHECK(std::get<bool>(ValueOf("false && $nope", context)) == false);
}

TEST_CASE("chained_comparison_is_type_mismatch") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("1 < 2 < 3", context).code == "type_mismatch");
}

TEST_CASE("negation_of_true") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<bool>(ValueOf("!true", context)) == false);
}

TEST_CASE("large_int_comparison_is_exact") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<bool>(ValueOf("9007199254740993 == 9007199254740992", context)) == false);
    CHECK(std::get<bool>(ValueOf("9007199254740992 < 9007199254740993", context)) == true);
}

TEST_CASE("min_max_and_clamp_follow_the_checker_numeric_type") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<int64_t>(ValueOf("min(3, 2)", context)) == 2);
    CHECK(std::get<int64_t>(ValueOf("max(3, 2)", context)) == 3);
    CHECK(std::get<int64_t>(ValueOf("min(3, 1, 2)", context)) == 1);
    CHECK(std::get<double>(ValueOf("min(1, 2.0)", context)) == 1.0);
    CHECK(std::get<double>(ValueOf("max(1, 2.0)", context)) == 2.0);
    CHECK(std::get<double>(ValueOf("min(2.0, 1)", context)) == 1.0);
    CHECK(std::get<int64_t>(ValueOf("clamp(2, 1, 3)", context)) == 2);
    CHECK(std::get<int64_t>(ValueOf("clamp(5, 1, 3)", context)) == 3);
    CHECK(std::get<int64_t>(ValueOf("clamp(0, 1, 3)", context)) == 1);
    CHECK(std::get<double>(ValueOf("clamp(5, 1, 3.0)", context)) == 3.0);
    CHECK(std::get<double>(ValueOf("clamp(0, 1.0, 3)", context)) == 1.0);
}

TEST_CASE("abs_floor_ceil_and_round_cover_ints_floats_and_overflow") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<int64_t>(ValueOf("abs(-5)", context)) == 5);
    CHECK(std::get<int64_t>(ValueOf("abs(5)", context)) == 5);
    CHECK(std::get<double>(ValueOf("abs(-5.5)", context)) == 5.5);
    CHECK(ErrorOf("abs(-9223372036854775807 - 1)", context).code == "integer_overflow");
    CHECK(std::get<int64_t>(ValueOf("floor(2.7)", context)) == 2);
    CHECK(std::get<int64_t>(ValueOf("floor(-2.1)", context)) == -3);
    CHECK(std::get<int64_t>(ValueOf("ceil(2.1)", context)) == 3);
    CHECK(std::get<int64_t>(ValueOf("ceil(-2.9)", context)) == -2);
    CHECK(std::get<int64_t>(ValueOf("round(2.5)", context)) == 3);
    CHECK(std::get<int64_t>(ValueOf("round(-2.5)", context)) == -3);
    CHECK(std::get<int64_t>(ValueOf("floor(7)", context)) == 7);
}

TEST_CASE("float_to_int_conversion_bounds_are_checked") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("floor(9223372036854775808.0)", context).code == "integer_overflow");
    CHECK(std::get<int64_t>(ValueOf("floor(-9223372036854775808.0)", context)) ==
          std::numeric_limits<int64_t>::min());
    CHECK(std::get<int64_t>(ValueOf("ceil(-9223372036854775809.0)", context)) ==
          std::numeric_limits<int64_t>::min());
}

}  // TEST_SUITE

namespace {

EvalResult ConditionOf(const json& value, match::expr::testing::FakeContext& context) {
    const ConditionParseResult parsed = ParseCondition(value, "test", "/when");
    REQUIRE(parsed.condition.has_value());
    return EvaluateCondition(*parsed.condition, context);
}

Value ConditionValueOf(const json& value, match::expr::testing::FakeContext& context) {
    const EvalResult result = ConditionOf(value, context);
    REQUIRE(std::holds_alternative<Value>(result));
    return std::get<Value>(result);
}

}  // namespace

TEST_SUITE("expr sets") {

TEST_CASE("set_functions_over_the_four_seats") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<int64_t>(ValueOf("count(@all[hand_size > 3])", context)) == 2);
    CHECK(std::get<int64_t>(ValueOf("count(@others, hand_size == 1)", context)) == 0);
    CHECK(std::get<bool>(ValueOf("any(@all, hand_size == 1)", context)) == true);
    CHECK(std::get<bool>(ValueOf("all(@all, hand_size > 0)", context)) == true);
    CHECK(std::get<int64_t>(ValueOf("sum(@all, hand_size)", context)) == 12);
}

TEST_CASE("member_sets_share_and_compare_as_sets") {
    match::expr::testing::FakeContext context;
    context.variables["played"] = Value{context.played_card};
    const std::string shares_source = "shares($played.color, @match.uni:active_color)";
    const std::string equal_source = "$played.color == @match.uni:active_color";
    CHECK(std::get<bool>(ValueOf(shares_source, context)) == true);
    CHECK(std::get<bool>(ValueOf(equal_source, context)) == false);
    context.match_colors = {"uni:red", "uni:blue"};
    CHECK(std::get<bool>(ValueOf(equal_source, context)) == true);
    context.match_colors = {"uni:green"};
    CHECK(std::get<bool>(ValueOf(shares_source, context)) == false);
}

TEST_CASE("contains_and_has_tag_follow_the_context") {
    match::expr::testing::FakeContext context;
    context.variables["played"] = Value{context.played_card};
    CHECK(std::get<bool>(ValueOf("contains(@self.hand, $played)", context)) == true);
    CHECK(std::get<bool>(ValueOf("has_tag($played, 'uni:wild')", context)) == true);
    CHECK(std::get<bool>(ValueOf("has_tag($played, 'uni:calm')", context)) == false);
    context.hand_cards = {EntityRef{Type::kCard, 7}};
    CHECK(std::get<bool>(ValueOf("contains(@self.hand, $played)", context)) == false);
}

TEST_CASE("status_magnitude_random_and_rand_selector") {
    match::expr::testing::FakeContext context;
    context.status_magnitudes["uni:draw_debt"] = 3;
    CHECK(std::get<int64_t>(ValueOf("status_magnitude(@next, 'uni:draw_debt')", context)) == 3);
    context.random_queue.push_back(4);
    CHECK(std::get<int64_t>(ValueOf("random(1, 6)", context)) == 4);
    context.rand_entity = match::expr::testing::FakeContext::kBo;
    CHECK(std::get<EntityRef>(ValueOf("@rand", context)) ==
          match::expr::testing::FakeContext::kBo);
}

TEST_CASE("evaluate_condition_groups_combine_and_short_circuit") {
    match::expr::testing::FakeContext context;
    const json guarded = json::parse(
        R"json({"all_of":["defined($responder)","$responder.hand_size > 0"]})json");
    CHECK(std::get<bool>(ConditionValueOf(guarded, context)) == false);
    const json none_of = json::parse(R"json({"none_of":["false","false"]})json");
    CHECK(std::get<bool>(ConditionValueOf(none_of, context)) == true);
    const json any_of = json::parse(R"json({"any_of":["false","true"]})json");
    CHECK(std::get<bool>(ConditionValueOf(any_of, context)) == true);
}

TEST_CASE("runtime_errors_in_set_functions") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("count(@all[hand_size > $missing])", context).code == "unbound_variable");

    match::expr::testing::FakeContext empty;
    empty.unbound_next = true;
    const std::string no_seats = "@others[hand_size > 100]";
    CHECK(std::get<int64_t>(ValueOf("sum(" + no_seats + ", hand_size)", empty)) == 0);
    CHECK(std::get<bool>(ValueOf("any(" + no_seats + ", hand_size > 0)", empty)) == false);
    CHECK(std::get<bool>(ValueOf("all(" + no_seats + ", hand_size > 0)", empty)) == true);
    CHECK(ErrorOf("sum(@all, @next.hand_size)", empty).code == "empty_selector");
}

TEST_CASE("condition_leaf_errors_are_returned_and_groups_short_circuit") {
    match::expr::testing::FakeContext context;
    const EvalResult leaf = ConditionOf(json("$missing"), context);
    REQUIRE(std::holds_alternative<EvalError>(leaf));
    CHECK(std::get<EvalError>(leaf).code == "unbound_variable");

    const json all_of = json::parse(R"json({"all_of":["false","$missing"]})json");
    CHECK(std::get<bool>(ConditionValueOf(all_of, context)) == false);
    const json any_of = json::parse(R"json({"any_of":["true","$missing"]})json");
    CHECK(std::get<bool>(ConditionValueOf(any_of, context)) == true);
}

TEST_CASE("contains_over_a_member_set_is_a_subset_test") {
    match::expr::testing::FakeContext context;
    context.variables["colors"] = Value{std::set<std::string>{"uni:red", "uni:blue"}};
    context.variables["choice"] = Value{std::set<std::string>{"uni:red"}};
    context.variables["empty"] = Value{std::set<std::string>{}};
    CHECK(std::get<bool>(ValueOf("contains($colors, $choice)", context)) == true);
    CHECK(std::get<bool>(ValueOf("contains($colors, $empty)", context)) == true);
    context.variables["choice"] = Value{std::set<std::string>{"uni:green"}};
    CHECK(std::get<bool>(ValueOf("contains($colors, $choice)", context)) == false);
    CHECK(ErrorOf("contains($colors, 1)", context).code == "type_mismatch");
}

TEST_CASE("count_and_any_over_a_member_set") {
    match::expr::testing::FakeContext context;
    context.variables["colors"] = Value{std::set<std::string>{"uni:red", "uni:blue"}};
    context.variables["empty"] = Value{std::set<std::string>{}};
    CHECK(std::get<int64_t>(ValueOf("count($colors)", context)) == 2);
    CHECK(std::get<bool>(ValueOf("any($colors)", context)) == true);
    CHECK(std::get<int64_t>(ValueOf("count($empty)", context)) == 0);
    CHECK(std::get<bool>(ValueOf("any($empty)", context)) == false);
    CHECK(ErrorOf("all($colors, true)", context).code == "type_mismatch");
    CHECK(ErrorOf("sum($colors, 1)", context).code == "type_mismatch");
}

TEST_CASE("an_empty_selector_as_a_single_entity_argument_is_reported") {
    match::expr::testing::FakeContext context;
    context.unbound_next = true;
    CHECK(ErrorOf("status_magnitude(@next, 'uni:draw_debt')", context).code == "empty_selector");
    CHECK(ErrorOf("has_tag(@next, 'uni:wild')", context).code == "empty_selector");
    CHECK(ErrorOf("contains(@self.hand, @next)", context).code == "empty_selector");
}

TEST_CASE("a_filter_keeps_self_and_nested_filters_correct") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<int64_t>(ValueOf("count(@all[hand_size > @self.hand_size])", context)) == 3);
    const std::string nested_source = "count(@all[hand_size > count(@all[hand_size < 3])])";
    CHECK(std::get<int64_t>(ValueOf(nested_source, context)) == 2);
}

TEST_CASE("a_filter_on_an_empty_set_selector_is_an_empty_set") {
    match::expr::testing::FakeContext context;
    CHECK(std::get<int64_t>(ValueOf("count(@others[hand_size > 100])", context)) == 0);
}

TEST_CASE("seat_sets_compare_as_sets_not_in_context_order") {
    match::expr::testing::FakeContext context;
    context.variables["seats"] =
        Value{std::vector<EntityRef>{match::expr::testing::FakeContext::kDi,
                                     match::expr::testing::FakeContext::kCy,
                                     match::expr::testing::FakeContext::kBo,
                                     match::expr::testing::FakeContext::kAna}};
    CHECK(std::get<bool>(ValueOf("$seats == @all", context)) == true);
    context.variables["seats"] =
        Value{std::vector<EntityRef>{match::expr::testing::FakeContext::kDi}};
    CHECK(std::get<bool>(ValueOf("$seats == @all", context)) == false);
}

TEST_CASE("an_unbound_single_selector_in_comparison_is_empty_selector") {
    match::expr::testing::FakeContext context;
    context.unbound_next = true;
    CHECK(ErrorOf("@next == @self", context).code == "empty_selector");
}

TEST_CASE("random_rejects_an_empty_range") {
    match::expr::testing::FakeContext context;
    context.random_queue.push_back(4);
    CHECK(ErrorOf("random(6, 1)", context).code == "invalid_range");
    CHECK(context.random_queue.size() == 1);
}

TEST_CASE("random_draws_one_value_per_call") {
    match::expr::testing::FakeContext context;
    context.random_queue.push_back(4);
    context.random_queue.push_back(2);
    CHECK(std::get<int64_t>(ValueOf("random(1, 6) + random(1, 6)", context)) == 6);
}

}  // TEST_SUITE
