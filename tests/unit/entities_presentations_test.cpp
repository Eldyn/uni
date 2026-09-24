#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file entities_presentations_test.cpp
 * @brief entities/presentations ingest: loader accept/reject, duplicate ids
 *        and deterministic sorted scans.
 */

namespace fs = std::filesystem;

using match::modload::LoadError;
using match::modload::LoadResult;
using match::modload::ScanModsDirectory;

namespace {

/* INFO: isolated on-disk mods root, mirroring outcome_tables_test.cpp. */
struct TempModsRoot {
    fs::path root;
    TempModsRoot() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_entities_test_" + std::to_string(::getpid()) + "_"
                + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempModsRoot() { fs::remove_all(root); }

    fs::path Write(const std::string& folder, const std::string& name,
                   const std::string& contents) const {
        fs::path dir = root / folder;
        fs::create_directories(dir);
        fs::path file = dir / name;
        if (file.has_parent_path()) fs::create_directories(file.parent_path());
        std::ofstream out(file);
        out << contents;
        out.close();
        return file;
    }
};

const char* kManifest =
    R"({"id":"test_mod","name":"t","version":"1","api":"1"})";

void WriteMod(const TempModsRoot& root, const std::string& id,
              const std::string& manifest) {
    root.Write(id, "mod.json", manifest);
}

void WriteFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << contents;
}

const char* kDiceEntity = R"({
  "id": "dice",
  "components": {
    "visual.emoji": { "glyph": "🎲", "size": 0.4 },
    "movement.arc": { "from": "$origin", "to": "$target", "height": 2.0 },
    "lifetime.ttl": { "seconds": 3.0 }
  }
})";

const char* kLsdPresentation = R"({
  "id": "lsd",
  "target": "hand",
  "priority": 10,
  "modifiers": [ { "kind": "face.override", "source": "seeded-random-tile" } ],
  "triggers": [
    { "on": "play_rejected", "where": { "reason": "unplayable" }, "duration": 5 }
  ]
})";

bool HasCheck(const std::vector<LoadError>& errors, const std::string& check) {
    for (const auto& e : errors) {
        if (e.check == check) return true;
    }
    return false;
}

}  // namespace

// --- entities --------------------------------------------------------------

TEST_CASE("entities_presentations: accepts a well-formed entity") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/entities/dice.json", kDiceEntity);

    LoadResult result = ScanModsDirectory(root.root.string());
    REQUIRE(result.errors.empty());
    REQUIRE(result.mods.size() == 1);
    REQUIRE(result.mods[0].entities.size() == 1);
    CHECK(result.mods[0].entities[0].id == "dice");
    CHECK(result.mods[0].entities[0].namespace_id == "test_mod");
    CHECK(result.mods[0].entities[0].components.size() == 3);
}

TEST_CASE("entities_presentations: duplicate entity ids are rejected") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/entities/a.json", kDiceEntity);
    WriteFile(root.root / "test_mod/entities/b.json", kDiceEntity);

    LoadResult result = ScanModsDirectory(root.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "entity.id.duplicate"));
    CHECK(result.mods.empty());
}

TEST_CASE("entities_presentations: missing components is rejected") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/entities/dice.json", R"({ "id": "dice" })");

    LoadResult result = ScanModsDirectory(root.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "entity.components"));
    CHECK(result.mods.empty());
}

TEST_CASE("entities_presentations: entity files load in sorted order") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/entities/z_dice.json", kDiceEntity);
    WriteFile(root.root / "test_mod/entities/a_coin.json", R"({
      "id": "coin", "components": { "movement.static": {} }
    })");

    LoadResult result = ScanModsDirectory(root.root.string());
    REQUIRE(result.errors.empty());
    REQUIRE(result.mods.size() == 1);
    REQUIRE(result.mods[0].entities.size() == 2);
    /* INFO: sorted by filename, not by filesystem iteration order. */
    CHECK(result.mods[0].entities[0].id == "coin");
    CHECK(result.mods[0].entities[1].id == "dice");
}

// --- presentations ---------------------------------------------------------

TEST_CASE("entities_presentations: accepts a well-formed presentation") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/presentations/lsd.json", kLsdPresentation);

    LoadResult result = ScanModsDirectory(root.root.string());
    REQUIRE(result.errors.empty());
    REQUIRE(result.mods.size() == 1);
    REQUIRE(result.mods[0].presentations.size() == 1);
    const auto& presentation = result.mods[0].presentations[0];
    CHECK(presentation.id == "lsd");
    CHECK(presentation.namespace_id == "test_mod");
    CHECK(presentation.target == "hand");
    CHECK(presentation.priority == 10);
    REQUIRE(presentation.modifiers.size() == 1);
    CHECK(presentation.modifiers[0].kind == "face.override");
    REQUIRE(presentation.triggers.size() == 1);
    CHECK(presentation.triggers[0].on == "play_rejected");
    REQUIRE(presentation.triggers[0].duration.has_value());
    CHECK(*presentation.triggers[0].duration == 5.0);
}

TEST_CASE("entities_presentations: duplicate presentation ids are rejected") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/presentations/a.json", kLsdPresentation);
    WriteFile(root.root / "test_mod/presentations/b.json", kLsdPresentation);

    LoadResult result = ScanModsDirectory(root.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "presentation.id.duplicate"));
    CHECK(result.mods.empty());
}

TEST_CASE("entities_presentations: missing target or modifiers is rejected") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/presentations/lsd.json",
              R"({ "id": "lsd", "target": "hand" })");

    LoadResult result = ScanModsDirectory(root.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "presentation.modifiers"));
    CHECK(result.mods.empty());
}

// --- entry-set shape -------------------------------------------------------

TEST_CASE("entities_presentations: missing directories are empty sets") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);

    LoadResult result = ScanModsDirectory(root.root.string());
    REQUIRE(result.errors.empty());
    REQUIRE(result.mods.size() == 1);
    CHECK(result.mods[0].entities.empty());
    CHECK(result.mods[0].presentations.empty());
}

TEST_CASE("entities_presentations: entities/ and presentations/ are known entries") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/entities/dice.json", kDiceEntity);
    WriteFile(root.root / "test_mod/presentations/lsd.json", kLsdPresentation);

    LoadResult result = ScanModsDirectory(root.root.string());
    for (const auto& warning : result.warnings) {
        CHECK(warning.check != "mod.unknown_entry");
    }
}
