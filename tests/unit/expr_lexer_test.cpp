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

}  // TEST_SUITE
