#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace match::expr {

// INFO: the node budget of one expression, shared by the parser, the checker,
// the condition parser and the evaluator so a tree that is accepted anywhere
// holds at most this many nodes.
inline constexpr size_t kMaximumExpressionNodes = 256;

enum class NodeKind {
    kLiteral,
    kSelector,
    kVariable,
    kProperty,
    kCall,
    kUnary,
    kBinary,
    kBareName,
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    NodeKind kind;
    std::string text;
    std::variant<bool, int64_t, double, std::string> literal;
    std::vector<NodePtr> children;
    size_t position;
    // INFO: set by the checker when the checked type is float but the value may
    // be an int, so the evaluator widens an int result to a double.
    mutable bool widen_to_float = false;
};

std::string DebugString(const Node& node);

size_t CountNodes(const Node& node);

}  // namespace match::expr
