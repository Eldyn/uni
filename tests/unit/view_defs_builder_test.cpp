#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/view/defs_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

/**
 * @file view_defs_builder_test.cpp
 * @brief `DefsBuilder` acceptance tests.
 *
 * Assembles the real vanilla classic match, then asserts the `defs` and
 * `match_start` packets carry the frozen mod list, the full kind table with
 * face + tags, and a stable digest that is identical across rebuilds.
 */

namespace fs = std::filesystem;
using namespace match::engine;
using namespace match::modload;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

/* INFO: locate the project root from this file, mirroring the engine tests, so
 *       the fixture is cwd-independent. */
fs::path ProjectRoot() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (fs::is_directory(p / "contract" / "schemas", ec)) return p;
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

bool LoadContent(Content& out) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    LoadResult load = ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "vanilla:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

std::unique_ptr<MatchAssembly> AssembleClassic(Content& content,
                                                std::size_t players) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (std::size_t i = 0; i < players; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::move(result.assembly);
}

const nlohmann::json* FindKindDef(const nlohmann::json& defs,
                                  const std::string& string_id) {
    for (const nlohmann::json& kind : defs["kinds"]) {
        if (kind["string_id"] == string_id) return &kind;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("view defs: builds the frozen mod and kind table") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchAssembly> assembly = AssembleClassic(content, 4);
    REQUIRE(assembly != nullptr);

    const nlohmann::json defs =
        match::view::DefsBuilder::Build(assembly->registries, content.mods);

    REQUIRE(defs["mods"].is_array());
    CHECK(defs["mods"].size() == assembly->registries.mods.size());
    CHECK(defs["mods"][0]["id"] == "vanilla");
    CHECK_FALSE(defs["mods"][0]["version"].get<std::string>().empty());
    CHECK(defs["mods"][0]["index"] == 0);

    std::size_t expected_kinds = 0;
    for (const std::vector<std::string>& kinds :
         assembly->registries.kinds_by_mod) {
        expected_kinds += kinds.size();
    }
    REQUIRE(defs["kinds"].is_array());
    CHECK(defs["kinds"].size() == expected_kinds);

    const std::optional<match::engine::KindIndex> index =
        assembly->registries.FindKind("vanilla:wild");
    REQUIRE(index.has_value());
    const nlohmann::json* wild = FindKindDef(defs, "vanilla:wild");
    REQUIRE(wild != nullptr);
    CHECK((*wild)["index"] == static_cast<int>(index->kind_index));
    CHECK((*wild)["face"]["kind"] == "text");
    CHECK((*wild)["face"]["color"] == "white");
    CHECK((*wild)["face"]["label"] == "jolly");
    CHECK((*wild)["tags"].is_array());

    const std::string digest = defs["defs_digest"].get<std::string>();
    CHECK(digest.size() == 16);
    CHECK(digest == match::view::DefsBuilder::Digest(defs));

    // INFO: defs is content-derived and must be byte-identical for every
    //       recipient, so a rebuild from the same registries matches.
    const nlohmann::json again =
        match::view::DefsBuilder::Build(assembly->registries, content.mods);
    CHECK(again == defs);
}

TEST_CASE("view defs: match_start mirrors the defs digest and mod list") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchAssembly> assembly = AssembleClassic(content, 4);
    REQUIRE(assembly != nullptr);

    const nlohmann::json defs =
        match::view::DefsBuilder::Build(assembly->registries, content.mods);
    const nlohmann::json settings = nlohmann::json{{"count_zeros", 1}};
    const nlohmann::json start = match::view::DefsBuilder::BuildMatchStart(
        assembly->registries, content.mods, "vanilla:classic", "Classic",
        settings);

    CHECK(start["defs_digest"] == defs["defs_digest"]);
    CHECK(start["mods"] == defs["mods"]);
    CHECK(start["deck_id"] == "vanilla:classic");
    CHECK(start["deck_name"] == "Classic");
    CHECK(start["settings"] == settings);
}
