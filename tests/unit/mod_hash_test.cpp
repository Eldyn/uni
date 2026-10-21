#include <doctest/doctest.h>

#include <match/content_wiring.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_hash.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/view/defs_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

/**
 * @file mod_hash_test.cpp
 * @brief The content hash of a mod, and the mod list of the `defs` packet.
 *
 * The golden file `tests/fixtures/golden_mod_event/defs_mods.json` holds the
 * mod list and the event manifest of the fixture mod. Set `UNI_UPDATE_GOLDEN=1`
 * to write it again; a default run never writes it.
 */

namespace fs = std::filesystem;
using match::modload::ComputeModContentHash;
using nlohmann::json;

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

struct TempFolder {
    fs::path root;
    TempFolder() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_mod_hash_" + std::to_string(::getpid()) + "_"
                + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempFolder() { fs::remove_all(root); }

    void Write(const std::string& relative, const std::string& contents) const {
        const fs::path file = root / relative;
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << contents;
    }
};

void FillMod(const TempFolder& folder, const std::string& event_text) {
    folder.Write("mod.json",
                 R"({"id":"my_mod","name":"t","version":"1.2.3","api":"1"})");
    folder.Write("events/a.json", event_text);
    folder.Write("rules/r.json", "{}");
}

bool IsLowerHex64(const std::string& text) {
    if (text.size() != 64) return false;
    for (const char character : text) {
        if (!std::isxdigit(static_cast<unsigned char>(character))
            || std::isupper(static_cast<unsigned char>(character))) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("mod hash: the same files give the same hash, in any folder") {
    TempFolder first;
    TempFolder second;
    FillMod(first, "A");
    FillMod(second, "A");
    const std::string hash = ComputeModContentHash(first.root.string());
    CHECK(IsLowerHex64(hash));
    CHECK(hash == ComputeModContentHash(second.root.string()));
    CHECK(hash == ComputeModContentHash(first.root.string()));
}

TEST_CASE("mod hash: a changed, added or renamed file changes the hash") {
    TempFolder folder;
    FillMod(folder, "A");
    const std::string original = ComputeModContentHash(folder.root.string());

    folder.Write("events/a.json", "B");
    const std::string changed = ComputeModContentHash(folder.root.string());
    CHECK(changed != original);

    folder.Write("events/a.json", "A");
    CHECK(ComputeModContentHash(folder.root.string()) == original);

    folder.Write("events/extra.json", "A");
    const std::string added = ComputeModContentHash(folder.root.string());
    CHECK(added != original);

    fs::remove(folder.root / "events/extra.json");
    fs::rename(folder.root / "events/a.json", folder.root / "events/b.json");
    CHECK(ComputeModContentHash(folder.root.string()) != original);
}

TEST_CASE("mod hash: moving content between two files changes the hash") {
    TempFolder first;
    TempFolder second;
    first.Write("a", "xy");
    first.Write("b", "z");
    second.Write("a", "x");
    second.Write("b", "yz");
    CHECK(ComputeModContentHash(first.root.string())
          != ComputeModContentHash(second.root.string()));
}

TEST_CASE("mod hash: the loader stores the hash on the loaded mod") {
    TempFolder root;
    root.Write("my_mod/mod.json",
               R"({"id":"my_mod","name":"t","version":"1.2.3","api":"1"})");
    const auto loaded =
        match::modload::ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    REQUIRE(loaded.mods.size() == 1);
    CHECK(loaded.mods[0].content_hash
          == ComputeModContentHash((root.root / "my_mod").string()));
}

namespace {

struct FixtureDefs {
    match::modload::LoadResult loaded;
    match::engine::MatchRegistries registries;
};

FixtureDefs LoadFixtureDefs(const fs::path& mods_root) {
    FixtureDefs fixture;
    fixture.loaded = match::modload::ScanModsDirectory(mods_root.string());
    REQUIRE(fixture.loaded.ok());
    auto content = std::make_shared<match::engine::AssemblyContent>();
    for (const auto& mod : fixture.loaded.mods) {
        content->mods.push_back({mod.manifest.id, mod.manifest.version});
        content->kinds_by_mod.emplace_back();
    }
    std::vector<match::modload::EventDef> events;
    for (const auto& mod : fixture.loaded.mods) {
        events.insert(events.end(), mod.events.begin(), mod.events.end());
    }
    match::wiring::LoadEvents(events, content->events);
    fixture.registries.content = std::move(content);
    return fixture;
}

fs::path FixtureMods() {
    return ProjectRoot() / "tests" / "fixtures" / "golden_mod_event" / "mods";
}

}  // namespace

TEST_CASE("defs: each mod lists its version and its content hash") {
    FixtureDefs fixture = LoadFixtureDefs(FixtureMods());
    const json defs = match::view::DefsBuilder::Build(fixture.registries,
                                                      fixture.loaded.mods);
    REQUIRE(defs["mods"].size() == 1);
    CHECK(defs["mods"][0]["version"] == "2.5.0");
    CHECK(defs["mods"][0]["hash"] == fixture.loaded.mods[0].content_hash);
    CHECK(IsLowerHex64(defs["mods"][0]["hash"].get<std::string>()));
}

TEST_CASE("defs: a changed mod file changes the mod hash and the digest") {
    TempFolder root;
    root.Write("fixture_mod/mod.json",
               R"({"id":"fixture_mod","name":"t","version":"1","api":"1"})");
    root.Write("fixture_mod/notes.txt", "one");
    FixtureDefs before = LoadFixtureDefs(root.root);
    const json first = match::view::DefsBuilder::Build(before.registries,
                                                       before.loaded.mods);
    root.Write("fixture_mod/notes.txt", "two");
    FixtureDefs after = LoadFixtureDefs(root.root);
    const json second = match::view::DefsBuilder::Build(after.registries,
                                                        after.loaded.mods);
    const json again = match::view::DefsBuilder::Build(after.registries,
                                                       after.loaded.mods);
    CHECK(first["mods"][0]["hash"] != second["mods"][0]["hash"]);
    CHECK(first["defs_digest"] != second["defs_digest"]);
    CHECK(second["defs_digest"] == again["defs_digest"]);
}

TEST_CASE("defs: the events manifest lists id, retention and field types") {
    FixtureDefs fixture = LoadFixtureDefs(FixtureMods());
    const json defs = match::view::DefsBuilder::Build(fixture.registries,
                                                      fixture.loaded.mods);
    REQUIRE(defs["events"].size() == 1);
    const json& event = defs["events"][0];
    CHECK(event["id"] == "fixture_mod:apple_eaten");
    CHECK(event["retention"] == "log");
    CHECK(event["def_hash"].get<std::string>().size() == 16);
    bool lists_color_type = false;
    for (const json& field : event["fields"]) {
        CHECK_FALSE(field.contains("audience"));
        if (field["name"] == "color" && field["type"] == "color") {
            lists_color_type = true;
        }
    }
    CHECK(lists_color_type);
}

TEST_CASE("defs: the mod list and event manifest match the golden file") {
    FixtureDefs fixture = LoadFixtureDefs(FixtureMods());
    const json defs = match::view::DefsBuilder::Build(fixture.registries,
                                                      fixture.loaded.mods);
    const json actual = {{"mods", defs["mods"]},
                         {"events", defs["events"]},
                         {"defs_digest", defs["defs_digest"]}};
    const fs::path golden = ProjectRoot() / "tests" / "fixtures"
                            / "golden_mod_event" / "defs_mods.json";
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(2) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}
