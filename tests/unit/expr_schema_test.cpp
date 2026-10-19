#include <doctest/doctest.h>

#include <match/expr/diagnostic.hpp>
#include <match/expr/schema_json.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;
using match::expr::Diagnostic;
using match::expr::LoadSchema;
using match::expr::ParseSchemaJson;
using match::expr::SchemaScan;

constexpr const char* kFile = "mods/a/schema.json";

// INFO: a file whose `seat_properties` object repeats one key `repetitions`
// times; valid JSON whose last value wins in the parsed object.
std::string RepeatedProperty(int repetitions) {
    std::string text = "{\"seat_properties\":{";
    for (int index = 0; index < repetitions; ++index) {
        text += (index == 0 ? "" : ",");
        text += "\"hand_size\":\"int\"";
    }
    text += "}}";
    return text;
}

std::vector<Diagnostic> LoadDiagnostics(const json& value) {
    std::vector<Diagnostic> diagnostics;
    match::expr::LoadSchema(value, kFile, diagnostics);
    return diagnostics;
}

std::vector<Diagnostic> ParseDiagnostics(const std::string& text) {
    return LoadDiagnostics(json::parse(text));
}

}  // namespace

TEST_SUITE("expr schema loader") {

TEST_CASE("a_file_with_many_repeated_keys_caps_its_diagnostics") {
    const std::string text = RepeatedProperty(100000);
    SchemaScan scan;
    const auto started = std::chrono::steady_clock::now();
    const std::optional<json> value = ParseSchemaJson(text, scan);
    REQUIRE(value.has_value());
    std::vector<Diagnostic> diagnostics;
    const std::optional<match::expr::Schema> schema = LoadSchema(*value, kFile, scan,
                                                                 diagnostics);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK_FALSE(schema.has_value());
    CHECK(diagnostics.size() <= 65);
    CHECK(elapsed < std::chrono::seconds(2));
}

TEST_CASE("a_duplicate_card_slot_is_reported_at_its_pointer") {
    const std::vector<Diagnostic> diagnostics =
        ParseDiagnostics(R"({"card_slots":["color","color"]})");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "duplicate slot \"color\"");
    CHECK(diagnostics.front().pointer == "/card_slots/1");
}

TEST_CASE("a_card_slot_that_shadows_a_card_property_is_reported") {
    const std::vector<Diagnostic> diagnostics = ParseDiagnostics(
        R"({"card_properties":{"color":"member_set"},"card_slots":["color"]})");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "slot \"color\" collides with a card property");
    CHECK(diagnostics.front().pointer == "/card_slots/0");
}

TEST_CASE("an_invalid_property_name_is_reported") {
    const std::vector<Diagnostic> diagnostics =
        ParseDiagnostics(R"({"seat_properties":{"Hand Size":"int"}})");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "invalid property name \"Hand Size\"");
    CHECK(diagnostics.front().pointer == "/seat_properties/Hand Size");
}

TEST_CASE("an_invalid_variable_name_is_reported") {
    const std::vector<Diagnostic> diagnostics =
        ParseDiagnostics(R"({"variables":{"Hand Size":{"type":"int"}}})");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "invalid variable name \"Hand Size\"");
    CHECK(diagnostics.front().pointer == "/variables/Hand Size");
}

TEST_CASE("an_invalid_card_slot_name_is_reported") {
    const std::vector<Diagnostic> diagnostics =
        ParseDiagnostics(R"({"card_slots":["Hand Size"]})");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "invalid slot name \"Hand Size\"");
    CHECK(diagnostics.front().pointer == "/card_slots/0");
}

TEST_CASE("a_namespaced_name_is_accepted") {
    const std::string text =
        R"({"seat_properties":{"axe:stunned":"bool"},"variables":{"axe:seen":{"type":"int"}}})";
    CHECK(ParseDiagnostics(text).empty());
}

TEST_CASE("an_unknown_root_key_is_reported_at_the_child_pointer") {
    const std::vector<Diagnostic> diagnostics = ParseDiagnostics(R"({"seat_propertes":{}})");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "unknown key \"seat_propertes\"");
    CHECK(diagnostics.front().pointer == "/seat_propertes");
    CHECK(diagnostics.front().suggestion == "seat_properties");
}

TEST_CASE("an_unknown_variable_key_is_reported_at_the_child_pointer") {
    const std::vector<Diagnostic> diagnostics =
        ParseDiagnostics(R"({"variables":{"played":{"type":"int","optional_":true}}})");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "unknown key \"optional_\"");
    CHECK(diagnostics.front().pointer == "/variables/played/optional_");
    CHECK(diagnostics.front().suggestion == "optional");
}

TEST_CASE("a_pointer_escapes_a_slash_and_a_tilde") {
    const std::vector<Diagnostic> root = ParseDiagnostics(R"({"a/b":1})");
    REQUIRE(root.size() == 1);
    CHECK(root.front().pointer == "/a~1b");

    const std::vector<Diagnostic> property =
        ParseDiagnostics(R"({"seat_properties":{"a/b~c":"int"}})");
    REQUIRE(property.size() == 1);
    CHECK(property.front().message == "invalid property name \"a/b~c\"");
    CHECK(property.front().pointer == "/seat_properties/a~1b~0c");
}

TEST_CASE("the_readable_error_overload_formats_a_diagnostic") {
    std::string error;
    const json value = json::parse(R"({"seat_properties":{"hand_sze":"unt"}})");
    CHECK_FALSE(match::expr::LoadSchema(value, error).has_value());
    CHECK(error ==
          "#/seat_properties/hand_sze: unknown type \"unt\" (did you mean \"int\")");
}

}  // TEST_SUITE
