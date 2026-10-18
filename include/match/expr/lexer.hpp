#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace match::expr {

enum class TokenKind {
    kNumber,
    kString,
    kIdentifier,
    kSelector,
    kVariable,
    kOperator,
    kLeftParen,
    kRightParen,
    kLeftBracket,
    kRightBracket,
    kComma,
    kDot,
    kEnd,
};

struct Token {
    TokenKind kind;
    std::string text;
    size_t position;
};

struct ParseError {
    size_t position;
    std::string message;
};

struct LexResult {
    std::vector<Token> tokens;
    std::optional<ParseError> error;
};

LexResult Lex(std::string_view source);

}  // namespace match::expr
