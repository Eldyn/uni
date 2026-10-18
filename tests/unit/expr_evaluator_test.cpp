#include <doctest/doctest.h>

#include <match/expr/evaluator.hpp>

#include <cstdint>
#include <limits>
#include <set>
#include <string>

#include "expr_fake_context.hpp"

namespace {

using match::expr::EvalError;
using match::expr::EvalResult;
using match::expr::Evaluate;
using match::expr::ParseExpression;
using match::expr::Value;

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

}  // TEST_SUITE
