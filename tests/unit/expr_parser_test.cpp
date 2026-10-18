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

std::string RepeatedAddition(size_t repetitions) {
    std::string source = "1";
    for (size_t index = 0; index < repetitions; ++index) {
        source += " + 1";
    }
    return source;
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

TEST_CASE("binary_operators_are_left_associative") {
    CHECK(Debug("1 - 2 - 3") == "(- (- 1 2) 3)");
    CHECK(Debug("-a * b") == "(* (neg a) b)");
    CHECK(Debug("a == b == c") == "(== (== a b) c)");
}

TEST_CASE("trailing_tokens_are_rejected_at_their_position") {
    const auto first = match::expr::ParseExpression("1 2");
    REQUIRE(first.error.has_value());
    CHECK(first.error->position == 2);
    CHECK(first.error->message.starts_with("unexpected token"));

    const auto second = match::expr::ParseExpression("a b");
    REQUIRE(second.error.has_value());
    CHECK(second.error->position == 2);
    CHECK(second.error->message.starts_with("unexpected token"));

    const auto third = match::expr::ParseExpression("1)");
    REQUIRE(third.error.has_value());
    CHECK(third.error->position == 1);
    CHECK(third.error->message.starts_with("unexpected token"));
}

TEST_CASE("empty_call_and_missing_argument") {
    CHECK(Debug("f()") == "(call f)");
    const auto result = match::expr::ParseExpression("f(1,)");
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "expected an expression");
}

TEST_CASE("unclosed_selector_filter") {
    const auto missing_filter = match::expr::ParseExpression("@all[");
    REQUIRE(missing_filter.error.has_value());
    CHECK(missing_filter.error->message == "expected an expression");

    const auto missing_bracket = match::expr::ParseExpression("@all[x > 3");
    REQUIRE(missing_bracket.error.has_value());
    CHECK(missing_bracket.error->message == "missing ']'");
}

TEST_CASE("node_count_is_capped_at_the_parse_node_limit") {
    const auto allowed = match::expr::ParseExpression(RepeatedAddition(300));
    REQUIRE_FALSE(allowed.error.has_value());
    REQUIRE(allowed.root != nullptr);
    CHECK(match::expr::CountNodes(*allowed.root) == 601);

    const auto rejected = match::expr::ParseExpression(RepeatedAddition(5000));
    REQUIRE(rejected.error.has_value());
    CHECK(rejected.error->message ==
          "expression is too large to parse (more than 4096 nodes)");
}

TEST_CASE("count_nodes_includes_filters_arguments_and_properties") {
    const auto result = match::expr::ParseExpression("count(@all[hand_size > 3])");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.root != nullptr);
    // INFO: call + select + comparison + bare name (hand_size) + literal (3).
    CHECK(match::expr::CountNodes(*result.root) == 5);

    const auto property = match::expr::ParseExpression("$played.color");
    REQUIRE_FALSE(property.error.has_value());
    REQUIRE(property.root != nullptr);
    CHECK(match::expr::CountNodes(*property.root) == 2);
}

TEST_CASE("out_of_range_number_is_rejected") {
    const auto result = match::expr::ParseExpression("99999999999999999999");
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "number out of range");
}

}  // TEST_SUITE

TEST_SUITE("expr assignment") {

TEST_CASE("set_namespaced_property_to_boolean") {
    const auto result = match::expr::ParseAssignment("@next.axe:stunned = true");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.assignment.has_value());
    CHECK(match::expr::DebugString(*result.assignment->target) ==
          "(prop (select next) axe:stunned)");
    CHECK(result.assignment->op == match::expr::AssignOp::kSet);
    CHECK(match::expr::DebugString(*result.assignment->value) == "true");
}

TEST_CASE("add_to_property") {
    const auto result = match::expr::ParseAssignment("@match.tree:growth += 1");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.assignment.has_value());
    CHECK(match::expr::DebugString(*result.assignment->target) ==
          "(prop (select match) tree:growth)");
    CHECK(result.assignment->op == match::expr::AssignOp::kAdd);
    CHECK(match::expr::DebugString(*result.assignment->value) == "1");
}

TEST_CASE("variable_target_is_rejected") {
    const auto result = match::expr::ParseAssignment("$x = 1");
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "cannot assign to a variable; variables are immutable");
}

TEST_CASE("non_property_target_is_rejected") {
    const auto result = match::expr::ParseAssignment("1 = 2");
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "an assignment target must be a property");
}

TEST_CASE("missing_assignment_operator_is_rejected") {
    const auto result = match::expr::ParseAssignment("@next.hand_size");
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "expected an assignment operator (=, +=, -=, *=)");
}

}  // TEST_SUITE
