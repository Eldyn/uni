#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include <match/modload/mod_loader.hpp>

#include "cardschema_test_support.hpp"

namespace {

namespace fs = std::filesystem;

using match::modload::CardDef;
using match::modload::LoadResult;
using match::modload::LoadedMod;

// INFO: walk up from this test file to the repository root, so the fixture
// mods resolve no matter where the test binary is started.
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

fs::path FixtureUni() {
    return ProjectRoot() / "tests" / "fixtures" / "card_schema" / "clean" / "uni";
}

// INFO: a scratch `mods/` root built from the shared cardschema temp dir. The
// clean mini mod is copied from the fixture folder; a test may add a second mod.
struct TempModsRoot {
    CardSchemaTempDir directory;

    const fs::path& root() const { return directory.root; }

    void Write(const std::string& relative, const std::string& text) const {
        directory.Write(relative, text);
    }

    void CopyMiniUni() const {
        fs::copy(FixtureUni(), directory.root / "uni",
                 fs::copy_options::recursive);
    }
};

const char* kManifest =
    R"({"id":"uni","name":"U","version":"1.0.0","api":"1"})";

// INFO: the numbered kind with a bad face placeholder; the loader accepts the
// file (only `identifier_format` and `title_format` are checked at load time),
// so the `unknown placeholder` error can only come from the catalog expansion.
const char* kBadFaceKind =
    R"({"format_version":"1.0.0","kind":{"identifier":"uni:numbered",)"
    R"("tags":["numbered"],"attributes":[{"attribute":"uni:color"},)"
    R"({"attribute":"uni:value"}],"identifier_format":"{color}_{value}",)"
    R"("title_format":"{color} {value}",)"
    R"("face":{"kind":"text","color":"{color}","label":"{colour}",)"
    R"("art_version":1}}})";

const LoadedMod* FindMod(const LoadResult& result, const std::string& mod_id) {
    for (const LoadedMod& mod : result.mods) {
        if (mod.manifest.id == mod_id) return &mod;
    }
    return nullptr;
}

const CardDef* FindCard(const LoadedMod& mod, const std::string& kind_id) {
    for (const CardDef& card : mod.cards) {
        if (card.kind_id == kind_id) return &card;
    }
    return nullptr;
}

bool Mentions(const std::vector<match::modload::LoadError>& errors,
              const std::string& text) {
    for (const match::modload::LoadError& error : errors) {
        if (error.message.find(text) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_SUITE("mod loader card schema") {

TEST_CASE("generates_cards_and_deck") {
    TempModsRoot mods;
    mods.CopyMiniUni();

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);
    const LoadedMod& uni = result.mods[0];
    CHECK(uni.manifest.id == "uni");

    std::vector<std::string> kind_ids;
    for (const CardDef& card : uni.cards) kind_ids.push_back(card.kind_id);
    CHECK(kind_ids == std::vector<std::string>{"uni:blue_0", "uni:blue_1",
                                               "uni:red_0", "uni:red_1",
                                               "uni:wild"});

    REQUIRE(uni.decks.size() == 1);
    CHECK(uni.decks[0].deck_id == "uni:mini");
    int total = 0;
    for (const auto& [kind_id, count] : uni.decks[0].cards) total += count;
    CHECK(total == 8);
}

TEST_CASE("card_def_fields") {
    TempModsRoot mods;
    mods.CopyMiniUni();

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());
    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);

    const CardDef* card = FindCard(result.mods[0], "uni:red_1");
    REQUIRE(card != nullptr);
    CHECK(card->face.color == "red");
    CHECK(card->face.label == "1");
    CHECK(card->tags == std::vector<std::string>{"numbered", "colored", "red"});
    CHECK(card->title == "Red 1");
    CHECK(card->raw["id"] == "red_1");
    CHECK(card->template_id == "uni:numbered");
    CHECK(card->requires_mods.empty());
}

TEST_CASE("old_cards_dir_is_an_error") {
    TempModsRoot mods;
    mods.Write("uni/mod.json", kManifest);
    mods.Write("uni/cards/legacy.json", R"({"id":"legacy"})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    CHECK(Mentions(result.errors, "removed"));
}

TEST_CASE("catalog_error_drops_the_mod") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("uni/kinds/numbered.json", kBadFaceKind);

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    CHECK(Mentions(result.errors, "unknown placeholder"));
}

TEST_CASE("foreign_member_hosts_in_kind_owner") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("magenta/mod.json",
               R"({"id":"magenta","name":"M","version":"1.0.0","api":"1"})");
    mods.Write("magenta/attributes/color/magenta.json",
               R"({"format_version":"1.0.0","attribute_member":)"
               R"({"attribute":"uni:color","identifier":"magenta:magenta",)"
               R"("tags":["colored","magenta"],"order":2}})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    REQUIRE(result.ok());
    const LoadedMod* uni = FindMod(result, "uni");
    const LoadedMod* magenta = FindMod(result, "magenta");
    REQUIRE(uni != nullptr);
    REQUIRE(magenta != nullptr);

    const CardDef* card = FindCard(*uni, "uni:magenta_0");
    REQUIRE(card != nullptr);
    CHECK(card->requires_mods == std::vector<std::string>{"magenta"});
    CHECK(magenta->cards.empty());
}

}  // TEST_SUITE("mod loader card schema")
