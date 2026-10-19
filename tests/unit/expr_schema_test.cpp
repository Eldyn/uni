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

}  // TEST_SUITE
