#include <doctest/doctest.h>

#include <match/expr/checker.hpp>
#include <match/expr/diagnostic.hpp>
#include <match/expr/evaluator.hpp>
#include <match/expr/parser.hpp>
#include <match/expr/schema_json.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "expr_test_support.hpp"

namespace fs = std::filesystem;
using match::expr::CheckExpression;
using match::expr::CheckParsedExpression;
using match::expr::CheckResult;
using match::expr::DuplicateKey;
using match::expr::EvalError;
using match::expr::EvalResult;
using match::expr::Evaluate;
using match::expr::FormatDiagnostic;
using match::expr::LoadSchema;
using match::expr::ParseExpression;
using match::expr::ParseResult;
using match::expr::ParseSchemaJson;
using match::expr::Schema;
using match::expr::SchemaScan;
using match::expr::Type;
using match::expr::TypeFromName;
using match::expr::Value;
using match::expr::testing::JsonWorld;
using match::expr::testing::MakeTestSchema;
using nlohmann::json;

namespace {

// INFO: the file label and pointer a corpus diagnostic is formatted with; the
// verifier's CLI uses the same pair.
constexpr std::string_view kFile = "<expression>";
constexpr std::string_view kPointer = "/";

// INFO: the only keys a corpus case and its expectation may carry. An unknown
// key is a typo the runner must report, not a silently ignored field.
constexpr std::string_view kCaseKeys[] = {"name", "expression", "expected_type", "world",
                                          "expect"};
constexpr std::string_view kExpectKeys[] = {"value", "error", "check_error"};

template <size_t Count>
bool IsAllowedKey(std::string_view key, const std::string_view (&keys)[Count]) {
    for (const std::string_view candidate : keys) {
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

// INFO: a corpus below this many cases is a mistake, not a passing run, so an
// empty or truncated `cases` array cannot pass vacuously.
constexpr size_t kMinimumCorpusCases = 190;

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

std::string ReadTextFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

// INFO: parses any JSON fixture with the duplicate-key scanner the schema
// loader uses, so a repeated key cannot lose data silently.
std::optional<json> ParseCorpusText(const std::string& text, std::string& error) {
    SchemaScan scan;
    const std::optional<json> value = ParseSchemaJson(text, scan);
    if (!value.has_value()) {
        error = "the corpus is not valid JSON";
        return std::nullopt;
    }
    if (!scan.duplicates.empty()) {
        const DuplicateKey& duplicate = scan.duplicates.front();
        error = "duplicate key \"" + duplicate.key + "\" at " + duplicate.pointer;
        return std::nullopt;
    }
    if (scan.nested_too_deep) {
        error = "the corpus nests too deeply";
        return std::nullopt;
    }
    return value;
}

json ReadScannedJsonFile(const fs::path& path) {
    const std::string text = ReadTextFile(path);
    std::string error;
    const std::optional<json> value = ParseCorpusText(text, error);
    REQUIRE_MESSAGE(value.has_value(), error);
    return *value;
}

json ReadCorpusFile(const fs::path& path) { return ReadScannedJsonFile(path); }

// INFO: checks the shape of one case before it runs: only known keys, a name
// and an expression, and exactly one expectation.
std::string ValidateCase(const json& item) {
    if (!item.is_object()) {
        return "the case must be an object";
    }
    for (auto iterator = item.begin(); iterator != item.end(); ++iterator) {
        if (!IsAllowedKey(iterator.key(), kCaseKeys)) {
            return "the case has the unknown key \"" + iterator.key() + "\"";
        }
    }
    if (!item.contains("name") || !item.contains("expression")) {
        return "the case needs a name and an expression";
    }
    if (!item.at("name").is_string()) {
        return "the name must be a string";
    }
    if (!item.at("expression").is_string()) {
        return "the expression must be a string";
    }
    const auto expected_type = item.find("expected_type");
    if (expected_type != item.end() && !expected_type->is_string()) {
        return "expected_type must be a string";
    }
    const auto world = item.find("world");
    if (world != item.end() && !world->is_object()) {
        return "world must be an object";
    }
    const auto expect = item.find("expect");
    if (expect == item.end() || !expect->is_object()) {
        return "the case needs an expect object";
    }
    for (auto iterator = expect->begin(); iterator != expect->end(); ++iterator) {
        if (!IsAllowedKey(iterator.key(), kExpectKeys)) {
            return "the case has the unknown expect key \"" + iterator.key() + "\"";
        }
    }
    size_t expectation_count = 0;
    for (const std::string_view key : kExpectKeys) {
        if (expect->contains(std::string(key))) {
            ++expectation_count;
        }
    }
    if (expectation_count != 1) {
        return "the case needs exactly one expectation";
    }
    return {};
}

// INFO: checks the expression with the declared type and matches the verifier's
// formatted diagnostic exactly, so a `check_error` case also asserts any
// suggestion and cannot pass on a truncated message.
std::string RunCheckError(const Schema& schema, const std::string& source,
                          const std::optional<Type>& expected, const json& check_error) {
    if (!check_error.is_string()) {
        return "check_error must be a string";
    }
    const CheckResult result = CheckExpression(source, schema, expected, kFile, kPointer);
    if (result.diagnostics.empty()) {
        return "expected a check error, got none";
    }
    const std::string wanted = check_error.get<std::string>();
    const std::string message = FormatDiagnostic(result.diagnostics.front());
    const std::string prefix = std::string(kFile) + "#" + std::string(kPointer) + ": ";
    if (message.rfind(prefix, 0) != 0) {
        return "the diagnostic has no file and pointer prefix: " + message;
    }
    const std::string text = message.substr(prefix.size());
    if (text != wanted) {
        return "the error \"" + text + "\" is not \"" + wanted + "\"";
    }
    return {};
}

// INFO: parses, checks and runs the expression, then matches the EvalError code.
// The checker's diagnostics are ignored because the case asserts the runtime
// error, but it still runs so the evaluator sees the widening hints.
std::string RunRuntimeError(const Schema& schema, JsonWorld& world, const std::string& source,
                            const json& error) {
    if (!error.is_string()) {
        return "error must be a string";
    }
    const std::string wanted = error.get<std::string>();
    const ParseResult parsed = ParseExpression(source);
    if (parsed.error.has_value()) {
        return "parse error: " + parsed.error->message;
    }
    static_cast<void>(CheckParsedExpression(*parsed.root, schema, std::nullopt, kFile, kPointer,
                                            match::expr::GuardState{}));
    const EvalResult result = Evaluate(*parsed.root, world);
    const EvalError* failure = std::get_if<EvalError>(&result);
    if (failure == nullptr) {
        return "expected the runtime error " + wanted + ", got a value";
    }
    if (failure->code != wanted) {
        return "expected the runtime error " + wanted + ", got " + failure->code;
    }
    return {};
}

// INFO: checks the expression, runs it and compares the decoded value. The
// check and the run share one parsed tree so the checker's widening hint
// reaches the evaluator.
std::string RunValue(const Schema& schema, const std::string& source,
                     const std::optional<Type>& expected, JsonWorld& world, const json& value) {
    const ParseResult parsed = ParseExpression(source);
    if (parsed.error.has_value()) {
        return "parse error: " + parsed.error->message;
    }
    const CheckResult checked = CheckParsedExpression(*parsed.root, schema, expected, kFile,
                                                      kPointer, match::expr::GuardState{});
    if (!checked.diagnostics.empty()) {
        return "check error: " + FormatDiagnostic(checked.diagnostics.front());
    }
    if (!checked.type.has_value()) {
        return "the checker returned no type";
    }
    const EvalResult result = Evaluate(*parsed.root, world);
    if (const EvalError* eval_error = std::get_if<EvalError>(&result)) {
        return "unexpected runtime error " + eval_error->code;
    }
    Value wanted;
    std::string error;
    if (!world.Decode(*checked.type, value, wanted, error)) {
        return "expected value: " + error;
    }
    if (!(std::get<Value>(result) == wanted)) {
        return "the value does not match the expected value";
    }
    return {};
}

// INFO: runs one corpus case and returns an empty string on success, or a
// description of the mismatch. A `check_error` case checks the expression; an
// `error` case runs it and matches the EvalError code; a `value` case checks it,
// runs it and compares the result.
std::string RunCase(const Schema& schema, const json& item) {
    const std::string invalid = ValidateCase(item);
    if (!invalid.empty()) {
        return invalid;
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
    if (const auto check_error = expect.find("check_error"); check_error != expect.end()) {
        return RunCheckError(schema, source, expected, *check_error);
    }
    if (const auto error = expect.find("error"); error != expect.end()) {
        return RunRuntimeError(schema, world, source, *error);
    }
    if (const auto value = expect.find("value"); value != expect.end()) {
        return RunValue(schema, source, expected, world, *value);
    }
    return "the case has no expectation";
}

}  // namespace

TEST_SUITE("expr conformance") {

TEST_CASE("the corpus reader rejects a duplicate key") {
    const std::string text =
        "{\"cases\": [{\"name\": \"a\", \"expression\": \"1\", "
        "\"expect\": {\"value\": 1}, \"expect\": {\"value\": 2}}]}";
    std::string error;
    CHECK_FALSE(ParseCorpusText(text, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("the corpus reader accepts valid text") {
    std::string error;
    const std::optional<json> value = ParseCorpusText("{\"cases\": []}", error);
    REQUIRE_MESSAGE(value.has_value(), error);
    CHECK(value->at("cases").is_array());
}

TEST_CASE("the runner rejects an unknown case key") {
    json item = {{"name", "a"}, {"expression", "1"}, {"expect", {{"value", 1}}}};
    item["extra"] = true;
    CHECK_FALSE(ValidateCase(item).empty());
}

TEST_CASE("the runner rejects more than one expectation") {
    json item = {{"name", "a"},
                 {"expression", "1"},
                 {"expect", {{"value", 1}, {"error", "division_by_zero"}}}};
    CHECK_FALSE(ValidateCase(item).empty());
}

TEST_CASE("the world loader rejects an unknown world key") {
    JsonWorld world(MakeTestSchema(), json{{"seat", 0}});
    CHECK_FALSE(world.error().empty());
}

TEST_CASE("the world loader rejects an unknown seat key") {
    JsonWorld world(MakeTestSchema(),
                    json{{"seats", json::array({json{{"nmae", "Ana"}}})}});
    CHECK_FALSE(world.error().empty());
}

TEST_CASE("the shared corpus passes in the C++ evaluator") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    const json schema_value =
        ReadScannedJsonFile(root / "tests" / "conformance" / "schema.json");
    std::string error;
    const std::optional<Schema> schema = LoadSchema(schema_value, error);
    REQUIRE_MESSAGE(schema.has_value(), error);

    const json corpus = ReadCorpusFile(root / "tests" / "conformance" / "expressions.json");
    const json& cases = corpus.at("cases");
    REQUIRE(cases.is_array());
    REQUIRE(cases.size() >= kMinimumCorpusCases);

    for (const json& item : cases) {
        const std::string name = item.value("name", std::string("<unnamed>"));
        const std::string failure = RunCase(*schema, item);
        CHECK_MESSAGE(failure.empty(), name, ": ", failure);
    }
}

}  // TEST_SUITE
