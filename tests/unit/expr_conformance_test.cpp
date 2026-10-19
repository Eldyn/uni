#include <doctest/doctest.h>

#include <match/expr/checker.hpp>
#include <match/expr/evaluator.hpp>
#include <match/expr/parser.hpp>
#include <match/expr/schema_json.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "expr_test_support.hpp"

namespace fs = std::filesystem;
using match::expr::CheckAssignment;
using match::expr::CheckExpression;
using match::expr::CheckResult;
using match::expr::Diagnostic;
using match::expr::EvalError;
using match::expr::EvalResult;
using match::expr::Evaluate;
using match::expr::LoadSchema;
using match::expr::ParseAssignment;
using match::expr::ParseExpression;
using match::expr::ParseResult;
using match::expr::Schema;
using match::expr::Type;
using match::expr::Value;
using match::expr::testing::JsonWorld;
using nlohmann::json;

namespace {

// INFO: the file label and pointer a corpus diagnostic is formatted with; the
// verifier's CLI uses the same pair.
constexpr std::string_view kFile = "<expression>";
constexpr std::string_view kPointer = "/";

// INFO: walks up from this source file to the repository root, the same way the
// golden tests find their fixtures.
fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        const fs::path parent = path.parent_path();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return {};
}

json ReadJsonFile(const fs::path& path) {
    std::ifstream stream(path);
    REQUIRE(stream.good());
    json value;
    stream >> value;
    return value;
}

std::optional<Type> TypeFromName(const std::string& name) {
    if (name == "bool") return Type::kBool;
    if (name == "int") return Type::kInt;
    if (name == "float") return Type::kFloat;
    if (name == "string") return Type::kString;
    if (name == "member_set") return Type::kMemberSet;
    if (name == "seat") return Type::kSeat;
    if (name == "seat_set") return Type::kSeatSet;
    if (name == "card") return Type::kCard;
    if (name == "card_set") return Type::kCardSet;
    return std::nullopt;
}

std::string FirstMessage(const std::vector<Diagnostic>& diagnostics) {
    return diagnostics.front().message;
}

// INFO: runs one corpus case and returns an empty string on success, or a
// description of the mismatch. A `check_error` case checks the expression (or
// assignment); an `error` case runs it and matches the EvalError code; a
// `value` case checks it, runs it and compares the result.
std::string RunCase(const Schema& schema, const json& item) {
    if (!item.is_object()) {
        return "the case must be an object";
    }
    if (!item.contains("name") || !item.contains("expression")) {
        return "the case needs a name and an expression";
    }
    const std::string source = item.at("expression").get<std::string>();

    std::optional<Type> expected;
    if (const auto declared = item.find("expected_type"); declared != item.end()) {
        if (!declared->is_string()) {
            return "expected_type must be a string";
        }
        expected = TypeFromName(declared->get<std::string>());
        if (!expected.has_value()) {
            return "unknown expected_type \"" + declared->get<std::string>() + "\"";
        }
    }

    JsonWorld world(schema, item.value("world", json::object()));
    if (!world.error().empty()) {
        return "world error: " + world.error();
    }
    const json expect = item.value("expect", json::object());
    const bool assignment = item.value("assignment", false);

    if (const auto check_error = expect.find("check_error"); check_error != expect.end()) {
        if (!check_error->is_string()) {
            return "check_error must be a string";
        }
        const std::string wanted = check_error->get<std::string>();
        std::string message;
        if (assignment) {
            const std::vector<Diagnostic> diagnostics =
                CheckAssignment(source, schema, kFile, kPointer);
            if (diagnostics.empty()) {
                return "expected a check error, got none";
            }
            message = FirstMessage(diagnostics);
        } else {
            const CheckResult result = CheckExpression(source, schema, expected, kFile, kPointer);
            if (result.diagnostics.empty()) {
                return "expected a check error, got none";
            }
            message = FirstMessage(result.diagnostics);
        }
        if (message.find(wanted) == std::string::npos) {
            return "the error \"" + message + "\" does not contain \"" + wanted + "\"";
        }
        return {};
    }

    if (const auto error = expect.find("error"); error != expect.end()) {
        if (!error->is_string()) {
            return "error must be a string";
        }
        const std::string wanted = error->get<std::string>();
        std::optional<EvalError> failure;
        if (assignment) {
            const match::expr::AssignmentResult parsed = ParseAssignment(source);
            if (parsed.error.has_value()) {
                return "parse error: " + parsed.error->message;
            }
            failure = match::expr::ApplyAssignment(*parsed.assignment, world);
        } else {
            const ParseResult parsed = ParseExpression(source);
            if (parsed.error.has_value()) {
                return "parse error: " + parsed.error->message;
            }
            const EvalResult result = Evaluate(*parsed.root, world);
            if (const EvalError* eval_error = std::get_if<EvalError>(&result)) {
                failure = *eval_error;
            }
        }
        if (!failure.has_value()) {
            return "expected the runtime error " + wanted + ", got a value";
        }
        if (failure->code != wanted) {
            return "expected the runtime error " + wanted + ", got " + failure->code;
        }
        return {};
    }

    if (const auto value = expect.find("value"); value != expect.end()) {
        if (assignment) {
            const match::expr::AssignmentResult parsed = ParseAssignment(source);
            if (parsed.error.has_value()) {
                return "parse error: " + parsed.error->message;
            }
            const std::optional<EvalError> failure =
                match::expr::ApplyAssignment(*parsed.assignment, world);
            if (failure.has_value()) {
                return "unexpected runtime error " + failure->code;
            }
            const EvalResult read = Evaluate(*parsed.assignment->target, world);
            if (const EvalError* eval_error = std::get_if<EvalError>(&read)) {
                return "reading the assigned value failed with " + eval_error->code;
            }
            Value wanted;
            std::string error;
            if (!world.DecodeNatural(*value, wanted, error)) {
                return "expected value: " + error;
            }
            if (!(std::get<Value>(read) == wanted)) {
                return "the assigned value does not match the expected value";
            }
            return {};
        }
        const CheckResult checked = CheckExpression(source, schema, expected, kFile, kPointer);
        if (!checked.diagnostics.empty()) {
            return "check error: " + FirstMessage(checked.diagnostics);
        }
        if (!checked.type.has_value()) {
            return "the checker returned no type";
        }
        const ParseResult parsed = ParseExpression(source);
        if (parsed.error.has_value()) {
            return "parse error: " + parsed.error->message;
        }
        const EvalResult result = Evaluate(*parsed.root, world);
        if (const EvalError* eval_error = std::get_if<EvalError>(&result)) {
            return "unexpected runtime error " + eval_error->code;
        }
        Value wanted;
        std::string error;
        if (!world.Decode(*checked.type, *value, wanted, error)) {
            return "expected value: " + error;
        }
        if (!(std::get<Value>(result) == wanted)) {
            return "the value does not match the expected value";
        }
        return {};
    }

    return "the case has no expectation";
}

}  // namespace

TEST_SUITE("expr conformance") {

TEST_CASE("the shared corpus passes in the C++ evaluator") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    const json schema_value = ReadJsonFile(root / "tests" / "conformance" / "schema.json");
    std::string error;
    const std::optional<Schema> schema = LoadSchema(schema_value, error);
    REQUIRE_MESSAGE(schema.has_value(), error);

    const json corpus = ReadJsonFile(root / "tests" / "conformance" / "expressions.json");
    const json& cases = corpus.at("cases");
    REQUIRE(cases.is_array());

    for (const json& item : cases) {
        const std::string name = item.value("name", std::string("<unnamed>"));
        const std::string failure = RunCase(*schema, item);
        CHECK_MESSAGE(failure.empty(), name, ": ", failure);
    }
}

}  // TEST_SUITE
