#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

/**
 * @file old_event_names_guard_test.cpp
 * @brief No source file names an old event or an old envelope field.
 *
 * The event contract renamed nine events and two envelope fields. The old
 * names are gone from the engine, the contract, the mods, the tests and the
 * client. This test scans the files and fails when one old name returns.
 *
 * The old names are built from parts so that this file stays clean. A mod hook
 * keeps its bare name (`match_end`, `window_open`, `window_close`): a hook is
 * not an event, so a hook line is allowed.
 */

namespace fs = std::filesystem;

namespace {

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        const fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

std::vector<std::string> OldEventNames() {
    return {std::string("window") + "_open",
            std::string("window") + "_response",
            std::string("window") + "_close",
            std::string("prompt") + "_open",
            std::string("prompt") + "_close",
            std::string("roll") + "_result",
            std::string("outcome") + "_result",
            std::string("place") + "ment",
            std::string("match") + "_end"};
}

std::string Alternation(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        out += (out.empty() ? "" : "|") + name;
    }
    return out;
}

bool IsScannedFile(const fs::path& path) {
    static const std::set<std::string> kExtensions = {
        ".cpp", ".hpp", ".h", ".ts", ".svelte", ".json", ".yaml", ".md"};
    return kExtensions.count(path.extension().string()) > 0;
}

bool IsExcluded(const fs::path& relative) {
    static const std::set<std::string> kSkippedDirectories = {
        "node_modules", "build", "docs", ".git", ".svelte-kit", "src-tauri"};
    for (const fs::path& part : relative) {
        if (kSkippedDirectories.count(part.string()) > 0) return true;
    }
    const std::string name = relative.filename().string();
    return name == "CHANGELOG.md" || name == "old_event_names_guard_test.cpp"
           || name == "oldEventNames.guard.test.ts";
}

bool IsHookLine(const fs::path& relative, const std::string& line) {
    const std::string name = relative.filename().string();
    if (name == "vocabulary.hpp" || name == "hooks.hpp"
        || name == "analytics.svelte.ts") {
        return true;
    }
    if (line.find("nalytics") != std::string::npos) return true;
    if (line.find("callsFor(") != std::string::npos) return true;
    if (line.find("hook") != std::string::npos) return true;
    if (line.find("Hook") != std::string::npos) return true;
    // INFO: the match history table has a `placement` column. It is not an
    //       event.
    if (line.find("Get<int>(\"placement\")") != std::string::npos) return true;
    static const std::regex kHookCall(R"((Before|After)\()");
    return std::regex_search(line, kHookCall);
}

struct Finding {
    std::string where;
    std::string text;
};

std::vector<Finding> Scan() {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    const std::string names = Alternation(OldEventNames());
    const std::regex quoted("([\"'`])(" + names + ")\\1");
    const std::regex yaml_key("^\\s*(" + names + "):");
    const std::string seq = std::string("s") + "eq";
    const std::regex old_field_quoted("\"" + seq + "\"");
    const std::regex old_field_member("\\b(env|envelope|entry|packet|frame|data)\\.(" + seq + ")\\b");
    const std::regex old_field_yaml("^\\s*" + seq + ":|required: \\[" + seq + "[,\\]]");
    const std::regex old_alias_table("kEvent" + std::string("Alias"));

    std::vector<Finding> findings;
    const std::vector<std::string> roots = {
        "src", "include", "tests", "contract", "mods", "frontend/src",
        "README.md", "BUILD.md"};
    for (const std::string& entry : roots) {
        const fs::path start = root / entry;
        std::error_code error;
        if (!fs::exists(start, error)) continue;
        std::vector<fs::path> files;
        if (fs::is_regular_file(start, error)) {
            files.push_back(start);
        } else {
            for (const auto& item : fs::recursive_directory_iterator(start)) {
                if (item.is_regular_file()) files.push_back(item.path());
            }
        }
        for (const fs::path& file : files) {
            const fs::path relative = fs::relative(file, root);
            if (IsExcluded(relative) || !IsScannedFile(file)) continue;
            std::ifstream input(file);
            std::string line;
            int number = 0;
            while (std::getline(input, line)) {
                ++number;
                const bool hook = IsHookLine(relative, line);
                const bool is_yaml = file.extension() == ".yaml";
                bool hit = std::regex_search(line, old_alias_table);
                if (!hook) {
                    hit = hit || std::regex_search(line, quoted)
                          || (is_yaml && std::regex_search(line, yaml_key));
                }
                hit = hit || std::regex_search(line, old_field_quoted);
                hit = hit || (is_yaml
                              && std::regex_search(line, old_field_yaml));
                hit = hit || (file.extension() == ".ts"
                              && std::regex_search(line, old_field_member));
                if (hit) {
                    findings.push_back(
                        {relative.string() + ":" + std::to_string(number),
                         line});
                }
            }
        }
    }
    return findings;
}

}  // namespace

TEST_CASE("guard: no old event name or old envelope field in the sources") {
    const std::vector<Finding> findings = Scan();
    std::ostringstream report;
    for (std::size_t index = 0; index < findings.size() && index < 40;
         ++index) {
        report << findings[index].where << ": " << findings[index].text << "\n";
    }
    INFO(findings.size() << " old names remain:\n" << report.str());
    CHECK(findings.empty());
}

TEST_CASE("guard: the scan finds a planted old name") {
    const std::string names = Alternation(OldEventNames());
    const std::regex quoted("([\"'`])(" + names + ")\\1");
    CHECK(std::regex_search("Emit(\"" + OldEventNames()[0] + "\", body);",
                            quoted));
    CHECK_FALSE(std::regex_search("Emit(\"window_opened\", body);", quoted));
    CHECK_FALSE(std::regex_search("Emit(\"placement_set\", body);", quoted));
    CHECK_FALSE(std::regex_search("\"outcome_rolled\"", quoted));
}
