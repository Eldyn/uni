#include <doctest/doctest.h>
#include <match/modload/mod_loader.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace match::modload;

namespace {

/** INFO: scratch directory under the system temp root, removed by the dtor. */
struct TempModsRoot {
    fs::path root;

    TempModsRoot() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_modloader_test_" + std::to_string(::getpid()) + "_"
                + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root);
    }

    ~TempModsRoot() { fs::remove_all(root); }

    fs::path Write(const std::string& folder,
                   const std::string& name,
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

    /** INFO: Entry sets are directories, one definition per file. Split a
     *       JSON array fixture into `<subdir>/0.json`, `1.json`, ... so tests
     *       can keep array-shaped fixtures; zero-padded indices preserve the
     *       array's order under the loader's sorted-filename scan. */
    void WriteEntrySet(const std::string& folder,
                       const std::string& subdir,
                       const std::string& array_json) const {
        nlohmann::json arr = nlohmann::json::parse(array_json);
        int index = 0;
        for (const auto& item : arr) {
            Write(folder, subdir + "/" + std::to_string(index++) + ".json",
                  item.dump());
        }
    }
};

const char* kValidManifest = R"({
  "id": "vanilla",
  "name": "UNI Vanilla",
  "version": "1.0.0",
  "api": "1",
  "description": "The standard UNI card set.",
  "author": "eldyn"
})";

const char* kMinimalManifest = R"({
  "id": "minimal",
  "name": "Minimal",
  "version": "0.0.1",
  "api": "1"
})";

const char* kValidCards = R"([
  {
    "id": "plus4",
    "title": "+4",
    "face": { "kind": "text", "color": "white", "label": "+4", "art_version": 2 },
    "tags": ["draw_penalty", "stackable"],
    "behavior": {
      "on_play": {
        "nodes": [
          { "id": "n1", "op": "draw_cards", "args": { "target": "@next_player", "n": 4 }, "next": "n2" },
          { "id": "n2", "op": "advance_turn", "args": {} }
        ]
      }
    },
    "window": {
      "when_played": {
        "responders": "@others",
        "respond_with": { "any_tag": ["stackable"] },
        "duration": "env",
        "on_response": "append_to_stack",
        "default": "resolve_stack"
      }
    }
  },
  { "id": "red_0", "title": "Red 0", "face": { "kind": "text", "color": "red", "label": "0" }, "tags": [] }
])";

const char* kValidRules = R"([
  {
    "id": "draw_stacking",
    "title": "Draw Stacking",
    "description": "+2/+4 stack.",
    "hooks": [
      { "on": "before:play", "where": { "card_has_tag": "stackable" },
        "nodes": [ { "id": "n1", "op": "advance_turn", "args": {} } ] }
    ]
  }
])";

const char* kValidStatuses = R"([
  { "id": "shielded", "title": "Shielded", "stack_policy": "replace", "hidden": false }
])";

const char* kValidMutations = R"([
  { "id": "nerf_plus4", "target": "vanilla:plus4", "mode": "replace",
    "replacement": { "nodes": [ { "id": "n1", "op": "advance_turn", "args": {} } ] } }
])";

const char* kValidDeck = R"({
  "id": "classic",
  "name": "Classic",
  "namespace": "vanilla",
  "mods": ["vanilla"],
  "cards": { "vanilla:red_0": 2, "vanilla:plus4": 4 },
  "settings": { "match": { "score_limit": 500 } }
})";

bool HasCheck(const std::vector<LoadError>& errors, const std::string& check) {
    for (const auto& e : errors) {
        if (e.check == check) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("modloader: valid mod folder loads all artifacts") {
    TempModsRoot tmp;
    tmp.Write("vanilla", "mod.json", kValidManifest);
    tmp.WriteEntrySet("vanilla", "cards", kValidCards);
    tmp.WriteEntrySet("vanilla", "rules", kValidRules);
    tmp.WriteEntrySet("vanilla", "statuses", kValidStatuses);
    tmp.WriteEntrySet("vanilla", "mutations", kValidMutations);
    tmp.Write("vanilla", "decks/classic.json", kValidDeck);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.errors.empty());
    REQUIRE(result.mods.size() == 1);

    const LoadedMod& mod = result.mods[0];
    CHECK(mod.manifest.id == "vanilla");
    CHECK(mod.manifest.name == "UNI Vanilla");
    CHECK(mod.manifest.api == "1");

    REQUIRE(mod.cards.size() == 2);
    CHECK(mod.cards[0].id == "plus4");
    CHECK(mod.cards[0].kind_id == "vanilla:plus4");
    CHECK(mod.cards[0].face.kind == FaceKind::kText);
    CHECK(mod.cards[0].face.color == "white");
    CHECK(mod.cards[0].face.art_version == 2);
    REQUIRE(mod.cards[0].tags.size() == 2);
    CHECK(mod.cards[0].tags[1] == "stackable");
    REQUIRE(mod.cards[0].behaviors.size() == 1);
    CHECK(mod.cards[0].behaviors[0].hook == "on_play");
    REQUIRE(mod.cards[0].behaviors[0].graph.nodes.size() == 2);
    REQUIRE(mod.cards[0].window.has_value());
    CHECK(mod.cards[0].window->responders == "@others");
    CHECK(mod.cards[0].window->default_route == "resolve_stack");

    REQUIRE(mod.rules.size() == 1);
    CHECK(mod.rules[0].rule_id == "vanilla:draw_stacking");
    REQUIRE(mod.rules[0].hooks.size() == 1);
    CHECK(mod.rules[0].hooks[0].hook == "before:play");
    CHECK(mod.rules[0].hooks[0].where.has_value());

    REQUIRE(mod.statuses.size() == 1);
    CHECK(mod.statuses[0].status_id == "vanilla:shielded");
    CHECK(mod.statuses[0].stack_policy == "replace");

    REQUIRE(mod.mutations.size() == 1);
    CHECK(mod.mutations[0].mode == "replace");
    CHECK(mod.mutations[0].target == "vanilla:plus4");

    REQUIRE(mod.decks.size() == 1);
    CHECK(mod.decks[0].deck_id == "vanilla:classic");
    REQUIRE(mod.decks[0].cards.size() == 2);
    CHECK(mod.decks[0].settings.contains("match"));
}

TEST_CASE("modloader: missing manifest yields structured error") {
    TempModsRoot tmp;
    tmp.Write("orphan", "cards/c1.json",
              R"({ "id": "c1", "face": { "kind": "blank" } })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "file.read");
    CHECK(result.errors[0].artifact == "mod");
    CHECK(result.errors[0].path.find("mod.json") != std::string::npos);
    CHECK_FALSE(result.errors[0].message.empty());
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: malformed JSON yields parse error") {
    TempModsRoot tmp;
    tmp.Write("vanilla", "mod.json", "{ this is not json ");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "json.parse");
    CHECK(result.errors[0].artifact == "mod");
}

TEST_CASE("modloader: bad id syntax is rejected") {
    TempModsRoot tmp;
    tmp.Write("bad", "mod.json", R"({
      "id": "Bad-ID", "name": "x", "version": "1.0.0", "api": "1"
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "manifest.id");
    CHECK(result.errors[0].message.find("Bad-ID") != std::string::npos);
}

TEST_CASE("modloader: id longer than 32 chars is rejected") {
    TempModsRoot tmp;
    tmp.Write("bad", "mod.json", R"({
      "id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "name": "x",
      "version": "1.0.0", "api": "1"
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "manifest.id");
}

TEST_CASE("modloader: duplicate mod ids across folders are rejected") {
    TempModsRoot tmp;
    tmp.Write("one", "mod.json", kMinimalManifest);
    tmp.Write("two", "mod.json", kMinimalManifest);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "mod.id.duplicate"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: api greater than engine version is rejected") {
    TempModsRoot tmp;
    tmp.Write("future", "mod.json", R"({
      "id": "future", "name": "Future", "version": "9.0.0", "api": "999"
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "manifest.api");
    CHECK(result.errors[0].message.find("999") != std::string::npos);
}

TEST_CASE("modloader: api equal to engine version is accepted") {
    TempModsRoot tmp;
    tmp.Write("ok", "mod.json", R"({
      "id": "ok", "name": "Ok", "version": "1.0.0", "api": "1"
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK(result.ok());
}

TEST_CASE("modloader: absent entry-set directories are fine") {
    TempModsRoot tmp;
    tmp.Write("minimal", "mod.json", kMinimalManifest);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);
    CHECK(result.mods[0].cards.empty());
    CHECK(result.mods[0].rules.empty());
    CHECK(result.mods[0].statuses.empty());
    CHECK(result.mods[0].mutations.empty());
    CHECK(result.mods[0].decks.empty());
    CHECK(result.mods[0].assets.empty());
}

TEST_CASE("modloader: malformed entry file is a structured error") {
    TempModsRoot tmp;
    tmp.Write("bad", "mod.json", kMinimalManifest);
    tmp.Write("bad", "cards/broken.json", "{ not json ");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "json.parse"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: an old-style cards array file is rejected") {
    TempModsRoot tmp;
    tmp.Write("old", "mod.json", kMinimalManifest);
    /* INFO: The loader retires the single-file array form; a cards entry must be one
     *       card object, so an array is a shape error, not a silent load. */
    tmp.Write("old", "cards/0.json",
              R"([ { "id": "c1", "face": { "kind": "blank" } } ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "card.shape"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: oversized mod.json is rejected before parsing") {
    TempModsRoot tmp;
    fs::path dir = tmp.root / "huge";
    fs::create_directories(dir);
    /* INFO: sparse file just over the loader's 16 MiB ceiling; seek then write
     *       one byte so the logical size is 16 MiB + 1. */
    std::ofstream out(dir / "mod.json", std::ios::binary);
    out.seekp(16 * 1024 * 1024);
    out << 'x';
    out.close();

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "file.too_large"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: deeply nested JSON is rejected") {
    TempModsRoot tmp;
    std::string nested(200, '[');
    nested += std::string(200, ']');
    tmp.Write("deep", "mod.json", nested);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "json.depth"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: duplicate card ids within a mod are rejected") {
    TempModsRoot tmp;
    tmp.Write("dup", "mod.json", R"({
      "id": "dup", "name": "Dup", "version": "1.0.0", "api": "1"
    })");
    tmp.WriteEntrySet("dup", "cards", R"([
      { "id": "same", "face": { "kind": "blank" } },
      { "id": "same", "face": { "kind": "blank" } }
    ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK(HasCheck(result.errors, "card.id.duplicate"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: unknown face kind is rejected") {
    TempModsRoot tmp;
    tmp.Write("faces", "mod.json", R"({
      "id": "faces", "name": "Faces", "version": "1.0.0", "api": "1"
    })");
    tmp.WriteEntrySet("faces", "cards", R"([
      { "id": "weird", "face": { "kind": "hologram" } }
    ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK(HasCheck(result.errors, "card.face"));
}

TEST_CASE("modloader: card auto_trigger loads with must_apply") {
    TempModsRoot tmp;
    tmp.Write("totem", "mod.json", R"({
      "id": "totem", "name": "Totem", "version": "1.0.0", "api": "1"
    })");
    tmp.WriteEntrySet("totem", "cards", R"([
      { "id": "totem", "face": { "kind": "blank" },
        "auto_trigger": {
          "condition": { "always": null },
          "graph": { "nodes": [
            { "id": "n1", "op": "declare_winner",
              "args": { "player": "@self", "kind": "special" } }
          ] },
          "must_apply": true
        } }
    ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);
    REQUIRE(result.mods[0].cards.size() == 1);
    const CardDef& card = result.mods[0].cards[0];
    REQUIRE(card.auto_trigger.has_value());
    CHECK(card.auto_trigger->must_apply);
    CHECK(card.auto_trigger->condition.contains("always"));
    REQUIRE(card.auto_trigger->graph.contains("nodes"));
    CHECK(card.auto_trigger->graph["nodes"].size() == 1);
}

TEST_CASE("modloader: auto_trigger must_apply defaults to false") {
    TempModsRoot tmp;
    tmp.Write("opt", "mod.json", R"({
      "id": "opt", "name": "Opt", "version": "1.0.0", "api": "1"
    })");
    tmp.WriteEntrySet("opt", "cards", R"([
      { "id": "c1", "face": { "kind": "blank" },
        "auto_trigger": {
          "condition": { "always": null },
          "graph": { "nodes": [
            { "id": "n1", "op": "advance_turn", "args": {} }
          ] }
        } }
    ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.ok());
    REQUIRE(result.mods[0].cards[0].auto_trigger.has_value());
    CHECK_FALSE(result.mods[0].cards[0].auto_trigger->must_apply);
}

TEST_CASE("modloader: non-object auto_trigger condition is rejected") {
    TempModsRoot tmp;
    tmp.Write("bad", "mod.json", R"({
      "id": "bad", "name": "Bad", "version": "1.0.0", "api": "1"
    })");
    tmp.WriteEntrySet("bad", "cards", R"([
      { "id": "c1", "face": { "kind": "blank" },
        "auto_trigger": { "condition": 7, "graph": { "nodes": [] } } }
    ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "card.auto_trigger"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: auto_trigger graph without nodes is rejected") {
    TempModsRoot tmp;
    tmp.Write("bad", "mod.json", R"({
      "id": "bad", "name": "Bad", "version": "1.0.0", "api": "1"
    })");
    tmp.WriteEntrySet("bad", "cards", R"([
      { "id": "c1", "face": { "kind": "blank" },
        "auto_trigger": { "condition": { "always": null },
                          "graph": { "foo": 1 } } }
    ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "card.auto_trigger"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: non-bool auto_trigger must_apply is rejected") {
    TempModsRoot tmp;
    tmp.Write("bad", "mod.json", R"({
      "id": "bad", "name": "Bad", "version": "1.0.0", "api": "1"
    })");
    tmp.WriteEntrySet("bad", "cards", R"([
      { "id": "c1", "face": { "kind": "blank" },
        "auto_trigger": { "condition": { "always": null },
                          "graph": { "nodes": [] },
                          "must_apply": "yes" } }
    ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "card.auto_trigger"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: rules and statuses load from their entry sets") {
    TempModsRoot tmp;
    tmp.Write("arr", "mod.json", kMinimalManifest);
    tmp.WriteEntrySet("arr", "rules", R"([
      { "id": "r1", "hooks": [] },
      { "id": "r2", "hooks": [ { "on": "after:play", "nodes": [] } ] }
    ])");
    tmp.WriteEntrySet("arr", "statuses", R"([
      { "id": "s1", "stack_policy": "cap:3" }
    ])");
    LoadResult arr = ScanModsDirectory(tmp.root.string());
    REQUIRE(arr.ok());
    REQUIRE(arr.mods[0].rules.size() == 2);
    CHECK(arr.mods[0].rules[1].hooks[0].hook == "after:play");
    REQUIRE(arr.mods[0].statuses.size() == 1);
    CHECK(arr.mods[0].statuses[0].stack_policy == "cap:3");
}

TEST_CASE("modloader: rule hook accepts the nested graph form") {
    TempModsRoot tmp;
    tmp.Write("g", "mod.json", kMinimalManifest);
    tmp.Write("g", "rules/r1.json", R"({
      "id": "r1", "hooks": [
        { "on": "before:play", "phase": "before",
          "graph": { "nodes": [
            { "id": "n1", "op": "advance_turn", "args": {} }
          ] } }
      ]
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.ok());
    REQUIRE(result.mods[0].rules.size() == 1);
    REQUIRE(result.mods[0].rules[0].hooks.size() == 1);
    const BehaviorEntry& hook = result.mods[0].rules[0].hooks[0];
    CHECK(hook.hook == "before:play");
    REQUIRE(hook.phase.has_value());
    CHECK(*hook.phase == "before");
    REQUIRE(hook.graph.nodes.size() == 1);
    CHECK(hook.graph.nodes[0]["op"] == "advance_turn");
}

TEST_CASE("modloader: a non-object rule hook is a structured error") {
    TempModsRoot tmp;
    tmp.Write("bad", "mod.json", kMinimalManifest);
    tmp.Write("bad", "rules/r1.json", R"({
      "id": "r1", "hooks": [ "not-an-object" ]
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "rules.hook"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: a rule hook missing 'on' is a structured error") {
    TempModsRoot tmp;
    tmp.Write("noon", "mod.json", kMinimalManifest);
    tmp.Write("noon", "rules/r1.json", R"({
      "id": "r1", "hooks": [ { "nodes": [] } ]
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "rules.hook"));
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: pre-existing errors do not poison ParseModFolder") {
    TempModsRoot tmp;
    tmp.Write("good", "mod.json", R"({
      "id": "good", "name": "Good", "version": "1.0.0", "api": "1"
    })");
    tmp.WriteEntrySet("good", "cards", R"([
      { "id": "c1", "face": { "kind": "blank" } }
    ])");

    /* INFO: ParseModFolder is public; its contract is "true when the folder
     *       loaded without errors" for the folder alone, regardless of any
     *       errors the caller already collected. */
    std::vector<LoadError> errors;
    errors.push_back(LoadError{"earlier", "mod", "elsewhere", "prior failure"});

    LoadedMod mod;
    bool loaded = ParseModFolder((tmp.root / "good").string(), "good",
                                 mod, errors);
    CHECK(loaded);
    CHECK(mod.manifest.id == "good");
    REQUIRE(mod.cards.size() == 1);
    /* INFO: the caller's prior error must be preserved, not cleared. */
    CHECK(errors.size() == 1);
    CHECK(errors[0].check == "earlier");
}

TEST_CASE("modloader: a valid folder after a failing folder still loads") {
    TempModsRoot tmp;
    /* INFO: "bad" sorts before "good"; its manifest errors must not leak into
     *       the shared error vector and poison the valid folder's parse. */
    tmp.Write("bad", "mod.json", R"({
      "id": "bad", "name": "x", "version": "1.0.0"
    })");
    tmp.Write("good", "mod.json", R"({
      "id": "good", "name": "Good", "version": "1.0.0", "api": "1"
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK_FALSE(result.ok());
    CHECK(HasCheck(result.errors, "manifest.required"));
    /* INFO: the scan is all-or-nothing, so no partial registry is returned. */
    CHECK(result.mods.empty());
}

TEST_CASE("modloader: mods are returned sorted by manifest id") {
    TempModsRoot tmp;
    tmp.Write("zulu", "mod.json", R"({
      "id": "zulu", "name": "Z", "version": "1.0.0", "api": "1"
    })");
    tmp.Write("alpha", "mod.json", R"({
      "id": "alpha", "name": "A", "version": "1.0.0", "api": "1"
    })");
    tmp.Write("mike", "mod.json", R"({
      "id": "mike", "name": "M", "version": "1.0.0", "api": "1"
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 3);
    CHECK(result.mods[0].manifest.id == "alpha");
    CHECK(result.mods[1].manifest.id == "mike");
    CHECK(result.mods[2].manifest.id == "zulu");
}

TEST_CASE("modloader: id helpers enforce the syntax") {
    CHECK(IsValidLocalId("vanilla"));
    CHECK(IsValidLocalId("a"));
    CHECK(IsValidLocalId("a_1"));
    CHECK_FALSE(IsValidLocalId(""));
    CHECK_FALSE(IsValidLocalId("Upper"));
    CHECK_FALSE(IsValidLocalId("has-dash"));
    CHECK_FALSE(IsValidLocalId(std::string(33, 'a')));

    CHECK(IsValidKindId("vanilla:plus4"));
    CHECK_FALSE(IsValidKindId("plus4"));
    CHECK_FALSE(IsValidKindId("a:b:c"));
    CHECK_FALSE(IsValidKindId(":plus4"));
}

TEST_CASE("modloader: engine api version starts at 1") {
    CHECK(EngineModApiVersion() == 1);
}

TEST_CASE("modloader: unreadable mods root is a structured error") {
    LoadResult result =
        ScanModsDirectory("/definitely/not/a/real/directory/uni_test");
    CHECK_FALSE(result.ok());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "root.scan");
}
