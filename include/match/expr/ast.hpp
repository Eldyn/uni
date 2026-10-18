#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace match::expr {

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
};

std::string DebugString(const Node& node);

size_t CountNodes(const Node& node);

}  // namespace match::expr
