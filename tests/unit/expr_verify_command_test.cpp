#include <doctest/doctest.h>

#include <match/expr/verify_command.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

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

std::string SchemaPath() {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    return (root / "tests" / "conformance" / "schema.json").string();
}

json ReadSchema() {
    std::ifstream stream(SchemaPath());
    REQUIRE(stream);
    json value;
    stream >> value;
    return value;
}

// INFO: a unique directory under the system temp directory; it is removed when
// the fixture goes out of scope.
class TempSchemas {
public:
    TempSchemas() {
        std::random_device device;
        directory_ = fs::temp_directory_path() /
                     ("uni_verify_" + std::to_string(device()) + "_" + std::to_string(device()));
        fs::create_directories(directory_);
    }

    ~TempSchemas() {
        std::error_code error;
        fs::remove_all(directory_, error);
    }

    std::string Write(const std::string& name, const std::string& contents) const {
        const fs::path path = directory_ / name;
        std::ofstream stream(path, std::ios::binary);
        stream << contents;
        return path.string();
    }

    std::string Path(const std::string& name) const { return (directory_ / name).string(); }

    std::string Directory() const { return directory_.string(); }

private:
    fs::path directory_;
};

struct Outcome {
    int code;
    std::string out;
    std::string err;
};

Outcome Run(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = match::expr::RunVerify(args, out, err);
    return Outcome{code, out.str(), err.str()};
}

struct VerifyCase {
    std::string name;
    std::vector<std::string> args;
    int exit_code;
    std::string expected_out;
    std::string expected_err;
};

constexpr std::string_view kUsage =
    "usage: uni_mods verify --schema <file> --expression <text> [--expect <type>]\n";

}  // namespace

TEST_SUITE("expr verify command") {

TEST_CASE("a_clean_expression_prints_ok") {
    std::ostringstream out;
    std::ostringstream err;
    const int code = match::expr::RunVerify(
        {"verify", "--schema", SchemaPath(), "--expression", "@next.hand_size + 1", "--expect",
         "int"},
        out, err);
    CHECK(code == 0);
    CHECK(out.str() == "ok\n");
}

TEST_CASE("a_bad_expression_prints_each_diagnostic") {
    std::ostringstream out;
    std::ostringstream err;
    const int code = match::expr::RunVerify(
        {"verify", "--schema", SchemaPath(), "--expression", "@next.hand_sze"}, out, err);
    CHECK(code == 1);
    CHECK(err.str() ==
          "<expression>#/: unknown property \"hand_sze\" on seat (did you mean "
          "\"hand_size\")\n");
}

TEST_CASE("a_missing_schema_is_a_usage_error") {
    std::ostringstream out;
    std::ostringstream err;
    const int code =
        match::expr::RunVerify({"verify", "--expression", "1 + 1"}, out, err);
    CHECK(code == 2);
}

TEST_CASE("every_argument_and_schema_failure_reports_the_same_way") {
    const TempSchemas temp;
    const std::string valid = temp.Write("valid.json", ReadSchema().dump(2));
    const std::string missing = temp.Path("does_not_exist.json");
    const std::string directory = temp.Directory();
    const std::string invalid_json = temp.Write("invalid.json", "{ not json");

    json unknown_type = ReadSchema();
    unknown_type["seat_properties"]["hand"] = "unt";
    const std::string unknown_type_path = temp.Write("unknown_type.json", unknown_type.dump(2));

    json unknown_root_key = ReadSchema();
    unknown_root_key["seat_propertes"] = unknown_root_key["seat_properties"];
    const std::string unknown_root_key_path =
        temp.Write("unknown_root_key.json", unknown_root_key.dump(2));

    json unknown_variable_key = ReadSchema();
    unknown_variable_key["variables"]["played"]["optional_"] = true;
    const std::string unknown_variable_key_path =
        temp.Write("unknown_variable_key.json", unknown_variable_key.dump(2));

    json several = ReadSchema();
    several["zzz"] = 1;
    several["variables"]["played"]["optional_"] = true;
    const std::string several_path = temp.Write("several.json", several.dump(2));

    json unknown_key_and_type = ReadSchema();
    unknown_key_and_type["zzz"] = 1;
    unknown_key_and_type["seat_properties"]["hand"] = "unt";
    const std::string unknown_key_and_type_path =
        temp.Write("unknown_key_and_type.json", unknown_key_and_type.dump(2));

    const std::string duplicate_root =
        temp.Write("duplicate_root.json", R"({"seat_properties": {}, "seat_properties": {}})");
    const std::string duplicate_nested = temp.Write(
        "duplicate_nested.json",
        R"({"seat_properties": {"hand_size": "int", "hand_size": "bool"}})");
    const std::string escaped_pointer = temp.Write(
        "escaped_pointer.json", R"({"seat_properties": {"a/b~c": "xyz"}})");

    const std::string too_large =
        temp.Write("too_large.json", std::string(1024 * 1024 + 8, ' '));

    const std::vector<VerifyCase> cases = {
        {"a_nonexistent_schema_file",
         {"verify", "--schema", missing, "--expression", "1"},
         3,
         "",
         "cannot read \"" + missing + "\": No such file or directory\n"},
        {"a_directory_as_schema",
         {"verify", "--schema", directory, "--expression", "1"},
         3,
         "",
         "cannot read \"" + directory + "\": not a regular file\n"},
        {"invalid_json_schema",
         {"verify", "--schema", invalid_json, "--expression", "1"},
         3,
         "",
         "the schema file \"" + invalid_json + "\" is not valid JSON\n"},
        {"an_unknown_type_in_the_schema",
         {"verify", "--schema", unknown_type_path, "--expression", "1"},
         1,
         "",
         unknown_type_path +
             "#/seat_properties/hand: unknown type \"unt\" (did you mean \"int\")\n"},
        {"an_unknown_top_level_key",
         {"verify", "--schema", unknown_root_key_path, "--expression", "1"},
         1,
         "",
         unknown_root_key_path +
             "#/seat_propertes: unknown key \"seat_propertes\" (did you mean "
             "\"seat_properties\")\n"},
        {"an_unknown_key_in_a_variable",
         {"verify", "--schema", unknown_variable_key_path, "--expression", "1"},
         1,
         "",
         unknown_variable_key_path +
             "#/variables/played/optional_: unknown key \"optional_\" (did you mean "
             "\"optional\")\n"},
        {"several_diagnostics_are_all_printed",
         {"verify", "--schema", several_path, "--expression", "1"},
         1,
         "",
         several_path + "#/zzz: unknown key \"zzz\"\n" + several_path +
             "#/variables/played/optional_: unknown key \"optional_\" (did you mean "
             "\"optional\")\n"},
        {"an_unknown_key_and_an_unknown_type_are_both_printed",
         {"verify", "--schema", unknown_key_and_type_path, "--expression", "1"},
         1,
         "",
         unknown_key_and_type_path + "#/zzz: unknown key \"zzz\"\n" +
             unknown_key_and_type_path +
             "#/seat_properties/hand: unknown type \"unt\" (did you mean \"int\")\n"},
        {"a_duplicate_top_level_key",
         {"verify", "--schema", duplicate_root, "--expression", "1"},
         1,
         "",
         duplicate_root + "#/: duplicate key \"seat_properties\"\n"},
        {"a_duplicate_key_in_a_nested_object",
         {"verify", "--schema", duplicate_nested, "--expression", "1"},
         1,
         "",
         duplicate_nested + "#/seat_properties: duplicate key \"hand_size\"\n"},
        {"a_pointer_escapes_special_characters",
         {"verify", "--schema", escaped_pointer, "--expression", "1"},
         1,
         "",
         escaped_pointer +
             "#/seat_properties/a~1b~0c: invalid property name \"a/b~c\"\n"},
        {"an_unknown_flag",
         {"verify", "--schema", valid, "--expression", "1", "--foo"},
         2,
         "",
         std::string("unknown flag \"--foo\"\n") + std::string(kUsage)},
        {"a_missing_flag_value",
         {"verify", "--schema"},
         2,
         "",
         std::string("--schema needs a value\n") + std::string(kUsage)},
        {"a_repeated_flag",
         {"verify", "--schema", valid, "--schema", valid, "--expression", "1"},
         2,
         "",
         std::string("flag \"--schema\" given twice\n") + std::string(kUsage)},
        {"a_repeated_expression_flag",
         {"verify", "--schema", valid, "--expression", "1", "--expression", "1"},
         2,
         "",
         std::string("flag \"--expression\" given twice\n") + std::string(kUsage)},
        {"a_repeated_expect_flag",
         {"verify", "--schema", valid, "--expression", "1", "--expect", "int", "--expect", "int"},
         2,
         "",
         std::string("flag \"--expect\" given twice\n") + std::string(kUsage)},
        {"an_unknown_expect_type",
         {"verify", "--schema", valid, "--expression", "1", "--expect", "unt"},
         2,
         "",
         std::string("unknown type \"unt\" for --expect\n") + std::string(kUsage)},
        {"an_expected_type_mismatch",
         {"verify", "--schema", valid, "--expression", "1 + 1", "--expect", "bool"},
         1,
         "",
         "<expression>#/: expected bool, got int\n"},
        {"a_parse_error",
         {"verify", "--schema", valid, "--expression", "1 +"},
         1,
         "",
         "<expression>#/: expected an expression after '+'\n"},
        {"an_empty_expression",
         {"verify", "--schema", valid, "--expression", ""},
         1,
         "",
         "<expression>#/: empty expression\n"},
        {"an_expression_over_the_length_limit",
         {"verify", "--schema", valid, "--expression", std::string(65536 + 1, 'a')},
         1,
         "",
         "expression is too long\n"},
        {"a_schema_file_over_the_size_limit",
         {"verify", "--schema", too_large, "--expression", "1"},
         3,
         "",
         "schema file is too large\n"},
    };

    for (const VerifyCase& item : cases) {
        const Outcome outcome = Run(item.args);
        CHECK_MESSAGE(outcome.code == item.exit_code, item.name, ": exit code");
        CHECK_MESSAGE(outcome.out == item.expected_out, item.name, ": stdout");
        CHECK_MESSAGE(outcome.err == item.expected_err, item.name, ": stderr");
    }
}

TEST_CASE("a_deeply_nested_schema_file_is_rejected") {
    TempSchemas schemas;
    std::string text;
    for (int level = 0; level < 65; ++level) {
        text += "{\"a\":";
    }
    text += "1";
    for (int level = 0; level < 65; ++level) {
        text += "}";
    }
    const std::string path = schemas.Write("deep.json", text);
    const Outcome outcome = Run({"verify", "--schema", path, "--expression", "1"});
    CHECK(outcome.code == 1);
    CHECK(outcome.err == path + "#/: schema is nested too deeply (more than 64 levels)\n");
}

TEST_CASE("a_schema_object_with_many_keys_loads_quickly") {
    TempSchemas schemas;
    std::string text = "{\"seat_properties\":{";
    constexpr int kKeyCount = 50000;
    for (int index = 0; index < kKeyCount; ++index) {
        text += (index == 0 ? "" : ",");
        text += "\"p" + std::to_string(index) + "\":\"int\"";
    }
    text += "}}";
    REQUIRE(text.size() < 1024 * 1024);
    const std::string path = schemas.Write("many.json", text);
    const auto started = std::chrono::steady_clock::now();
    const Outcome outcome = Run({"verify", "--schema", path, "--expression", "1"});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(outcome.code == 0);
    CHECK(elapsed < std::chrono::seconds(2));
}

}  // TEST_SUITE
