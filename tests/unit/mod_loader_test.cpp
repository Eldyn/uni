#include <doctest/doctest.h>
#include <match/modload/mod_loader.hpp>

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
};

const char* kValidManifest = R"({
  "id": "vanilla",
  "name": "UNI Vanilla",
  "version": "1.0.0",
  "api": "1",
  "description": "The standard UNI card set.",
  "author": "eldyn",
  "provides_cards": "cards.json",
  "provides_rules": "rules.json",
  "provides_mutations": "mutations.json"
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

const char* kValidRules = R"({
  "rules": [
    {
      "id": "draw_stacking",
      "title": "Draw Stacking",
      "description": "+2/+4 stack.",
      "hooks": [
        { "on": "before:play", "where": { "card_has_tag": "stackable" },
          "nodes": [ { "id": "n1", "op": "advance_turn", "args": {} } ] }
      ]
    }
  ],
  "statuses": [
    { "id": "shielded", "title": "Shielded", "stack_policy": "replace", "hidden": false }
  ]
})";

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
    tmp.Write("vanilla", "cards.json", kValidCards);
    tmp.Write("vanilla", "rules.json", kValidRules);
    tmp.Write("vanilla", "mutations.json", kValidMutations);
    tmp.Write("vanilla", "decks/classic.json", kValidDeck);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.errors.empty());
    REQUIRE(result.mods.size() == 1);

    const LoadedMod& mod = result.mods[0];
    CHECK(mod.manifest.id == "vanilla");
    CHECK(mod.manifest.name == "UNI Vanilla");
    CHECK(mod.manifest.api == "1");
    CHECK(mod.manifest.provides_cards.has_value());
    CHECK(mod.manifest.provides_mutations.has_value());

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
    tmp.Write("orphan", "cards.json", "[]");

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

TEST_CASE("modloader: absent optional provides files are fine") {
    TempModsRoot tmp;
    tmp.Write("minimal", "mod.json", kMinimalManifest);

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);
    CHECK(result.mods[0].cards.empty());
    CHECK(result.mods[0].rules.empty());
    CHECK(result.mods[0].mutations.empty());
    CHECK(result.mods[0].decks.empty());
}

TEST_CASE("modloader: referenced-but-absent file is a structured error") {
    TempModsRoot tmp;
    tmp.Write("ghost", "mod.json", R"({
      "id": "ghost", "name": "Ghost", "version": "1.0.0", "api": "1",
      "provides_cards": "cards.json"
    })");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "provides.missing");
    CHECK(result.errors[0].artifact == "cards");
    CHECK(result.errors[0].path.find("cards.json") != std::string::npos);
}

TEST_CASE("modloader: duplicate card ids within a mod are rejected") {
    TempModsRoot tmp;
    tmp.Write("dup", "mod.json", R"({
      "id": "dup", "name": "Dup", "version": "1.0.0", "api": "1",
      "provides_cards": "cards.json"
    })");
    tmp.Write("dup", "cards.json", R"([
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
      "id": "faces", "name": "Faces", "version": "1.0.0", "api": "1",
      "provides_cards": "cards.json"
    })");
    tmp.Write("faces", "cards.json", R"([
      { "id": "weird", "face": { "kind": "hologram" } }
    ])");

    LoadResult result = ScanModsDirectory(tmp.root.string());
    CHECK(HasCheck(result.errors, "card.face"));
}

TEST_CASE("modloader: rules.json accepts bare-array and object forms") {
    TempModsRoot tmp;
    tmp.Write("arr", "mod.json", R"({
      "id": "arr", "name": "A", "version": "1.0.0", "api": "1",
      "provides_rules": "rules.json"
    })");
    tmp.Write("arr", "rules.json", R"([
      { "id": "r1", "hooks": [] },
      { "id": "r2", "hooks": [ { "on": "after:play", "nodes": [] } ] }
    ])");
    LoadResult arr = ScanModsDirectory(tmp.root.string());
    REQUIRE(arr.ok());
    REQUIRE(arr.mods[0].rules.size() == 2);
    CHECK(arr.mods[0].rules[1].hooks[0].hook == "after:play");

    TempModsRoot tmp2;
    tmp2.Write("obj", "mod.json", R"({
      "id": "obj", "name": "O", "version": "1.0.0", "api": "1",
      "provides_rules": "rules.json"
    })");
    tmp2.Write("obj", "rules.json", R"({
      "rules": [ { "id": "r1", "hooks": [] } ],
      "statuses": [ { "id": "s1", "stack_policy": "cap:3" } ]
    })");
    LoadResult obj = ScanModsDirectory(tmp2.root.string());
    REQUIRE(obj.ok());
    CHECK(obj.mods[0].rules.size() == 1);
    CHECK(obj.mods[0].statuses.size() == 1);
    CHECK(obj.mods[0].statuses[0].stack_policy == "cap:3");
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
