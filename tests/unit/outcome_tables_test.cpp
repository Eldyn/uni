#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file outcome_tables_test.cpp
 * @brief outcome tables: loader, `roll_table` op, accessors, wire.
 */

namespace fs = std::filesystem;

using match::ecs::BudgetLedger;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::MatchMeta;
using match::ecs::RngState;
using match::engine::MatchRegistries;
using match::modload::LoadResult;
using match::modload::LoadedMod;
using match::modload::ScanModsDirectory;
using match::modload::SemanticValidator;
using match::modload::TableDef;
using match::ops::LastOutcomeKey;
using match::ops::LastOutcomeTable;
using match::ops::LastOutcomeValue;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;
using match::ops::TableRegistry;

using nlohmann::json;

namespace {

struct TempModsRoot {
    fs::path root;
    TempModsRoot() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_outcome_test_" + std::to_string(::getpid()) + "_"
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

const char* kManifest = R"({
  "id": "test", "name": "Test", "version": "1.0.0", "api": "1"
})";

/** INFO: one table per file; `entries` sorted by key on load. */
const char* kLootTable = R"({
  "id": "loot",
  "entries": [
    { "key": "six", "weight": 1, "value": 6 },
    { "key": "none", "weight": 3, "value": 0 }
  ]
})";

/** INFO: store/bus/runtime fixture with the test table frozen into registries. */
struct Harness {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus = EventBus(std::vector<std::string>{"test"});
    ResolutionFrame frame;
    OpRuntime runtime = MakeDefaultRuntime();
    MatchRegistries registries;

    Harness() {
        TableDef loot;
        loot.id = "loot";
        loot.namespace_id = "test";
        loot.table_id = "test:loot";
        loot.entries = {
            match::modload::TableEntry{"six", 1, 6, std::nullopt, std::nullopt,
                                       json::object()},
            match::modload::TableEntry{"none", 3, 0, std::nullopt, std::nullopt,
                                       json::object()},
        };
        registries.tables["test:loot"] = loot;
    }

    OpResult Invoke(const std::string& op, json raw) {
        OpArgs args(op, std::move(raw));
        OpContext ctx(bus, ledger, frame);
        ctx.registries = &registries;
        return runtime.Invoke(op, store, args, ctx);
    }
};

Entity AddMatch(EntityStore& store, uint64_t seed, uint64_t counter = 0) {
    Entity entity = store.Create();
    store.Add(entity, MatchMeta{});
    RngState rng;
    rng.seed = seed;
    rng.op_counter = counter;
    store.Add(entity, rng);
    return entity;
}

uint64_t Counter(EntityStore& store, Entity match) {
    const RngState* rng = store.Get<RngState>(match);
    REQUIRE(rng != nullptr);
    return rng->op_counter;
}

const json* FindEvent(const OpResult& result, const std::string& type) {
    for (const json& event : result.events) {
        if (event.value("type", std::string()) == type) return &event;
    }
    return nullptr;
}

bool HasCheck(const std::vector<match::modload::LoadError>& errors,
              const std::string& check) {
    for (const auto& e : errors) {
        if (e.check == check) return true;
    }
    return false;
}

}  // namespace

// --- loader ----------------------------------------------------------------

TEST_CASE("outcome_tables: tables entry set loads and sorts by key") {
    TempModsRoot tmp;
    tmp.Write("test", "mod.json", kManifest);
    tmp.Write("test", "tables/loot.json", kLootTable);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.errors.empty());
    REQUIRE(result.mods.size() == 1);
    const LoadedMod& mod = result.mods[0];
    REQUIRE(mod.tables.size() == 1);
    CHECK(mod.tables[0].table_id == "test:loot");
    REQUIRE(mod.tables[0].entries.size() == 2);
    CHECK(mod.tables[0].entries[0].key == "none");
    CHECK(mod.tables[0].entries[1].key == "six");
    CHECK(mod.tables[0].entries[1].value == 6);
}

TEST_CASE("outcome_tables: tables/ is a known top-level entry") {
    TempModsRoot tmp;
    tmp.Write("test", "mod.json", kManifest);
    tmp.Write("test", "tables/loot.json", kLootTable);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    for (const auto& warning : result.warnings) {
        CHECK(warning.check != "mod.unknown_entry");
    }
}

TEST_CASE("outcome_tables: a malformed table entry is isolated and blocks") {
    TempModsRoot tmp;
    tmp.Write("test", "mod.json", kManifest);
    tmp.Write("test", "tables/loot.json",
              R"({ "id": "loot", "entries": [ { "key": "six" } ] })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "table.entry"));
    CHECK(result.mods.empty());
}

TEST_CASE("outcome_tables: duplicate table ids are rejected") {
    TempModsRoot tmp;
    tmp.Write("test", "mod.json", kManifest);
    tmp.Write("test", "tables/loot.json", kLootTable);
    tmp.Write("test", "tables/loot2.json", kLootTable);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "table.id.duplicate"));
}

// --- validation ------------------------------------------------------------

namespace {

/* INFO: locate contract/schemas by walking up from this test file so the
 *       suite works regardless of the ctest working directory. */
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

std::vector<match::modload::LoadError> ValidateTable(
    const std::string& table_json,
    const std::string& schema_dir = {}) {
    TempModsRoot tmp;
    tmp.Write("test", "mod.json", kManifest);
    tmp.Write("test", "tables/t.json", table_json);
    LoadResult loaded = ScanModsDirectory(tmp.root.string());
    if (loaded.mods.empty()) return loaded.errors;
    SemanticValidator validator(schema_dir.empty() ? SchemaDir() : schema_dir);
    return validator.ValidateMod(loaded.mods[0]);
}

}  // namespace

TEST_CASE("outcome_tables: schema rejects weight < 1") {
    auto errors = ValidateTable(R"({
      "id": "t", "entries": [ { "key": "a", "weight": 0, "value": 1 } ]
    })");
    CHECK(HasCheck(errors, "schema.invalid"));
}

TEST_CASE("outcome_tables: schema rejects weight above the cap") {
    auto errors = ValidateTable(R"({
      "id": "t", "entries": [ { "key": "a", "weight": 1000001, "value": 1 } ]
    })");
    CHECK(HasCheck(errors, "schema.invalid"));
}

TEST_CASE("outcome_tables: duplicate keys are a semantic error") {
    auto errors = ValidateTable(R"({
      "id": "t", "entries": [
        { "key": "a", "weight": 1 }, { "key": "a", "weight": 2 } ] })");
    CHECK(HasCheck(errors, "table.key.duplicate"));
}

TEST_CASE("outcome_tables: value and roll together are rejected") {
    auto errors = ValidateTable(R"({
      "id": "t", "entries": [
        { "key": "a", "weight": 1, "value": 2, "roll": { "sides": 6, "count": 1 } } ] })");
    CHECK(HasCheck(errors, "table.value_roll"));
}

TEST_CASE("outcome_tables: out-of-range nested roll is rejected") {
    auto errors = ValidateTable(R"({
      "id": "t", "entries": [
        { "key": "a", "weight": 1, "roll": { "sides": 0, "count": 1 } } ] })");
    CHECK(HasCheck(errors, "schema.invalid"));
}

TEST_CASE("outcome_tables: keep beyond count is a semantic error") {
    auto errors = ValidateTable(R"({
      "id": "t", "entries": [
        { "key": "a", "weight": 1,
          "roll": { "sides": 6, "count": 1, "keep": 3 } } ] })");
    CHECK(HasCheck(errors, "table.roll"));
}

TEST_CASE("outcome_tables: dangling where keyword is rejected") {
    auto errors = ValidateTable(R"({
      "id": "t", "entries": [
        { "key": "a", "weight": 1, "where": { "no_such_keyword": { "x": 1 } } } ] })");
    CHECK(HasCheck(errors, "op.unknown"));
}

TEST_CASE("outcome_tables: a roll_table ref to an unknown table is rejected") {
    TempModsRoot tmp;
    tmp.Write("test", "mod.json", kManifest);
    tmp.Write("test", "tables/loot.json", kLootTable);
    tmp.Write("test", "rules/r.json", R"(
      { "id": "r", "hooks": [ { "on": "after:play", "nodes": [
        { "id": "n1", "op": "roll_table", "args": { "table": "missing" } } ] } ] }
    )");
    LoadResult loaded = ScanModsDirectory(tmp.root.string());
    REQUIRE(loaded.mods.size() == 1);
    SemanticValidator validator(SchemaDir());
    auto errors = validator.ValidateMod(loaded.mods[0]);
    CHECK(HasCheck(errors, "ref.table"));
}

TEST_CASE("outcome_tables: a broken table blocks the mod readably") {
    TempModsRoot tmp;
    tmp.Write("test", "mod.json", kManifest);
    tmp.Write("test", "tables/loot.json", R"({
      "id": "loot", "entries": [ { "key": "six", "weight": -1 } ] })");
    LoadResult loaded = ScanModsDirectory(tmp.root.string());
    // INFO: the loader parses; the schema/semantic gate blocks the mod.
    REQUIRE(loaded.mods.size() == 1);
    SemanticValidator validator(SchemaDir());
    auto errors = validator.ValidateMod(loaded.mods[0]);
    CHECK_FALSE(errors.empty());
    CHECK(HasCheck(errors, "schema.invalid"));
}

// --- op: determinism, distribution, nested, conditions ---------------------

TEST_CASE("outcome_tables: same seed replays the same pick") {
    Harness first;
    Harness second;
    AddMatch(first.store, 0xABCDEFULL);
    AddMatch(second.store, 0xABCDEFULL);

    const OpResult a = first.Invoke("roll_table", json{{"table", "loot"}});
    const OpResult b = second.Invoke("roll_table", json{{"table", "loot"}});
    CHECK(a.status == OpStatus::kResolved);
    CHECK(a.value == b.value);
    REQUIRE(LastOutcomeKey(first.frame).has_value());
    CHECK(*LastOutcomeKey(first.frame) == *LastOutcomeKey(second.frame));
    CHECK(*LastOutcomeValue(first.frame) == *LastOutcomeValue(second.frame));
}

TEST_CASE("outcome_tables: counter increments once and seeds the stream") {
    Harness h;
    Entity match = AddMatch(h.store, 4242);
    h.Invoke("roll_table", json{{"table", "loot"}});
    CHECK(Counter(h.store, match) == 1);

    // INFO: a fresh store pre-set to counter 1 reproduces the second pick.
    Harness replay;
    Entity replay_match = AddMatch(replay.store, 4242, 1);
    const OpResult replayed =
        replay.Invoke("roll_table", json{{"table", "loot"}});
    Harness h2;
    AddMatch(h2.store, 4242);
    h2.Invoke("roll_table", json{{"table", "loot"}});
    const OpResult second = h2.Invoke("roll_table", json{{"table", "loot"}});
    CHECK(second.value == replayed.value);
    CHECK(Counter(replay.store, replay_match) == 2);
}

TEST_CASE("outcome_tables: weighted distribution favours the heavy key") {
    Harness h;
    AddMatch(h.store, 99);
    int six = 0;
    int none = 0;
    const int iterations = 400;
    for (int i = 0; i < iterations; ++i) {
        const OpResult result =
            h.Invoke("roll_table", json{{"table", "loot"}});
        REQUIRE(result.value.is_object());
        if (result.value["key"] == "test:six") {
            ++six;
        } else {
            ++none;
        }
    }
    CHECK(six + none == iterations);
    CHECK(none > six);
    CHECK(six > 0);
}

TEST_CASE("outcome_tables: a nested roll becomes the value and consumes twice") {
    Harness h;
    h.registries.tables["test:loot"].entries = {
        match::modload::TableEntry{"rare", 1,
                                   std::nullopt,
                                   json{{"sides", 6}, {"count", 2}},
                                   std::nullopt, json::object()},
    };
    Entity match = AddMatch(h.store, 7);
    const OpResult result =
        h.Invoke("roll_table", json{{"table", "loot"}});
    REQUIRE(result.value.is_object());
    CHECK(result.value["key"] == "test:rare");
    const int64_t value = result.value["value"].get<int64_t>();
    CHECK(value >= 2);
    CHECK(value <= 12);
    CHECK(Counter(h.store, match) == 2);
    const json* event = FindEvent(result, "outcome_result");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["counter"] == 1);
}

TEST_CASE("outcome_tables: ineligible entries are never picked") {
    Harness h;
    // INFO: `never` where makes the heavy entry ineligible; only `six` remains.
    h.registries.tables["test:loot"].entries = {
        match::modload::TableEntry{"six", 1, 6, std::nullopt, std::nullopt,
                                   json::object()},
        match::modload::TableEntry{"none", 99, 0, std::nullopt,
                                   json{{"never", json::object()}},
                                   json::object()},
    };
    AddMatch(h.store, 3);
    for (int i = 0; i < 20; ++i) {
        const OpResult result =
            h.Invoke("roll_table", json{{"table", "loot"}});
        CHECK(result.value["key"] == "test:six");
    }
}

TEST_CASE("outcome_tables: all-ineligible is a fail-safe no-op") {
    Harness h;
    h.registries.tables["test:loot"].entries = {
        match::modload::TableEntry{"none", 1, 0, std::nullopt,
                                   json{{"never", json::object()}},
                                   json::object()},
    };
    Entity match = AddMatch(h.store, 5);
    const OpResult result =
        h.Invoke("roll_table", json{{"table", "loot"}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value.is_null());
    CHECK(result.events.empty());
    CHECK(Counter(h.store, match) == 0);
}

TEST_CASE("outcome_tables: missing table/registries is a no-op") {
    Harness h;
    Entity match = AddMatch(h.store, 5);
    const OpResult missing =
        h.Invoke("roll_table", json{{"table", "nope"}});
    CHECK(missing.value.is_null());
    CHECK(Counter(h.store, match) == 0);

    Harness bare;
    AddMatch(bare.store, 5);
    OpArgs args("roll_table", json{{"table", "loot"}});
    OpContext ctx(bare.bus, bare.ledger, bare.frame);  // registries null
    const OpResult none = bare.runtime.Invoke("roll_table", bare.store, args,
                                              ctx);
    CHECK(none.value.is_null());
}

TEST_CASE("outcome_tables: into overrides the binding key") {
    Harness h;
    AddMatch(h.store, 11);
    const OpResult result =
        h.Invoke("roll_table", json{{"table", "loot"}, {"into", "$my_roll"}});
    CHECK(result.value.is_object());
    CHECK_FALSE(LastOutcomeKey(h.frame).has_value());
    CHECK(LastOutcomeKey(h.frame, "$my_roll").has_value());
}

// --- accessors -------------------------------------------------------------

TEST_CASE("outcome_tables: outcome_is and outcome_value read the binding") {
    Harness h;
    AddMatch(h.store, 123);
    h.registries.tables["test:loot"].entries = {
        match::modload::TableEntry{"six", 1, 6, std::nullopt, std::nullopt,
                                   json::object()},
    };
    const OpResult result =
        h.Invoke("roll_table", json{{"table", "loot"}});
    REQUIRE(result.value.is_object());

    CHECK(h.runtime.IsRegistered("roll_table"));
    // INFO: accessors evaluate through the shared registry.
    auto& conditions = match::ops::StoreDefaultConditions();
    OpContext ctx(h.bus, h.ledger, h.frame);
    CHECK(conditions.Evaluate(
        h.store, json{{"outcome_is", json{{"key", "six"}}}}, ctx));
    CHECK_FALSE(conditions.Evaluate(
        h.store, json{{"outcome_is", json{{"key", "rare"}}}}, ctx));
    CHECK(conditions.Evaluate(
        h.store,
        json{{"outcome_value", json{{"cmp", "eq"}, {"n", 6}}}}, ctx));
}

TEST_CASE("outcome_tables: accessors are false without an outcome") {
    Harness h;
    AddMatch(h.store, 1);
    auto& conditions = match::ops::StoreDefaultConditions();
    OpContext ctx(h.bus, h.ledger, h.frame);
    CHECK_FALSE(conditions.Evaluate(
        h.store, json{{"outcome_is", json{{"key", "six"}}}}, ctx));
    CHECK_FALSE(conditions.Evaluate(
        h.store,
        json{{"outcome_value", json{{"cmp", "gte"}, {"n", 1}}}}, ctx));
}

TEST_CASE("outcome_tables: LastOutcomeTable reports the full table id") {
    Harness h;
    AddMatch(h.store, 21);
    const OpResult result =
        h.Invoke("roll_table", json{{"table", "loot"}});
    REQUIRE(result.value.is_object());
    REQUIRE(match::ops::LastOutcomeTable(h.frame).has_value());
    CHECK(*match::ops::LastOutcomeTable(h.frame) == "test:loot");
}

TEST_CASE("outcome_tables: a bare ref scopes to the invoking mod") {
    Harness h;
    // INFO: two mods declare `loot`; the invoking mod's own table must win,
    //       never the lexicographically-first co-tenant.
    match::modload::TableDef other;
    other.id = "loot";
    other.namespace_id = "aaa";
    other.table_id = "aaa:loot";
    other.entries = {match::modload::TableEntry{
        "aaa_only", 1, 99, std::nullopt, std::nullopt, json::object()}};
    h.registries.tables["aaa:loot"] = other;

    const std::string mod = "test";
    AddMatch(h.store, 31);
    OpArgs args("roll_table", json{{"table", "loot"}});
    OpContext ctx(h.bus, h.ledger, h.frame);
    ctx.registries = &h.registries;
    ctx.mod_id = &mod;
    const OpResult result = h.runtime.Invoke("roll_table", h.store, args, ctx);
    REQUIRE(result.value.is_object());
    CHECK(result.value["table"] == "test:loot");
}

TEST_CASE("outcome_tables: an ambiguous bare ref with no invoking mod is a no-op") {
    Harness h;
    match::modload::TableDef other;
    other.id = "loot";
    other.namespace_id = "aaa";
    other.table_id = "aaa:loot";
    other.entries = {match::modload::TableEntry{
        "aaa_only", 1, 99, std::nullopt, std::nullopt, json::object()}};
    h.registries.tables["aaa:loot"] = other;

    Entity match = AddMatch(h.store, 32);
    OpArgs args("roll_table", json{{"table", "loot"}});
    OpContext ctx(h.bus, h.ledger, h.frame);
    ctx.registries = &h.registries;  // mod_id null
    const OpResult result = h.runtime.Invoke("roll_table", h.store, args, ctx);
    CHECK(result.value.is_null());
    CHECK(Counter(h.store, match) == 0);
}

// --- wire ------------------------------------------------------------------

#include <match/view/event_sink.hpp>

TEST_CASE("outcome_tables: projected event carries namespaced ids") {
    Harness h;
    AddMatch(h.store, 2);
    const OpResult result =
        h.Invoke("roll_table", json{{"table", "loot"}});
    const json* event = FindEvent(result, "outcome_result");
    REQUIRE(event != nullptr);
    const json& payload = (*event)["payload"];
    CHECK(payload["table"] == "test:loot");
    CHECK(payload["key"].get<std::string>().rfind("test:", 0) == 0);
    CHECK(payload["value"].is_number_integer());
    CHECK(payload["counter"] == 1);
}
