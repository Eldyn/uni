#include <doctest/doctest.h>

#include <match/expr/parser.hpp>

#include <string>

namespace {

std::string Debug(std::string_view source) {
    const match::expr::ParseResult result = match::expr::ParseExpression(source);
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.root != nullptr);
    return match::expr::DebugString(*result.root);
}

}  // namespace

TEST_SUITE("expr parser") {

TEST_CASE("multiplication_binds_tighter_than_addition") {
    CHECK(Debug("1 + 2 * 3") == "(+ 1 (* 2 3))");
}

TEST_CASE("comparison_binds_tighter_than_logical_and") {
    CHECK(Debug("hand_size > 3 && $x") == "(&& (> hand_size 3) $x)");
}

TEST_CASE("unary_not_over_a_call") {
    CHECK(Debug("!defined($r)") == "(! (call defined $r))");
}

TEST_CASE("selector_with_filter") {
    CHECK(Debug("@all[hand_size > 3]") == "(select all (> hand_size 3))");
}

TEST_CASE("property_access_on_selector_and_variable") {
    CHECK(Debug("@next.hand_size") == "(prop (select next) hand_size)");
    CHECK(Debug("$played.color") == "(prop $played color)");
}

TEST_CASE("function_call_arguments") {
    CHECK(Debug("if(a, 1, 2)") == "(call if a 1 2)");
    CHECK(Debug("count(@all, hand_size > 2)") ==
          "(call count (select all) (> hand_size 2))");
}

TEST_CASE("unary_negation_and_literals") {
    CHECK(Debug("-x") == "(neg x)");
    CHECK(Debug("true") == "true");
    CHECK(Debug("'uni:draw2'") == "'uni:draw2'");
}

TEST_CASE("missing_operand_reports_operator_position") {
    const auto result = match::expr::ParseExpression("1 +");
    REQUIRE(result.error.has_value());
    CHECK(result.error->position == 3);
    CHECK(result.error->message == "expected an expression after '+'");
}

TEST_CASE("unclosed_parenthesis") {
    const auto result = match::expr::ParseExpression("(1 + 2");
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "missing ')'");
}

TEST_CASE("count_nodes_counts_every_node") {
    const auto result = match::expr::ParseExpression("1 + 2 * 3");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.root != nullptr);
    CHECK(match::expr::CountNodes(*result.root) == 5);
}

TEST_CASE("deep_nesting_is_rejected_without_crashing") {
    const auto result = match::expr::ParseExpression(std::string(10000, '('));
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "expression is nested deeper than 64 levels");
}

}  // TEST_SUITE
