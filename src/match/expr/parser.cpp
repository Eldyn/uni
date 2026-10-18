#include <match/expr/parser.hpp>

#include <charconv>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace match::expr {

std::string DebugString(const Node& node) {
    switch (node.kind) {
        case NodeKind::kLiteral:
            if (std::holds_alternative<std::string>(node.literal)) {
                return "'" + std::get<std::string>(node.literal) + "'";
            }
            return node.text;
        case NodeKind::kSelector:
            if (node.children.empty()) {
                return "(select " + node.text + ")";
            }
            return "(select " + node.text + " " + DebugString(*node.children[0]) + ")";
        case NodeKind::kVariable:
            return "$" + node.text;
        case NodeKind::kBareName:
            return node.text;
        case NodeKind::kProperty:
            return "(prop " + DebugString(*node.children[0]) + " " + node.text + ")";
        case NodeKind::kCall: {
            std::string out = "(call " + node.text;
            for (const NodePtr& child : node.children) {
                out += " " + DebugString(*child);
            }
            return out + ")";
        }
        case NodeKind::kUnary: {
            const std::string symbol = node.text == "-" ? "neg" : node.text;
            return "(" + symbol + " " + DebugString(*node.children[0]) + ")";
        }
        case NodeKind::kBinary:
            return "(" + node.text + " " + DebugString(*node.children[0]) + " " +
                   DebugString(*node.children[1]) + ")";
    }
    return {};
}

size_t CountNodes(const Node& node) {
    size_t count = 1;
    for (const NodePtr& child : node.children) {
        count += CountNodes(*child);
    }
    return count;
}

namespace {

constexpr size_t kMaximumDepth = 64;
constexpr int kUnaryBindingPower = 7;

int BinaryBindingPower(std::string_view symbol) {
    if (symbol == "||") {
        return 1;
    }
    if (symbol == "&&") {
        return 2;
    }
    if (symbol == "==" || symbol == "!=") {
        return 3;
    }
    if (symbol == "<" || symbol == "<=" || symbol == ">" || symbol == ">=") {
        return 4;
    }
    if (symbol == "+" || symbol == "-") {
        return 5;
    }
    if (symbol == "*" || symbol == "/" || symbol == "%") {
        return 6;
    }
    return 0;
}

bool StartsExpression(const Token& token) {
    switch (token.kind) {
        case TokenKind::kNumber:
        case TokenKind::kString:
        case TokenKind::kIdentifier:
        case TokenKind::kSelector:
        case TokenKind::kVariable:
        case TokenKind::kLeftParen:
            return true;
        case TokenKind::kOperator:
            return token.text == "-" || token.text == "!";
        default:
            return false;
    }
}

// INFO: a Pratt parser. `depth_` bounds recursion so pathological input such as
// ten thousand open parentheses fails instead of overflowing the stack.
class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    NodePtr Parse() {
        NodePtr node = ParseBinary(0);
        if (error_) {
            return nullptr;
        }
        if (Peek().kind != TokenKind::kEnd) {
            SetError(Peek().position, "unexpected token");
            return nullptr;
        }
        return node;
    }

    const std::optional<ParseError>& error() const { return error_; }

private:
    void SetError(size_t position, std::string message) {
        if (!error_) {
            error_ = ParseError{position, std::move(message)};
        }
    }

    const Token& Peek() const { return tokens_[index_]; }

    NodePtr MakeSimpleNode(NodeKind kind, const Token& token) {
        auto node = std::make_unique<Node>();
        node->kind = kind;
        node->text = token.text;
        node->position = token.position;
        return node;
    }

    NodePtr ParseBinary(int minimum_binding_power) {
        if (++depth_ > kMaximumDepth) {
            SetError(Peek().position, "expression is nested deeper than 64 levels");
            return nullptr;
        }
        NodePtr left = ParsePrefix();
        if (!left) {
            return nullptr;
        }
        while (!error_ && Peek().kind == TokenKind::kOperator) {
            const std::string symbol = Peek().text;
            const int binding_power = BinaryBindingPower(symbol);
            if (binding_power == 0 || binding_power <= minimum_binding_power) {
                break;
            }
            ++index_;
            if (!StartsExpression(Peek())) {
                SetError(Peek().position, "expected an expression after '" + symbol + "'");
                return nullptr;
            }
            NodePtr right = ParseBinary(binding_power);
            if (!right) {
                return nullptr;
            }
            auto node = std::make_unique<Node>();
            node->kind = NodeKind::kBinary;
            node->text = symbol;
            node->position = left->position;
            node->children.push_back(std::move(left));
            node->children.push_back(std::move(right));
            left = std::move(node);
        }
        --depth_;
        return left;
    }

    NodePtr ParsePrefix() {
        if (Peek().kind == TokenKind::kOperator &&
            (Peek().text == "-" || Peek().text == "!")) {
            const std::string symbol = Peek().text;
            const size_t position = Peek().position;
            ++index_;
            if (!StartsExpression(Peek())) {
                SetError(Peek().position, "expected an expression after '" + symbol + "'");
                return nullptr;
            }
            NodePtr operand = ParseBinary(kUnaryBindingPower);
            if (!operand) {
                return nullptr;
            }
            auto node = std::make_unique<Node>();
            node->kind = NodeKind::kUnary;
            node->text = symbol;
            node->position = position;
            node->children.push_back(std::move(operand));
            return node;
        }
        NodePtr atom = ParseAtom();
        if (!atom) {
            return nullptr;
        }
        return ParsePostfix(std::move(atom));
    }

    NodePtr ParsePostfix(NodePtr base) {
        while (!error_ && Peek().kind == TokenKind::kDot) {
            ++index_;
            if (Peek().kind != TokenKind::kIdentifier) {
                SetError(Peek().position, "expected a property name after '.'");
                return nullptr;
            }
            const std::string key = Peek().text;
            ++index_;
            auto node = std::make_unique<Node>();
            node->kind = NodeKind::kProperty;
            node->text = key;
            node->position = base->position;
            node->children.push_back(std::move(base));
            base = std::move(node);
        }
        return base;
    }

    NodePtr ParseAtom() {
        const Token token = Peek();
        switch (token.kind) {
            case TokenKind::kNumber:
                ++index_;
                return MakeNumberLiteral(token);
            case TokenKind::kString:
                ++index_;
                return MakeStringLiteral(token);
            case TokenKind::kIdentifier:
                return ParseIdentifier();
            case TokenKind::kSelector:
                return ParseSelector();
            case TokenKind::kVariable:
                ++index_;
                return MakeSimpleNode(NodeKind::kVariable, token);
            case TokenKind::kLeftParen:
                return ParseGroup();
            default:
                break;
        }
        SetError(token.position, "expected an expression");
        return nullptr;
    }

    NodePtr MakeNumberLiteral(const Token& token) {
        auto node = MakeSimpleNode(NodeKind::kLiteral, token);
        if (token.text.find('.') != std::string::npos) {
            double value = 0.0;
            std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
            node->literal = value;
        } else {
            int64_t value = 0;
            std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
            node->literal = value;
        }
        return node;
    }

    NodePtr MakeStringLiteral(const Token& token) {
        auto node = MakeSimpleNode(NodeKind::kLiteral, token);
        node->literal = token.text;
        return node;
    }

    NodePtr ParseIdentifier() {
        const Token token = Peek();
        ++index_;
        if (token.text == "true" || token.text == "false") {
            auto node = MakeSimpleNode(NodeKind::kLiteral, token);
            node->literal = token.text == "true";
            return node;
        }
        if (Peek().kind == TokenKind::kLeftParen) {
            return ParseCall(token);
        }
        return MakeSimpleNode(NodeKind::kBareName, token);
    }

    NodePtr ParseCall(const Token& name_token) {
        ++index_;
        auto node = MakeSimpleNode(NodeKind::kCall, name_token);
        if (Peek().kind == TokenKind::kRightParen) {
            ++index_;
            return node;
        }
        while (true) {
            NodePtr argument = ParseBinary(0);
            if (!argument) {
                return nullptr;
            }
            node->children.push_back(std::move(argument));
            if (Peek().kind == TokenKind::kComma) {
                ++index_;
                continue;
            }
            if (Peek().kind == TokenKind::kRightParen) {
                ++index_;
                break;
            }
            SetError(Peek().position, "expected ',' or ')'");
            return nullptr;
        }
        return node;
    }

    NodePtr ParseSelector() {
        const Token token = Peek();
        ++index_;
        auto node = MakeSimpleNode(NodeKind::kSelector, token);
        if (Peek().kind == TokenKind::kLeftBracket) {
            ++index_;
            NodePtr filter = ParseBinary(0);
            if (!filter) {
                return nullptr;
            }
            if (Peek().kind != TokenKind::kRightBracket) {
                SetError(Peek().position, "missing ']'");
                return nullptr;
            }
            ++index_;
            node->children.push_back(std::move(filter));
        }
        return node;
    }

    NodePtr ParseGroup() {
        ++index_;
        NodePtr inner = ParseBinary(0);
        if (!inner) {
            return nullptr;
        }
        if (Peek().kind != TokenKind::kRightParen) {
            SetError(Peek().position, "missing ')'");
            return nullptr;
        }
        ++index_;
        return inner;
    }

    const std::vector<Token>& tokens_;
    size_t index_ = 0;
    size_t depth_ = 0;
    std::optional<ParseError> error_;
};

}  // namespace

ParseResult ParseExpression(std::string_view source) {
    ParseResult result;
    LexResult lexed = Lex(source);
    if (lexed.error) {
        result.error = std::move(lexed.error);
        return result;
    }
    Parser parser(lexed.tokens);
    result.root = parser.Parse();
    if (parser.error()) {
        result.error = parser.error();
        result.root = nullptr;
    }
    return result;
}

}  // namespace match::expr
