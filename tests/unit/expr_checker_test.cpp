#include <doctest/doctest.h>

#include <match/expr/checker.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "expr_test_support.hpp"

namespace {

using match::expr::CheckResult;
using match::expr::Type;

CheckResult Check(std::string_view source, std::optional<Type> expected = std::nullopt) {
    return match::expr::CheckExpression(source, match::expr::testing::MakeTestSchema(), expected,
                                        "mods/a/rules/x.json", "/rule/when");
}

std::string FirstMessage(const CheckResult& result) {
    REQUIRE_FALSE(result.diagnostics.empty());
    return result.diagnostics.front().message;
}

bool Mentions(const CheckResult& result, std::string_view fragment) {
    return FirstMessage(result).find(fragment) != std::string::npos;
}

std::string RepeatedAddition(size_t repetitions) {
    std::string source = "1";
    for (size_t index = 0; index < repetitions; ++index) {
        source += " + 1";
    }
    return source;
}

}  // namespace

TEST_SUITE("expr checker") {

TEST_CASE("arithmetic_follows_its_operand_types") {
    const CheckResult seat_property = Check("@next.hand_size + 1");
    REQUIRE(seat_property.diagnostics.empty());
    CHECK(seat_property.type == Type::kInt);

    REQUIRE(Check("1 + 2.5").diagnostics.empty());
    CHECK(Check("1 + 2.5").type == Type::kFloat);
    REQUIRE(Check("7 / 2").diagnostics.empty());
    CHECK(Check("7 / 2").type == Type::kInt);
    REQUIRE(Check("7.0 / 2").diagnostics.empty());
    CHECK(Check("7.0 / 2").type == Type::kFloat);
    REQUIRE(Check("10 % 3").diagnostics.empty());
    CHECK(Check("10 % 3").type == Type::kInt);
}

TEST_CASE("modulo_takes_two_integers") {
    CHECK(Mentions(Check("10.5 % 3"), "operator % needs ints"));
}

TEST_CASE("unknown_property_suggests_the_closest_name") {
    const CheckResult result = Check("@next.hand_sze");
    CHECK(Mentions(result, "unknown property \"hand_sze\" on seat"));
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().suggestion == "hand_size");
    CHECK_FALSE(result.type.has_value());
}

TEST_CASE("reading_a_property_of_a_seat_set_is_an_error") {
    CHECK(Mentions(Check("@all.hand_size"),
                   "selects many seats; use count(), any(), all(), sum() or a "
                   "single-seat selector"));
}

TEST_CASE("expected_int_rejects_a_float") {
    CHECK(Mentions(Check("1.5", Type::kInt),
                   "expected int, got float (use floor(), ceil() or round())"));
}

TEST_CASE("member_sets_compare_by_equality_only") {
    const CheckResult equal = Check("$played.color == @match.uni:active_color");
    REQUIRE(equal.diagnostics.empty());
    CHECK(equal.type == Type::kBool);
    CHECK(Mentions(Check("$played.color < @match.uni:active_color"),
                   "operator < needs numbers"));
}

TEST_CASE("unknown_variable_and_bare_name") {
    CHECK(Mentions(Check("$nope"), "unknown variable $nope"));
    CHECK(Mentions(Check("hand_size"),
                   "bare name 'hand_size' is only valid inside [ ] or a set function "
                   "condition"));
}

TEST_CASE("filter_over_a_seat_set_reads_candidate_properties") {
    const CheckResult result = Check("@all[hand_size > 3]");
    REQUIRE(result.diagnostics.empty());
    CHECK(result.type == Type::kSeatSet);
}

TEST_CASE("operators_reject_wrong_operand_types") {
    CHECK(Mentions(Check("'a' + 1"), "operator + needs numbers"));
    CHECK(Mentions(Check("1 < 2 < 3"), "cannot compare bool with int"));
}

TEST_CASE("division_by_zero_is_an_error") {
    CHECK(Mentions(Check("5 / 0"), "division by zero"));
}

TEST_CASE("node_budget_is_256_nodes") {
    const CheckResult too_large = Check(RepeatedAddition(128));
    CHECK(Mentions(too_large, "expression has 257 nodes; the limit is 256"));

    const CheckResult at_budget = Check(RepeatedAddition(127));
    REQUIRE(at_budget.diagnostics.empty());
    CHECK(at_budget.type == Type::kInt);
    CHECK(at_budget.node_count == 255);
}

TEST_CASE("a_parse_error_leaves_the_node_count_at_zero") {
    CHECK(Check("1 +").node_count == 0);
    CHECK(Check("").node_count == 0);
}

TEST_CASE("an_int_is_accepted_where_a_float_is_expected") {
    const CheckResult result = Check("1", Type::kFloat);
    REQUIRE(result.diagnostics.empty());
    CHECK(result.type == Type::kInt);
}

TEST_CASE("expected_bool_rejects_an_int") {
    CHECK(Mentions(Check("1", Type::kBool), "expected bool, got int"));
}

TEST_CASE("modulo_by_zero_is_an_error") {
    CHECK(Mentions(Check("5 % 0"), "division by zero"));
}

TEST_CASE("equality_between_different_types_is_an_error") {
    CHECK(Mentions(Check("1 == 'a'"), "operator == cannot compare int with string"));
}

TEST_CASE("an_unknown_selector_is_reported") {
    CHECK(Mentions(Check("@nope"), "unknown selector @nope"));

    const CheckResult near_miss = Check("@nex");
    REQUIRE_FALSE(near_miss.diagnostics.empty());
    CHECK(near_miss.diagnostics.front().suggestion == "next");
}

TEST_CASE("a_bare_name_in_a_filter_must_be_a_candidate_property") {
    CHECK(Mentions(Check("@all[nope]"), "unknown property \"nope\" on seat"));
}

TEST_CASE("reading_a_property_of_a_member_set_is_an_error") {
    CHECK(Mentions(Check("@match.uni:active_color.color"),
                   "reading a property of a member set is not allowed; use count(), "
                   "any(), all(), sum(), contains() or shares()"));
}

TEST_CASE("a_rule_has_no_properties") {
    CHECK(Mentions(Check("$rule.turn"), "cannot read properties of a rule"));
}

TEST_CASE("selectors_have_their_documented_types") {
    const CheckResult others = Check("@others");
    REQUIRE(others.diagnostics.empty());
    CHECK(others.type == Type::kSeatSet);

    const CheckResult random_seat = Check("@rand");
    REQUIRE(random_seat.diagnostics.empty());
    CHECK(random_seat.type == Type::kSeat);
}

TEST_CASE("unary_operators_need_their_operand_type") {
    CHECK(Mentions(Check("-'a'"), "operator - needs numbers"));
    CHECK(Mentions(Check("!1"), "operator ! needs a boolean"));
}

TEST_CASE("a_property_read_needs_an_entity") {
    CHECK(Mentions(Check("1.hand_size"), "cannot read \"hand_size\" on a value of type int"));
}

TEST_CASE("logical_operators_need_booleans") {
    CHECK(Mentions(Check("'a' && 1"), "operator && needs booleans"));
    CHECK(Mentions(Check("1 && 2"), "operator && needs booleans"));
    CHECK(Mentions(Check("@all && true"), "operator && needs booleans"));
    CHECK(Mentions(Check("'a' || 1"), "operator || needs booleans"));
}

TEST_CASE("a_filter_must_be_a_boolean_condition") {
    CHECK(Mentions(Check("@all[hand_size + 1]"),
                   "filter must be a boolean condition, got int"));
    CHECK(Mentions(Check("@all['x']"), "filter must be a boolean condition, got string"));
}

}  // TEST_SUITE

TEST_SUITE("expr functions") {

TEST_CASE("if_widens_its_branch_types") {
    const CheckResult mixed = Check("if(@next.hand_size > 2, 1, 2.5)");
    REQUIRE(mixed.diagnostics.empty());
    CHECK(mixed.type == Type::kFloat);

    CHECK(Mentions(Check("if(1, 2, 3)"), "if needs a bool condition"));
}

TEST_CASE("rounding_and_clamp_keep_the_number_kind") {
    const CheckResult rounded = Check("floor(2.5)");
    REQUIRE(rounded.diagnostics.empty());
    CHECK(rounded.type == Type::kInt);

    const CheckResult clamped = Check("clamp(@next.hand_size, 0, 5)");
    REQUIRE(clamped.diagnostics.empty());
    CHECK(clamped.type == Type::kInt);
}

TEST_CASE("set_aggregations_return_their_types") {
    const CheckResult filtered = Check("count(@all[hand_size > 3])");
    REQUIRE(filtered.diagnostics.empty());
    CHECK(filtered.type == Type::kInt);

    const CheckResult conditioned = Check("count(@all, hand_size > 3)");
    REQUIRE(conditioned.diagnostics.empty());
    CHECK(conditioned.type == Type::kInt);

    CHECK(Mentions(Check("all(@all)"), "all needs a condition"));

    const CheckResult summed = Check("sum(@all, hand_size)");
    REQUIRE(summed.diagnostics.empty());
    CHECK(summed.type == Type::kInt);
}

TEST_CASE("membership_and_tags_return_bool") {
    const CheckResult overlap = Check("shares($played.color, @match.uni:active_color)");
    REQUIRE(overlap.diagnostics.empty());
    CHECK(overlap.type == Type::kBool);

    const CheckResult tagged = Check("has_tag($played, 'uni:wild')");
    REQUIRE(tagged.diagnostics.empty());
    CHECK(tagged.type == Type::kBool);
}

TEST_CASE("defined_and_default_follow_their_arguments") {
    const CheckResult bound = Check("defined($responder)");
    REQUIRE(bound.diagnostics.empty());
    CHECK(bound.type == Type::kBool);

    CHECK(Mentions(Check("defined(@next)"), "defined needs a $variable"));

    const CheckResult fallback = Check("default($choice, @match.uni:active_color)");
    REQUIRE(fallback.diagnostics.empty());
    CHECK(fallback.type == Type::kMemberSet);
}

TEST_CASE("status_magnitude_and_random_return_int") {
    const CheckResult magnitude = Check("status_magnitude(@next, 'uni:draw_debt')");
    REQUIRE(magnitude.diagnostics.empty());
    CHECK(magnitude.type == Type::kInt);

    const CheckResult roll = Check("random(1, 6)");
    REQUIRE(roll.diagnostics.empty());
    CHECK(roll.type == Type::kInt);

    CHECK(Mentions(Check("random(1)"), "random takes 2 arguments, got 1"));
}

TEST_CASE("an_unknown_function_is_reported_with_a_suggestion") {
    CHECK(Mentions(Check("frobnicate(1)"), "unknown function \"frobnicate\""));

    const CheckResult near_miss = Check("rnadom(1, 2)");
    REQUIRE_FALSE(near_miss.diagnostics.empty());
    CHECK(near_miss.diagnostics.front().suggestion == "random");
}

TEST_CASE("function_signature_errors_name_their_expectation") {
    CHECK(Mentions(Check("count(1)"), "count needs a set"));
    CHECK(Mentions(Check("sum(@all, 'x')"), "sum needs a number expression"));
    CHECK(Mentions(Check("contains(@all, 1)"), "contains needs a seat for a seat_set"));
    CHECK(Mentions(Check("shares(1, 2)"), "shares needs member sets"));
    CHECK(Mentions(Check("has_tag(@next, 'x')"), "has_tag needs a card or a rule"));
    CHECK(Mentions(Check("if(true, 1, 'a')"), "if needs matching branch types"));
    CHECK(Mentions(Check("random(1.5, 2)"), "random needs ints"));
    CHECK(Mentions(Check("min(1)"), "min takes at least 2 arguments, got 1"));
    CHECK(Mentions(Check("clamp(1, 2)"), "clamp takes 3 arguments, got 2"));
    CHECK(Mentions(Check("status_magnitude(1, 'x')"), "status_magnitude needs a seat"));
    CHECK(Mentions(Check("abs('a')"), "abs needs numbers"));
}

TEST_CASE("membership_and_numeric_functions_accept_their_kinds") {
    const CheckResult hand = Check("contains(@next.hand, $played)");
    REQUIRE(hand.diagnostics.empty());
    CHECK(hand.type == Type::kBool);

    const CheckResult seats = Check("contains(@all, @next)");
    REQUIRE(seats.diagnostics.empty());
    CHECK(seats.type == Type::kBool);

    const CheckResult colors =
        Check("contains(default($choice, @match.uni:active_color), @match.uni:active_color)");
    REQUIRE(colors.diagnostics.empty());
    CHECK(colors.type == Type::kBool);

    const CheckResult any_match = Check("any(@all, hand_size > 3)");
    REQUIRE(any_match.diagnostics.empty());
    CHECK(any_match.type == Type::kBool);

    const CheckResult minimum = Check("min(1, 2)");
    REQUIRE(minimum.diagnostics.empty());
    CHECK(minimum.type == Type::kInt);

    const CheckResult maximum = Check("max(1.5, 2)");
    REQUIRE(maximum.diagnostics.empty());
    CHECK(maximum.type == Type::kFloat);

    const CheckResult absolute = Check("abs(@next.hand_size)");
    REQUIRE(absolute.diagnostics.empty());
    CHECK(absolute.type == Type::kInt);

    const CheckResult ceiled = Check("ceil(1.5)");
    REQUIRE(ceiled.diagnostics.empty());
    CHECK(ceiled.type == Type::kInt);

    const CheckResult rounded = Check("round(@next.hand_size)");
    REQUIRE(rounded.diagnostics.empty());
    CHECK(rounded.type == Type::kInt);
}

}  // TEST_SUITE

TEST_SUITE("expr guards") {

TEST_CASE("an_unguarded_optional_variable_is_an_error") {
    CHECK(Mentions(Check("$responder.hand_size"),
                   "variable $responder may be unbound; guard it with defined($responder)"));
}

TEST_CASE("a_short_circuit_guard_binds_its_variable") {
    const CheckResult conjunction = Check("defined($responder) && $responder.hand_size > 0");
    REQUIRE(conjunction.diagnostics.empty());

    const CheckResult disjunction = Check("!defined($responder) || $responder.hand_size > 0");
    REQUIRE(disjunction.diagnostics.empty());
}

TEST_CASE("a_true_branch_binds_only_within_the_operator_that_guards_it") {
    CHECK(Mentions(Check("defined($responder) || $responder.hand_size > 0"),
                   "variable $responder may be unbound; guard it with defined($responder)"));
}

TEST_CASE("if_guards_its_branches_from_its_condition") {
    const CheckResult result = Check("if(defined($responder), $responder.hand_size, 0)");
    REQUIRE(result.diagnostics.empty());
    CHECK(result.type == Type::kInt);
}

TEST_CASE("default_accepts_an_optional_variable") {
    const CheckResult result = Check("default($responder, @self).hand_size");
    REQUIRE(result.diagnostics.empty());
    CHECK(result.type == Type::kInt);
}

TEST_CASE("an_always_defined_variable_needs_no_guard") {
    const CheckResult result = Check("$played.color");
    REQUIRE(result.diagnostics.empty());
    CHECK(result.type == Type::kMemberSet);
}

}  // TEST_SUITE
