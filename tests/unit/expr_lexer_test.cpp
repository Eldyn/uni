#include <doctest/doctest.h>

#include <match/expr/lexer.hpp>

TEST_SUITE("expr lexer") {

TEST_CASE("selector_with_namespaced_property") {
    const auto result = match::expr::Lex("@next.axe:stunned");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.tokens.size() == 4);
    CHECK(result.tokens[0].kind == match::expr::TokenKind::kSelector);
    CHECK(result.tokens[0].text == "next");
    CHECK(result.tokens[0].position == 0);
    CHECK(result.tokens[1].kind == match::expr::TokenKind::kDot);
    CHECK(result.tokens[1].position == 5);
    CHECK(result.tokens[2].kind == match::expr::TokenKind::kIdentifier);
    CHECK(result.tokens[2].text == "axe:stunned");
    CHECK(result.tokens[2].position == 6);
    CHECK(result.tokens[3].kind == match::expr::TokenKind::kEnd);
}

TEST_CASE("variable_property_comparison") {
    const auto result = match::expr::Lex("$responder.hand_size > 5");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.tokens.size() == 6);
    CHECK(result.tokens[0].kind == match::expr::TokenKind::kVariable);
    CHECK(result.tokens[0].text == "responder");
    CHECK(result.tokens[0].position == 0);
    CHECK(result.tokens[1].kind == match::expr::TokenKind::kDot);
    CHECK(result.tokens[2].kind == match::expr::TokenKind::kIdentifier);
    CHECK(result.tokens[2].text == "hand_size");
    CHECK(result.tokens[3].kind == match::expr::TokenKind::kOperator);
    CHECK(result.tokens[3].text == ">");
    CHECK(result.tokens[4].kind == match::expr::TokenKind::kNumber);
    CHECK(result.tokens[4].text == "5");
    CHECK(result.tokens[5].kind == match::expr::TokenKind::kEnd);
}

TEST_CASE("string_literal") {
    const auto result = match::expr::Lex("'uni:draw2'");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.tokens.size() == 2);
    CHECK(result.tokens[0].kind == match::expr::TokenKind::kString);
    CHECK(result.tokens[0].text == "uni:draw2");
    CHECK(result.tokens[0].position == 0);
    CHECK(result.tokens[1].kind == match::expr::TokenKind::kEnd);
}

TEST_CASE("float_literal") {
    const auto result = match::expr::Lex("1.5");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.tokens.size() == 2);
    CHECK(result.tokens[0].kind == match::expr::TokenKind::kNumber);
    CHECK(result.tokens[0].text == "1.5");
    CHECK(result.tokens[1].kind == match::expr::TokenKind::kEnd);
}

TEST_CASE("plain_selector") {
    const auto result = match::expr::Lex("@rand_all");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.tokens.size() == 2);
    CHECK(result.tokens[0].kind == match::expr::TokenKind::kSelector);
    CHECK(result.tokens[0].text == "rand_all");
    CHECK(result.tokens[1].kind == match::expr::TokenKind::kEnd);
}

TEST_CASE("ternary_character_is_an_error") {
    const auto result = match::expr::Lex("a ? b");
    REQUIRE(result.error.has_value());
    CHECK(result.error->position == 2);
    CHECK(result.error->message ==
          "unexpected character '?'; there is no ternary operator, use if(condition, a, b)");
}

TEST_CASE("empty_source_is_an_error") {
    const auto result = match::expr::Lex("");
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "empty expression");
}

TEST_CASE("a_bare_prefix_reports_the_start_of_the_token") {
    const auto at_result = match::expr::Lex("@");
    REQUIRE(at_result.error.has_value());
    CHECK(at_result.error->position == 0);
    CHECK(at_result.error->message == "unexpected character '@'");

    const auto dollar_result = match::expr::Lex("1 + $");
    REQUIRE(dollar_result.error.has_value());
    CHECK(dollar_result.error->position == 4);
    CHECK(dollar_result.error->message == "unexpected character '$'");
}

TEST_CASE("non_ascii_identifier_is_an_error") {
    const auto result = match::expr::Lex("hand_síze");
    REQUIRE(result.error.has_value());
    CHECK(result.error->position == 6);
    CHECK(result.error->message ==
          "unexpected character; identifiers use lowercase ASCII snake_case");
}

TEST_CASE("uppercase_identifier_is_an_error") {
    const auto result = match::expr::Lex("HandSize");
    REQUIRE(result.error.has_value());
    CHECK(result.error->position == 0);
    CHECK(result.error->message ==
          "unexpected character; identifiers use lowercase ASCII snake_case");
}

TEST_CASE("number_out_of_range_is_an_error") {
    const auto result = match::expr::Lex("99999999999999999999");
    REQUIRE(result.error.has_value());
    CHECK(result.error->position == 0);
    CHECK(result.error->message == "number out of range");
}

TEST_CASE("multi_character_operators") {
    const auto result = match::expr::Lex("== != <= >= && || += -= *=");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.tokens.size() == 10);
    const std::vector<std::string> expected{
        "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*="};
    for (size_t index = 0; index < expected.size(); ++index) {
        CHECK(result.tokens[index].kind == match::expr::TokenKind::kOperator);
        CHECK(result.tokens[index].text == expected[index]);
    }
    CHECK(result.tokens.back().kind == match::expr::TokenKind::kEnd);
}

TEST_CASE("groups_and_single_character_operators") {
    const auto result = match::expr::Lex("(-a[1>%2]==b, ., =)");
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.tokens.size() == 17);
    const std::vector<match::expr::TokenKind> kinds{
        match::expr::TokenKind::kLeftParen,
        match::expr::TokenKind::kOperator,     // -
        match::expr::TokenKind::kIdentifier,   // a
        match::expr::TokenKind::kLeftBracket,
        match::expr::TokenKind::kNumber,       // 1
        match::expr::TokenKind::kOperator,     // >
        match::expr::TokenKind::kOperator,     // %
        match::expr::TokenKind::kNumber,       // 2
        match::expr::TokenKind::kRightBracket,
        match::expr::TokenKind::kOperator,     // ==
        match::expr::TokenKind::kIdentifier,   // b
        match::expr::TokenKind::kComma,
        match::expr::TokenKind::kDot,
        match::expr::TokenKind::kComma,
        match::expr::TokenKind::kOperator,     // =
        match::expr::TokenKind::kRightParen,
        match::expr::TokenKind::kEnd,
    };
    for (size_t index = 0; index < kinds.size(); ++index) {
        CHECK(result.tokens[index].kind == kinds[index]);
    }
}

TEST_CASE("trailing_colon_is_an_error") {
    const auto identifier_result = match::expr::Lex("a:");
    REQUIRE(identifier_result.error.has_value());
    CHECK(identifier_result.error->position == 1);
    CHECK(identifier_result.error->message == "expected an identifier after ':'");

    const auto selector_result = match::expr::Lex("@next:");
    REQUIRE(selector_result.error.has_value());
    CHECK(selector_result.error->position == 5);
    CHECK(selector_result.error->message == "expected an identifier after ':'");
}

TEST_CASE("colon_without_following_identifier_is_an_error") {
    const auto space_result = match::expr::Lex("a: b");
    REQUIRE(space_result.error.has_value());
    CHECK(space_result.error->position == 1);
    CHECK(space_result.error->message == "expected an identifier after ':'");

    const auto paren_result = match::expr::Lex("a:)");
    REQUIRE(paren_result.error.has_value());
    CHECK(paren_result.error->position == 1);
    CHECK(paren_result.error->message == "expected an identifier after ':'");
}

TEST_CASE("unterminated_string_is_an_error") {
    const auto result = match::expr::Lex("'abc");
    REQUIRE(result.error.has_value());
    CHECK(result.error->position == 0);
    CHECK(result.error->message == "unterminated string");
}

TEST_CASE("lone_ampersand_and_pipe_are_errors") {
    const auto ampersand_result = match::expr::Lex("&");
    REQUIRE(ampersand_result.error.has_value());
    CHECK(ampersand_result.error->position == 0);
    CHECK(ampersand_result.error->message == "unexpected character '&'");

    const auto pipe_result = match::expr::Lex("|");
    REQUIRE(pipe_result.error.has_value());
    CHECK(pipe_result.error->position == 0);
    CHECK(pipe_result.error->message == "unexpected character '|'");
}

TEST_CASE("integer_boundary_values") {
    const auto largest_result = match::expr::Lex("9223372036854775807");
    REQUIRE_FALSE(largest_result.error.has_value());
    REQUIRE(largest_result.tokens.size() == 2);
    CHECK(largest_result.tokens[0].kind == match::expr::TokenKind::kNumber);
    CHECK(largest_result.tokens[0].text == "9223372036854775807");
    CHECK(largest_result.tokens[0].position == 0);
    CHECK(largest_result.tokens[1].kind == match::expr::TokenKind::kEnd);

    const auto overflow_result = match::expr::Lex("9223372036854775808");
    REQUIRE(overflow_result.error.has_value());
    CHECK(overflow_result.error->position == 0);
    CHECK(overflow_result.error->message == "number out of range");
}

TEST_CASE("number_edge_shapes") {
    const auto two_fractions_result = match::expr::Lex("1.2.3");
    REQUIRE_FALSE(two_fractions_result.error.has_value());
    REQUIRE(two_fractions_result.tokens.size() == 4);
    CHECK(two_fractions_result.tokens[0].kind == match::expr::TokenKind::kNumber);
    CHECK(two_fractions_result.tokens[0].text == "1.2");
    CHECK(two_fractions_result.tokens[1].kind == match::expr::TokenKind::kDot);
    CHECK(two_fractions_result.tokens[2].kind == match::expr::TokenKind::kNumber);
    CHECK(two_fractions_result.tokens[2].text == "3");
    CHECK(two_fractions_result.tokens[3].kind == match::expr::TokenKind::kEnd);

    const auto exponent_result = match::expr::Lex("1e5");
    REQUIRE_FALSE(exponent_result.error.has_value());
    REQUIRE(exponent_result.tokens.size() == 3);
    CHECK(exponent_result.tokens[0].kind == match::expr::TokenKind::kNumber);
    CHECK(exponent_result.tokens[0].text == "1");
    CHECK(exponent_result.tokens[1].kind == match::expr::TokenKind::kIdentifier);
    CHECK(exponent_result.tokens[1].text == "e5");
    CHECK(exponent_result.tokens[2].kind == match::expr::TokenKind::kEnd);

    const auto leading_dot_result = match::expr::Lex(".5");
    REQUIRE_FALSE(leading_dot_result.error.has_value());
    REQUIRE(leading_dot_result.tokens.size() == 3);
    CHECK(leading_dot_result.tokens[0].kind == match::expr::TokenKind::kDot);
    CHECK(leading_dot_result.tokens[1].kind == match::expr::TokenKind::kNumber);
    CHECK(leading_dot_result.tokens[1].text == "5");
    CHECK(leading_dot_result.tokens[2].kind == match::expr::TokenKind::kEnd);

    const auto trailing_dot_result = match::expr::Lex("5.");
    REQUIRE_FALSE(trailing_dot_result.error.has_value());
    REQUIRE(trailing_dot_result.tokens.size() == 3);
    CHECK(trailing_dot_result.tokens[0].kind == match::expr::TokenKind::kNumber);
    CHECK(trailing_dot_result.tokens[0].text == "5");
    CHECK(trailing_dot_result.tokens[1].kind == match::expr::TokenKind::kDot);
    CHECK(trailing_dot_result.tokens[2].kind == match::expr::TokenKind::kEnd);
}

TEST_CASE("lone_single_character_operators") {
    const std::vector<std::string> sources{"<", "!", "+", "*", "/"};
    for (const std::string& source : sources) {
        const auto result = match::expr::Lex(source);
        REQUIRE_FALSE(result.error.has_value());
        REQUIRE(result.tokens.size() == 2);
        CHECK(result.tokens[0].kind == match::expr::TokenKind::kOperator);
        CHECK(result.tokens[0].text == source);
        CHECK(result.tokens[1].kind == match::expr::TokenKind::kEnd);
    }
}

}  // TEST_SUITE
