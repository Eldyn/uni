#pragma once

#include <optional>
#include <string_view>

#include <match/expr/ast.hpp>
#include <match/expr/lexer.hpp>

namespace match::expr {

struct ParseResult {
    NodePtr root;
    std::optional<ParseError> error;
};

ParseResult ParseExpression(std::string_view source);

enum class AssignOp { kSet, kAdd, kSubtract, kMultiply };

struct Assignment {
    NodePtr target;
    AssignOp op;
    NodePtr value;
};

struct AssignmentResult {
    std::optional<Assignment> assignment;
    std::optional<ParseError> error;
};

AssignmentResult ParseAssignment(std::string_view source);

}  // namespace match::expr
