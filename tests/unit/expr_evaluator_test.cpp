#include <doctest/doctest.h>

#include <match/expr/evaluator.hpp>

#include <cstdint>
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

TEST_CASE("float_zero_divisor_and_runtime_nan") {
    match::expr::testing::FakeContext context;
    CHECK(ErrorOf("0.0 / 0.0", context).code == "division_by_zero");
    // INFO: 1e308 * 1e308 widens to inf, and inf / inf is a runtime NaN.
    const std::string huge = "1" + std::string(308, '0') + ".0";
    const std::string source =
        "(" + huge + " * " + huge + ") / (" + huge + " * " + huge + ")";
    CHECK(ErrorOf(source, context).code == "not_a_number");
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

}  // TEST_SUITE
