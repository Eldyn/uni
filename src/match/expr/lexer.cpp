#include <match/expr/lexer.hpp>

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace match::expr {
namespace {

bool IsDigit(char character) {
    return character >= '0' && character <= '9';
}

bool IsIdentifierStart(char character) {
    return (character >= 'a' && character <= 'z') || character == '_';
}

bool IsIdentifierChar(char character) {
    return IsIdentifierStart(character) || IsDigit(character);
}

bool IsNonAscii(char character) {
    return static_cast<unsigned char>(character) >= 0x80;
}

bool IsUppercase(char character) {
    return character >= 'A' && character <= 'Z';
}

bool IsWhitespace(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

std::string UnexpectedCharacterMessage(char character) {
    return std::string("unexpected character '") + character + "'";
}

constexpr std::string_view kSnakeCaseMessage =
    "unexpected character; identifiers use lowercase ASCII snake_case";

constexpr std::string_view kColonMessage = "expected an identifier after ':'";

constexpr std::string_view kTernaryMessage =
    "unexpected character '?'; there is no ternary operator, use if(condition, a, b)";

// INFO: reads `[a-z_][a-z0-9_]*`, optionally `:` plus a second identifier with
// no spaces; on `:` a second identifier must follow directly, otherwise the
// error points at the colon. `position` stops just past the identifier on
// success.
std::optional<ParseError> LexIdentifier(std::string_view source, size_t& position,
                                        std::string& text) {
    text.clear();
    text.push_back(source[position]);
    ++position;
    while (position < source.size() && IsIdentifierChar(source[position])) {
        text.push_back(source[position]);
        ++position;
    }
    if (position >= source.size() || source[position] != ':') {
        return std::nullopt;
    }
    const size_t colon_position = position;
    ++position;
    text.push_back(':');
    if (position >= source.size() || !IsIdentifierStart(source[position])) {
        position = colon_position;
        return ParseError{colon_position, std::string(kColonMessage)};
    }
    while (position < source.size() && IsIdentifierChar(source[position])) {
        text.push_back(source[position]);
        ++position;
    }
    if (position < source.size() &&
        (IsUppercase(source[position]) || IsNonAscii(source[position]))) {
        return ParseError{position, std::string(kSnakeCaseMessage)};
    }
    return std::nullopt;
}

// INFO: digits with an optional fraction; integers must fit int64_t and floats
// must be finite, otherwise `number out of range`.
std::optional<ParseError> LexNumber(std::string_view source, size_t& position,
                                    std::string& text) {
    const size_t start = position;
    while (position < source.size() && IsDigit(source[position])) {
        ++position;
    }
    const bool has_fraction = position + 1 < source.size() && source[position] == '.' &&
                              IsDigit(source[position + 1]);
    if (has_fraction) {
        ++position;
        while (position < source.size() && IsDigit(source[position])) {
            ++position;
        }
    }
    text.assign(source.substr(start, position - start));
    if (has_fraction) {
        double value = 0.0;
        const auto result =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc() || !std::isfinite(value)) {
            position = start;
            return ParseError{start, "number out of range"};
        }
    } else {
        int64_t value = 0;
        const auto result =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc()) {
            position = start;
            return ParseError{start, "number out of range"};
        }
    }
    return std::nullopt;
}

// INFO: single-quoted string; the text excludes the quotes.
std::optional<ParseError> LexString(std::string_view source, size_t& position,
                                    std::string& text) {
    const size_t start = position;
    ++position;
    text.clear();
    while (position < source.size()) {
        const char character = source[position];
        ++position;
        if (character == '\'') {
            return std::nullopt;
        }
        text.push_back(character);
    }
    position = start;
    return ParseError{start, "unterminated string"};
}

// INFO: identifier after `@` or `$`, pushed as a kSelector or kVariable token.
std::optional<ParseError> LexAfterPrefix(std::string_view source, size_t& position,
                                         TokenKind kind, std::vector<Token>& tokens) {
    const size_t start = position;
    ++position;
    if (position < source.size() && IsUppercase(source[position])) {
        return ParseError{position, std::string(kSnakeCaseMessage)};
    }
    if (position < source.size() && IsNonAscii(source[position])) {
        return ParseError{position, std::string(kSnakeCaseMessage)};
    }
    if (position >= source.size() || !IsIdentifierStart(source[position])) {
        return ParseError{start, UnexpectedCharacterMessage(source[start])};
    }
    std::string text;
    if (const std::optional<ParseError> error = LexIdentifier(source, position, text)) {
        return error;
    }
    tokens.push_back(Token{kind, std::move(text), start});
    return std::nullopt;
}

bool MatchesAny(std::string_view text, std::initializer_list<std::string_view> words) {
    for (const std::string_view word : words) {
        if (text == word) {
            return true;
        }
    }
    return false;
}

// INFO: the single-character tokens that need no lookahead; one table so a new
// punctuation mark is one entry rather than one more branch in the loop.
struct SingleCharacterToken {
    char character;
    TokenKind kind;
};

constexpr SingleCharacterToken kSingleCharacterTokens[] = {
    {'(', TokenKind::kLeftParen},   {')', TokenKind::kRightParen},
    {'[', TokenKind::kLeftBracket}, {']', TokenKind::kRightBracket},
    {',', TokenKind::kComma},       {'.', TokenKind::kDot},
};

std::optional<TokenKind> SingleCharacterTokenKind(char character) {
    for (const SingleCharacterToken& entry : kSingleCharacterTokens) {
        if (entry.character == character) {
            return entry.kind;
        }
    }
    return std::nullopt;
}

}  // namespace

LexResult Lex(std::string_view source) {
    LexResult result;
    size_t position = 0;
    while (position < source.size()) {
        const char character = source[position];
        if (IsWhitespace(character)) {
            ++position;
            continue;
        }

        const size_t start = position;
        std::string text;
        std::optional<ParseError> error;

        if (const std::optional<TokenKind> kind = SingleCharacterTokenKind(character)) {
            result.tokens.push_back(Token{*kind, std::string(1, character), start});
            ++position;
            continue;
        }
        if (character == '@' || character == '$') {
            error = LexAfterPrefix(source, position,
                                   character == '@' ? TokenKind::kSelector
                                                    : TokenKind::kVariable,
                                   result.tokens);
        } else if (character == '\'') {
            error = LexString(source, position, text);
            if (!error) {
                result.tokens.push_back(Token{TokenKind::kString, std::move(text), start});
            }
        } else if (IsDigit(character)) {
            error = LexNumber(source, position, text);
            if (!error) {
                result.tokens.push_back(Token{TokenKind::kNumber, std::move(text), start});
            }
        } else if (IsIdentifierStart(character)) {
            error = LexIdentifier(source, position, text);
            if (!error) {
                result.tokens.push_back(
                    Token{TokenKind::kIdentifier, std::move(text), start});
            }
        } else if (character == '?') {
            error = ParseError{start, std::string(kTernaryMessage)};
        } else if (IsUppercase(character) || IsNonAscii(character)) {
            error = ParseError{start, std::string(kSnakeCaseMessage)};
        } else if (character == ':') {
            error = ParseError{start, UnexpectedCharacterMessage(character)};
        } else {
            const std::string_view pair = source.substr(start, 2);
            if (position + 1 < source.size() &&
                MatchesAny(pair, {"==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*="})) {
                result.tokens.push_back(Token{TokenKind::kOperator, std::string(pair), start});
                position += 2;
                continue;
            }
            if (std::string_view("+-*/%<>!=").find(character) != std::string_view::npos) {
                result.tokens.push_back(
                    Token{TokenKind::kOperator, std::string(1, character), start});
                ++position;
                continue;
            }
            error = ParseError{start, UnexpectedCharacterMessage(character)};
        }

        if (error) {
            result.error = std::move(error);
            return result;
        }
    }

    if (result.tokens.empty()) {
        result.error = ParseError{position, "empty expression"};
    } else {
        result.tokens.push_back(Token{TokenKind::kEnd, "", position});
    }
    return result;
}

}  // namespace match::expr
