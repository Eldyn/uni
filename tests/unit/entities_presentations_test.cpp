#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/view/defs_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file entities_presentations_test.cpp
 * @brief entities/presentations ingest: loader accept/reject, duplicate ids
 *        and deterministic sorted scans, plus the
 *        `defs` packet manifests.
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
    { "on": "play_rejected", "where": { "reason_id": "unplayable" }, "duration": 5 }
  ]
})";

bool HasCheck(const std::vector<LoadError>& errors, const std::string& check) {
    for (const auto& e : errors) {
        if (e.check == check) return true;
    }
    return false;
}

/* INFO: locate contract/schemas by walking up from this test file so the suite
 *       works regardless of the ctest working directory (mirrors
 *       outcome_tables_test.cpp). */
std::string SchemaDir() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        fs::path cand = p / "contract" / "schemas";
        if (fs::is_directory(cand, ec)) return cand.string();
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

std::vector<LoadError> ValidateModFrom(const TempModsRoot& root) {
    LoadResult loaded = ScanModsDirectory(root.root.string());
    if (loaded.mods.empty()) return loaded.errors;
    match::modload::SemanticValidator validator(SchemaDir());
    return validator.ValidateMod(loaded.mods[0]);
}

std::vector<LoadError> ValidateEntity(const std::string& entity_json) {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/entities/e.json", entity_json);
    return ValidateModFrom(root);
}

std::vector<LoadError> ValidatePresentation(
    const std::string& presentation_json) {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/presentations/p.json", presentation_json);
    return ValidateModFrom(root);
}

/* INFO: locate the repo root from this test file so the shipped `mods/` tree
 *       can be assembled cwd-independently (mirrors view_defs_builder_test). */
std::filesystem::path ProjectRoot() {
    std::filesystem::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(p / "mods" / "test", ec)) return p;
        std::filesystem::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

/* INFO: the `defs` manifest entry with a given `id` in array `key`. */
const nlohmann::json* FindManifest(const nlohmann::json& defs,
                                   const char* key,
                                   const std::string& id) {
    for (const nlohmann::json& entry : defs[key]) {
        if (entry.value("id", std::string()) == id) return &entry;
    }
    return nullptr;
}

/* INFO: the first event of `type` an op produced (mirrors outcome_tables_test). */
const nlohmann::json* FindEvent(const match::ops::OpResult& result,
                                const std::string& type) {
    for (const nlohmann::json& event : result.events) {
        if (event.value("type", std::string()) == type) return &event;
    }
    return nullptr;
}

/* INFO: the shipped `rules/dice_roll.json` acceptance chain, driven through the
 *       real `roll_table` op against the assembled match registries. */
struct DiceHarness {
    match::ecs::EntityStore store;
    match::ecs::BudgetLedger ledger;
    match::ecs::EventBus bus = match::ecs::EventBus(std::vector<std::string>{"test"});
    match::ops::ResolutionFrame frame;
    match::ops::OpRuntime runtime = match::ops::MakeDefaultRuntime();
    match::engine::MatchRegistries* registries = nullptr;

    DiceHarness() {
        match::ecs::Entity match = store.Create();
        store.Add(match, match::ecs::MatchMeta{});
        match::ecs::RngState rng;
        rng.seed = 42;
        rng.op_counter = 0;
        store.Add(match, rng);
    }

    match::ops::OpResult Roll(const std::string& table) {
        match::ops::OpArgs args("roll_table", nlohmann::json{{"table", table}});
        match::ops::OpContext ctx(bus, ledger, frame);
        ctx.registries = registries;
        return runtime.Invoke("roll_table", store, args, ctx);
    }
};

/* INFO: the assembled match registries for the shipped `mods/` tree (the deck
 *       selects the `test` mod, whose `entities/dice.json` carries the
 *       outcome.resolver under test). */
match::engine::AssemblyResult AssembleShippedMods(const fs::path& root) {
    LoadResult loaded = ScanModsDirectory((root / "mods").string());
    REQUIRE(loaded.ok());

    match::modload::DeckDef deck;
    for (const auto& mod : loaded.mods) {
        for (const auto& candidate : mod.decks) {
            if (candidate.deck_id == "test:classic_bombs") deck = candidate;
        }
    }
    REQUIRE_FALSE(deck.deck_id.empty());

    match::engine::MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 2; ++i) {
        match::engine::MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    return match::engine::MatchAssembler::Assemble(loaded.mods, deck, options);
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

// --- semantic validation: entities -----------------------------------

TEST_CASE("entities_presentations: rejects two components of one family") {
    auto errors = ValidateEntity(R"({
      "id": "dice",
      "components": {
        "movement.arc": { "from": "a", "to": "b" },
        "movement.linear": { "from": "a", "to": "b" }
      }
    })");
    CHECK(HasCheck(errors, "entity.component.family"));
}

TEST_CASE("entities_presentations: allows distinct component families") {
    auto errors = ValidateEntity(R"({
      "id": "dice",
      "components": {
        "visual.emoji": { "glyph": "🎲" },
        "movement.arc": { "from": "a", "to": "b" },
        "motion.spin": { "axis": "y", "turns": 3, "hz": 1.0 }
      }
    })");
    CHECK_FALSE(HasCheck(errors, "entity.component.family"));
}

TEST_CASE("entities_presentations: unknown component name is an error") {
    auto errors = ValidateEntity(R"({
      "id": "dice",
      "components": { "visual.bogus": {} }
    })");
    CHECK(HasCheck(errors, "entity.component.unknown"));
}

TEST_CASE("entities_presentations: empty components object is an error") {
    auto errors = ValidateEntity(R"({ "id": "dice", "components": {} })");
    CHECK(HasCheck(errors, "entity.components.empty"));
}

// --- semantic validation: presentations ------------------------------------

TEST_CASE("entities_presentations: unknown modifier kind is an error") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "hand",
      "modifiers": [ { "kind": "face.bogus" } ]
    })");
    CHECK(HasCheck(errors, "presentation.kind.unknown"));
}

TEST_CASE("entities_presentations: a stray mode field is rejected") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "hand",
      "modifiers": [ { "kind": "face.override", "mode": "override" } ]
    })");
    CHECK(HasCheck(errors, "presentation.mode.forbidden"));
}

TEST_CASE("entities_presentations: malformed target grammar is an error") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "bogus",
      "modifiers": [ { "kind": "face.override" } ]
    })");
    CHECK(HasCheck(errors, "presentation.target"));
}

TEST_CASE("entities_presentations: empty modifiers array is an error") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "hand", "modifiers": []
    })");
    CHECK(HasCheck(errors, "presentation.modifiers.empty"));
}

TEST_CASE("entities_presentations: unknown trigger source is an error") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "hand",
      "modifiers": [ { "kind": "face.override" } ],
      "triggers": [ { "on": "bogus_event" } ]
    })");
    CHECK(HasCheck(errors, "presentation.trigger.source"));
}

TEST_CASE("entities_presentations: unknown trigger where field is an error") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "hand",
      "modifiers": [ { "kind": "face.override" } ],
      "triggers": [ { "on": "card_played", "where": { "nope": 1 } } ]
    })");
    CHECK(HasCheck(errors, "presentation.trigger.where"));
}

TEST_CASE("entities_presentations: known event source and payload pass") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "hand",
      "modifiers": [ { "kind": "face.override" } ],
      "triggers": [ { "on": "card_played", "where": { "player": "x" } } ]
    })");
    CHECK_FALSE(HasCheck(errors, "presentation.trigger.source"));
    CHECK_FALSE(HasCheck(errors, "presentation.trigger.where"));
}

TEST_CASE("entities_presentations: play_rejected reason_id is accepted") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "hand",
      "modifiers": [ { "kind": "face.override" } ],
      "triggers": [ { "on": "play_rejected",
                      "where": { "reason_id": "unplayable" } } ]
    })");
    CHECK_FALSE(HasCheck(errors, "presentation.trigger.source"));
    CHECK_FALSE(HasCheck(errors, "presentation.trigger.where"));
}

TEST_CASE("entities_presentations: play_rejected phantom reason is rejected") {
    auto errors = ValidatePresentation(R"({
      "id": "p", "target": "hand",
      "modifiers": [ { "kind": "face.override" } ],
      "triggers": [ { "on": "play_rejected",
                      "where": { "reason": "unplayable" } } ]
    })");
    CHECK(HasCheck(errors, "presentation.trigger.where"));
}

// --- semantic validation: outcome resolver -----------------------------

TEST_CASE("entities_presentations: requires a resolver for every table key") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/tables/loot.json", R"({
      "id": "loot", "entries": [ { "key": "six", "weight": 1, "value": 6 } ]
    })");
    WriteFile(root.root / "test_mod/entities/dice.json", R"({
      "id": "dice",
      "components": { "outcome.resolver": { "test_mod:other": {} } }
    })");

    auto errors = ValidateModFrom(root);
    CHECK(HasCheck(errors, "outcome.resolver.missing"));
}

TEST_CASE("entities_presentations: exact key and star catch-all pass") {
    {
        TempModsRoot root;
        WriteMod(root, "test_mod", kManifest);
        WriteFile(root.root / "test_mod/tables/loot.json", R"({
          "id": "loot", "entries": [ { "key": "six", "weight": 1, "value": 6 } ]
        })");
        WriteFile(root.root / "test_mod/entities/dice.json", R"({
          "id": "dice",
          "components": { "outcome.resolver": { "test_mod:six": {} } }
        })");
        auto errors = ValidateModFrom(root);
        CHECK_FALSE(HasCheck(errors, "outcome.resolver.missing"));
    }
    {
        TempModsRoot root;
        WriteMod(root, "test_mod", kManifest);
        WriteFile(root.root / "test_mod/tables/loot.json", R"({
          "id": "loot", "entries": [ { "key": "six", "weight": 1, "value": 6 } ]
        })");
        WriteFile(root.root / "test_mod/entities/dice.json", R"({
          "id": "dice",
          "components": { "outcome.resolver": { "*": {} } }
        })");
        auto errors = ValidateModFrom(root);
        CHECK_FALSE(HasCheck(errors, "outcome.resolver.missing"));
    }
}

// --- acceptance: dice_roll -> outcome_result -> outcome.resolver --------

TEST_CASE("entities_presentations: dice_roll binds the loot outcome and the dice resolver covers it") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());

    LoadResult loaded = ScanModsDirectory((root / "mods").string());
    REQUIRE(loaded.ok());

    // INFO: the shipped chain exists and rolls the loot table (the key binding
    //       and outcome_result emission are the roll_table op's job).
    const match::modload::RuleDef* dice_roll = nullptr;
    for (const auto& mod : loaded.mods) {
        for (const auto& rule : mod.rules) {
            if (rule.rule_id == "test:dice_roll") dice_roll = &rule;
        }
    }
    REQUIRE(dice_roll != nullptr);
    const nlohmann::json* roll_node = nullptr;
    for (const auto& hook : dice_roll->hooks) {
        for (const auto& node : hook.graph.nodes) {
            if (node.value("op", std::string()) == "roll_table") roll_node = &node;
        }
    }
    REQUIRE(roll_node != nullptr);
    CHECK((*roll_node)["args"]["table"] == "loot");

    match::engine::AssemblyResult assembled = AssembleShippedMods(root);
    REQUIRE(assembled.ok());
    REQUIRE(assembled.assembly != nullptr);
    match::engine::MatchRegistries& registries = assembled.assembly->registries;

    // INFO: force the loot table to its `six` entry so the emitted key is
    //       deterministic, then drive the roll through the real op.
    auto loot = registries.tables.find("test:loot");
    REQUIRE(loot != registries.tables.end());
    loot->second.entries = {match::modload::TableEntry{
        "six", 1, 6, std::nullopt, std::nullopt, nlohmann::json::object()}};

    DiceHarness harness;
    harness.registries = &registries;
    const match::ops::OpResult result = harness.Roll("loot");
    REQUIRE(result.value.is_object());
    CHECK(result.value["key"] == "test:six");

    const nlohmann::json* event = FindEvent(result, "outcome_rolled");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["key"] == "test:six");

    // INFO: acceptance end-to-end - the key the chain emits is covered by
    //       the dice entity's outcome.resolver payload on the wire.
    auto dice = registries.entities.find("test:dice");
    REQUIRE(dice != registries.entities.end());
    const nlohmann::json& components = dice->second.raw.at("components");
    REQUIRE(components.contains("outcome.resolver"));
    const nlohmann::json& resolver = components["outcome.resolver"];
    REQUIRE(resolver.is_object());
    CHECK((resolver.contains("test:six") || resolver.contains("*")));
}

// --- semantic validation: asset dependencies -------------------------------

TEST_CASE("entities_presentations: an unresolved sound clip is an error") {
    auto errors = ValidateEntity(R"({
      "id": "dice",
      "components": { "audio.sound": { "clip": "nope" } }
    })");
    CHECK(HasCheck(errors, "asset.dep.missing"));
}

TEST_CASE("entities_presentations: a declared sound clip resolves") {
    TempModsRoot root;
    WriteMod(root, "test_mod", kManifest);
    WriteFile(root.root / "test_mod/assets/sfx/index.json", R"({
      "id": "sfx", "license": "CC0-1.0", "author": "t",
      "slots": { "sound": [ { "tier": "low", "value": "roll" } ] }
    })");
    WriteFile(root.root / "test_mod/entities/dice.json", R"({
      "id": "dice",
      "components": { "audio.sound": { "clip": "roll" } }
    })");

    auto errors = ValidateModFrom(root);
    CHECK_FALSE(HasCheck(errors, "asset.dep.missing"));
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

// --- defs packet manifests --------------------------------------

TEST_CASE("entities_presentations: defs publishes entity and presentation manifests") {
    const std::filesystem::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    LoadResult loaded = ScanModsDirectory((root / "mods").string());
    REQUIRE(loaded.ok());

    match::modload::DeckDef deck;
    bool found = false;
    for (const auto& mod : loaded.mods) {
        for (const auto& candidate : mod.decks) {
            if (candidate.deck_id == "test:classic_bombs") {
                deck = candidate;
                found = true;
            }
        }
    }
    REQUIRE(found);

    match::engine::MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 2; ++i) {
        match::engine::MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    match::engine::AssemblyResult assembled =
        match::engine::MatchAssembler::Assemble(loaded.mods, deck, options);
    REQUIRE(assembled.ok());
    REQUIRE(assembled.assembly != nullptr);

    const nlohmann::json defs = match::view::DefsBuilder::Build(
        assembled.assembly->registries, loaded.mods);

    // INFO: entities/presentations are client-consumed, so the defs
    //       packet must publish them.
    REQUIRE(defs.contains("entities"));
    REQUIRE(defs["entities"].is_array());
    const nlohmann::json* dice = FindManifest(defs, "entities", "test:dice");
    REQUIRE(dice != nullptr);
    CHECK((*dice)["def_hash"].is_string());
    CHECK((*dice)["def_hash"].get<std::string>().size() == 16);
    REQUIRE((*dice)["components"].is_object());

    REQUIRE(defs.contains("presentations"));
    REQUIRE(defs["presentations"].is_array());
    const nlohmann::json* lsd =
        FindManifest(defs, "presentations", "test:lsd");
    REQUIRE(lsd != nullptr);
    CHECK((*lsd)["def_hash"].is_string());
    CHECK((*lsd)["def_hash"].get<std::string>().size() == 16);
    CHECK((*lsd)["target"] == "hand");
    REQUIRE((*lsd)["modifiers"].is_array());
}

TEST_CASE("entities_presentations: manifest def_hash tracks the def content") {
    const std::filesystem::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    LoadResult loaded = ScanModsDirectory((root / "mods").string());
    REQUIRE(loaded.ok());

    match::modload::DeckDef deck;
    for (const auto& mod : loaded.mods) {
        for (const auto& candidate : mod.decks) {
            if (candidate.deck_id == "test:classic_bombs") deck = candidate;
        }
    }
    REQUIRE_FALSE(deck.deck_id.empty());

    match::engine::MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 2; ++i) {
        match::engine::MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    match::engine::AssemblyResult assembled =
        match::engine::MatchAssembler::Assemble(loaded.mods, deck, options);
    REQUIRE(assembled.assembly != nullptr);
    match::engine::MatchRegistries& registries =
        assembled.assembly->registries;

    const nlohmann::json first =
        match::view::DefsBuilder::Build(registries, loaded.mods);
    const nlohmann::json* dice = FindManifest(first, "entities", "test:dice");
    REQUIRE(dice != nullptr);
    const std::string hash = (*dice)["def_hash"].get<std::string>();

    // INFO: rebuilding from the same frozen registries is deterministic.
    const nlohmann::json second =
        match::view::DefsBuilder::Build(registries, loaded.mods);
    REQUIRE(FindManifest(second, "entities", "test:dice") != nullptr);
    CHECK(FindManifest(second, "entities", "test:dice")->at("def_hash")
          == hash);

    // INFO: changing the def's canonical JSON changes its content hash, so the
    //       client reloads exactly that def.
    auto it = registries.entities.find("test:dice");
    REQUIRE(it != registries.entities.end());
    it->second.raw["components"]["visual.emoji"]["size"] = 0.9;
    const nlohmann::json third =
        match::view::DefsBuilder::Build(registries, loaded.mods);
    REQUIRE(FindManifest(third, "entities", "test:dice") != nullptr);
    CHECK(FindManifest(third, "entities", "test:dice")->at("def_hash") != hash);
}
