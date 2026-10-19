#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// INFO: the usage line of the mods verifier, and the phase-1 line the tool falls
// back to; a bad command line prints one of them verbatim.
const char* kModsUsage =
    "usage: uni-mods verify --mods-root <dir> [--mod <id>] [--deck <identifier>]\n";
const char* kSchemaUsage =
    "usage: uni_mods verify --schema <file> --expression <text> [--expect <type>]\n";

struct RunResult {
    int code = 0;
    std::string out;
    std::string err;
};

RunResult RunMods(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = match::cardschema::RunVerifyMods(args, out, err);
    return RunResult{code, out.str(), err.str()};
}

RunResult RunCommand(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = match::cardschema::RunVerifyCommand(args, out, err);
    return RunResult{code, out.str(), err.str()};
}

// INFO: a scratch directory outside the repository for the tests whose shape is
// generated (65 mods, 257 decks, an oversized manifest, a symlinked folder).
class ScratchDirectory {
public:
    explicit ScratchDirectory(const std::string& name) {
        static std::size_t counter = 0;
        path_ = fs::temp_directory_path() /
                ("uni_verify_mods_" + name + "_" + std::to_string(++counter));
        std::error_code error;
        fs::remove_all(path_, error);
        fs::create_directories(path_);
    }

    ~ScratchDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << text;
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
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("clean")});

    CHECK(result.code == 0);
    CHECK(result.err.empty());
    CHECK(result.out == "ok\ndeck uni:mini: 8 cards\n");
}

TEST_CASE("broken_mod_diagnostic") {
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("broken")});

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    const std::string last = LastLine(result.err);
    CHECK(last.starts_with("mods/uni/kinds/"));
    CHECK(last.find("unknown placeholder \"colour\" (did you mean \"color\")") !=
          std::string::npos);
}

TEST_CASE("missing_root_arg_returns_2") {
    const RunResult result = RunMods({"verify"});

    CHECK(result.code == 2);
    CHECK(result.out.empty());
    CHECK(result.err == kModsUsage);
}

TEST_CASE("repeated_mods_root_returns_2") {
    const RunResult result =
        RunMods({"verify", "--mods-root", "a", "--mods-root", "b"});

    CHECK(result.code == 2);
    CHECK(result.err == std::string("flag \"--mods-root\" given twice\n") + kModsUsage);
}

TEST_CASE("repeated_mod_returns_2") {
    const RunResult result = RunMods(
        {"verify", "--mods-root", Fixture("clean"), "--mod", "a", "--mod", "b"});

    CHECK(result.code == 2);
    CHECK(result.err == std::string("flag \"--mod\" given twice\n") + kModsUsage);
}

TEST_CASE("repeated_deck_returns_2") {
    const RunResult result = RunMods(
        {"verify", "--mods-root", Fixture("clean"), "--deck", "a", "--deck", "b"});

    CHECK(result.code == 2);
    CHECK(result.err == std::string("flag \"--deck\" given twice\n") + kModsUsage);
}

TEST_CASE("flag_without_value_returns_2") {
    const RunResult result = RunMods({"verify", "--mods-root"});

    CHECK(result.code == 2);
    CHECK(result.err == std::string("--mods-root needs a value\n") + kModsUsage);
}

TEST_CASE("unknown_flag_returns_2") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("clean"), "--bogus"});

    CHECK(result.code == 2);
    CHECK(result.err == std::string("unknown flag \"--bogus\"\n") + kModsUsage);
}

TEST_CASE("wrong_command_returns_2") {
    const RunResult result = RunMods({"notverify", "--mods-root", Fixture("clean")});

    CHECK(result.code == 2);
    CHECK(result.err == std::string("unknown command \"notverify\"\n") + kModsUsage);
}

TEST_CASE("mods_root_on_a_file_returns_3") {
    const std::string path = Fixture("clean") + "/uni/mod.json";
    const RunResult result = RunMods({"verify", "--mods-root", path});

    CHECK(result.code == 3);
    CHECK(result.out.empty());
    CHECK(result.err == "cannot read mods root \"" + path + "\": not a directory\n");
}

TEST_CASE("mods_root_on_a_missing_directory_returns_3") {
    const std::string path = Fixture("clean") + "/absent";
    const RunResult result = RunMods({"verify", "--mods-root", path});

    CHECK(result.code == 3);
    CHECK(result.out.empty());
    CHECK(result.err ==
          "cannot read mods root \"" + path + "\": No such file or directory\n");
}

TEST_CASE("no_mods_found_returns_1") {
    const std::string path = Fixture("no_mods");
    const RunResult result = RunMods({"verify", "--mods-root", path});

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    CHECK(result.err == "no mods found in \"" + path + "\"\n");
}

TEST_CASE("a_folder_without_a_manifest_is_skipped") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("skipped_folder")});

    CHECK(result.code == 0);
    CHECK(result.err.empty());
    CHECK(result.out == "ok\n");
}

TEST_CASE("unknown_mod_returns_1") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("clean"), "--mod", "un"});

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    CHECK(result.err == Fixture("clean") +
                            "#/: unknown mod \"un\" (did you mean \"uni\")\n");
}

TEST_CASE("unknown_deck_returns_1") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("clean"), "--deck", "uni:min"});

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    CHECK(result.err ==
          Fixture("clean") + "#/: unknown deck \"uni:min\" (did you mean \"uni:mini\")\n");
}

TEST_CASE("selected_mod_hides_another_mod_error") {
    const std::string root = Fixture("mod_local");

    const RunResult all = RunMods({"verify", "--mods-root", root});
    CHECK(all.code == 1);
    CHECK(all.err.find("mods/beta/") != std::string::npos);

    const RunResult selected =
        RunMods({"verify", "--mods-root", root, "--mod", "alpha"});
    CHECK(selected.code == 0);
    CHECK(selected.err.empty());
    CHECK(selected.out == "ok\n");
}

TEST_CASE("selected_mod_keeps_a_cross_mod_error") {
    const std::string root = Fixture("cross_mod");
    const std::string line =
        "mods/beta/attributes/color/red.json#/attribute_member/attribute: members "
        "\"alpha:red\" and \"beta:red\" share the local name \"red\"; set "
        "identifier_alias on one\n";

    const RunResult all = RunMods({"verify", "--mods-root", root});
    CHECK(all.code == 1);
    CHECK(all.err == line);

    const RunResult selected =
        RunMods({"verify", "--mods-root", root, "--mod", "alpha"});
    CHECK(selected.code == 1);
    CHECK(selected.err == line);
}

TEST_CASE("invalid_manifest_json_returns_1") {
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("invalid_json")});

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    CHECK(result.err == "mods/uni/mod.json#/: invalid JSON\n");
}

TEST_CASE("non_object_manifest_returns_1") {
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("non_object")});

    CHECK(result.code == 1);
    CHECK(result.err == "mods/uni/mod.json#/: file must be a JSON object\n");
}

TEST_CASE("non_string_id_returns_1") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("non_string_id")});

    CHECK(result.code == 1);
    CHECK(result.err == "mods/uni/mod.json#/id: mod \"id\" must be a string\n");
}

TEST_CASE("mismatched_id_returns_1") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("mismatched_id")});

    CHECK(result.code == 1);
    CHECK(result.err ==
          "mods/uni/mod.json#/id: mod id \"other\" does not match the folder name "
          "\"uni\"\n");
}

TEST_CASE("missing_id_falls_back_to_the_folder_name") {
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("missing_id")});

    CHECK(result.code == 0);
    CHECK(result.err.empty());
    CHECK(result.out == "ok\n");
}

TEST_CASE("empty_id_returns_1") {
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("empty_id")});

    CHECK(result.code == 1);
    CHECK(result.err == "mods/uni/mod.json#/id: mod \"id\" must not be empty\n");
}

TEST_CASE("oversized_manifest_returns_1") {
    ScratchDirectory scratch("oversized");
    const std::string padding(1024 * 1024 + 1, 'a');
    WriteText(scratch.path() / "uni" / "mod.json",
              "{\"id\":\"uni\",\"padding\":\"" + padding + "\"}\n");

    const RunResult result =
        RunMods({"verify", "--mods-root", scratch.path().generic_string()});

    CHECK(result.code == 1);
    CHECK(result.err ==
          "mods/uni/mod.json#/: file is too large (more than 1048576 bytes)\n");
}

TEST_CASE("malformed_settings_return_1") {
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("bad_settings")});

    CHECK(result.code == 1);
    CHECK(result.err ==
          "mods/uni/mod.json#/settings/0: setting \"no_default\" must have an integer "
          "default\n"
          "mods/uni/mod.json#/settings/1: setting \"id\" must be a non-empty string\n"
          "mods/uni/mod.json#/settings/2: setting \"bad_type\" must have type \"int\"\n"
          "mods/uni/mod.json#/settings/3: setting \"bad_default\" must have an integer "
          "default\n");
}

TEST_CASE("duplicate_setting_id_names_both_mods") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("duplicate_setting")});

    CHECK(result.code == 1);
    CHECK(result.err ==
          "mods/beta/mod.json#/settings/0: setting id \"count_wild\" is declared by "
          "mods \"alpha\" and \"beta\"\n");
}

TEST_CASE("missing_setting_is_reported_by_the_deck") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("missing_setting")});

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    CHECK(result.err ==
          "mods/uni/decks/one.json#/deck/cards/0/copies: unknown property "
          "\"count_wild\" on settings\n");
}

TEST_CASE("selected_deck_prints_only_that_deck") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("clean"), "--deck", "uni:mini"});

    CHECK(result.code == 0);
    CHECK(result.err.empty());
    CHECK(result.out == "ok\ndeck uni:mini: 8 cards\n");
}

TEST_CASE("selected_mod_restricts_the_decks") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("two_decks"), "--mod", "alpha"});

    CHECK(result.code == 0);
    CHECK(result.err.empty());
    CHECK(result.out == "ok\ndeck alpha:one: 0 cards\n");
}

TEST_CASE("deck_of_another_mod_is_unknown") {
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("two_decks"),
                                      "--mod", "alpha", "--deck", "beta:one"});

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    CHECK(result.err ==
          Fixture("two_decks") + "#/: unknown deck \"beta:one\"\n");
}

TEST_CASE("duplicate_diagnostics_are_printed_once") {
    const RunResult result = RunMods({"verify", "--mods-root", Fixture("dedupe")});

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    CHECK(result.err == "mods/uni/decks/one.json#/deck/cards/0: unknown kind "
                        "\"uni:nope\"\n");
}

TEST_CASE("diagnostics_are_capped_at_100") {
    const RunResult result =
        RunMods({"verify", "--mods-root", Fixture("many_diagnostics")});

    std::string expected;
    for (int index = 0; index < 100; ++index) {
        expected += "mods/uni/decks/big.json#/deck/cards/" + std::to_string(index) +
                    ": unknown kind \"uni:nope\"\n";
    }
    expected += "more diagnostics not shown\n";

    CHECK(result.code == 1);
    CHECK(result.out.empty());
    CHECK(result.err == expected);
}

TEST_CASE("too_many_mods_returns_1") {
    ScratchDirectory scratch("many_mods");
    for (int index = 0; index < 65; ++index) {
        const std::string name = "m" + std::to_string(index);
        WriteText(scratch.path() / name / "mod.json",
                  "{\"id\":\"" + name + "\"}\n");
    }

    const RunResult result =
        RunMods({"verify", "--mods-root", scratch.path().generic_string()});

    CHECK(result.code == 1);
    CHECK(result.err ==
          scratch.path().generic_string() + "#/: too many mods (more than 64)\n");
}

TEST_CASE("too_many_decks_returns_1") {
    ScratchDirectory scratch("many_decks");
    const fs::path folder = scratch.path() / "uni";
    WriteText(folder / "mod.json", "{\"id\":\"uni\"}\n");
    for (int index = 0; index < 257; ++index) {
        const std::string name = "d" + std::to_string(index);
        WriteText(folder / "decks" / (name + ".json"),
                  "{\"format_version\":\"1.0.0\",\"deck\":{\"identifier\":\"uni:" + name +
                      "\",\"name\":\"D\",\"cards\":[]}}\n");
    }

    const RunResult result =
        RunMods({"verify", "--mods-root", scratch.path().generic_string()});

    CHECK(result.code == 1);
    CHECK(result.err ==
          scratch.path().generic_string() + "#/: too many decks (more than 256)\n");
}

TEST_CASE("symlinked_mod_folder_is_not_allowed") {
    ScratchDirectory scratch("symlink");
    WriteText(scratch.path() / "real" / "mod.json", "{\"id\":\"real\"}\n");
    std::error_code error;
    fs::create_directory_symlink(scratch.path() / "real", scratch.path() / "linked",
                                 error);
    REQUIRE_FALSE(static_cast<bool>(error));

    const RunResult result =
        RunMods({"verify", "--mods-root", scratch.path().generic_string()});

    CHECK(result.code == 1);
    CHECK(result.err == "linked#/: symbolic links are not allowed\n");
}

TEST_CASE("main_dispatch_rejects_schema_with_mods_root") {
    const RunResult result =
        RunCommand({"verify", "--schema", "s.json", "--mods-root", "dir"});

    CHECK(result.code == 2);
    CHECK(result.err ==
          std::string("--mods-root cannot be combined with --schema\n") + kModsUsage);
}

TEST_CASE("main_dispatch_ignores_values") {
    // `--mods-root` here is the value of `--expression`, not a flag position.
    const RunResult result = RunCommand({"verify", "--expression", "--mods-root"});

    CHECK(result.code == 2);
    CHECK(result.err == kSchemaUsage);
}

TEST_CASE("main_dispatch_runs_the_mods_verifier") {
    const RunResult result =
        RunCommand({"verify", "--mods-root", Fixture("clean")});

    CHECK(result.code == 0);
    CHECK(result.out == "ok\ndeck uni:mini: 8 cards\n");
}

}  // namespace
