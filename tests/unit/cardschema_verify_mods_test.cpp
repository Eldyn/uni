#include <doctest/doctest.h>

#include <filesystem>
#include <sstream>
#include <string>

#include <match/cardschema/verify_mods.hpp>

namespace {

namespace fs = std::filesystem;

// INFO: walk up from this test file to the repository root, so the fixture
// folders resolve no matter where the test binary is started.
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

std::string Fixture(const std::string& name) {
    return (ProjectRoot() / "tests" / "fixtures" / "card_schema" / name).generic_string();
}

// INFO: the last line of `text` without its trailing newline, so a test can
// assert on the final diagnostic exactly.
std::string LastLine(std::string text) {
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    const std::size_t newline = text.rfind('\n');
    return newline == std::string::npos ? text : text.substr(newline + 1);
}

}  // namespace

TEST_SUITE("cardschema verify mods") {

TEST_CASE("clean_mods_root") {
    std::ostringstream out;
    std::ostringstream err;

    const int code = match::cardschema::RunVerifyMods(
        {"verify", "--mods-root", Fixture("clean")}, out, err);

    CHECK(code == 0);
    CHECK(err.str().empty());
    CHECK(out.str() == "ok\ndeck uni:mini: 8 cards\n");
}

TEST_CASE("broken_mod_diagnostic") {
    std::ostringstream out;
    std::ostringstream err;

    const int code = match::cardschema::RunVerifyMods(
        {"verify", "--mods-root", Fixture("broken")}, out, err);

    CHECK(code == 1);
    CHECK(out.str().empty());
    const std::string last = LastLine(err.str());
    CHECK(last.starts_with("mods/uni/kinds/"));
    CHECK(last.find("unknown placeholder \"colour\" (did you mean \"color\")") !=
          std::string::npos);
}

TEST_CASE("missing_root_arg_returns_2") {
    std::ostringstream out;
    std::ostringstream err;

    const int code = match::cardschema::RunVerifyMods({"verify"}, out, err);

    CHECK(code == 2);
    CHECK(out.str().empty());
    CHECK(err.str().find("usage:") != std::string::npos);
}

}  // namespace
