#include <doctest/doctest.h>

#include <match/expr/verify_command.hpp>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

}  // TEST_SUITE
