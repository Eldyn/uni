#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/modload/streak_definition_keys.hpp>
#include <match/streak/state_values.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file streak_definitions_test.cpp
 * @brief The streak definition file kind: loader, namespace rule, matcher
 *        and field checks, context resolution, and validation order.
 */

namespace fs = std::filesystem;
namespace keys = match::modload::streak_keys;

using match::modload::LoadError;
using match::modload::LoadResult;
using match::modload::ScanModsDirectory;
using nlohmann::json;

namespace {

struct TempModsRoot {
    fs::path root;
    TempModsRoot() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_streak_definitions_" + std::to_string(::getpid()) + "_"
                + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempModsRoot() { fs::remove_all(root); }

    void Write(const std::string& relative,
               const std::string& contents) const {
        const fs::path file = root / relative;
        fs::create_directories(file.parent_path());
        std::ofstream out(file);
        out << contents;
    }
};

std::string ManifestFor(const std::string& id) {
    return R"({"id":")" + id + R"(","name":"t","version":"1","api":"1"})";
}

std::string SchemaDir() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        const fs::path candidate = path / "contract" / "schemas";
        if (fs::is_directory(candidate, error)) return candidate.string();
        const fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

bool HasCheck(const std::vector<LoadError>& errors, const std::string& check) {
    for (const LoadError& error : errors) {
        if (error.check == check) return true;
    }
    return false;
}

/** @brief A streak file; `body` holds the keys after the identifier. */
std::string StreakFile(const std::string& identifier,
                       const std::string& body) {
    return R"({
  "uni:format_version": "1.0.0",
  "uni:streak": {
    "uni:description": { "uni:identifier": ")" + identifier + R"(" },
    )" + body + R"(
  }
})";
}

const char* kSkipRunBody = R"(
    "uni:type": "run",
    "uni:matcher": { "uni:event": "turn_skipped" },
    "uni:group_by": "target",
    "uni:reset_on": [ { "uni:event": "direction_changed" } ])";

std::string SecretEventFile() {
    return R"({
  "uni:format_version": "1.0.0",
  "uni:event": {
    "uni:description": { "uni:identifier": "my_mod:secret_seen" },
    "uni:payload": { "uni:fields": {
      "target": { "uni:type": "seat", "uni:audience": "all" },
      "secret": { "uni:type": "integer", "uni:audience": "actor" },
      "color": { "uni:type": "color", "uni:audience": "all" } } },
    "uni:context": ["my_mod:seen_total"]
  }
})";
}

/** @brief One mod with a secret event and one streak file. */
LoadResult LoadMod(const std::string& streak_body,
                   const std::string& identifier = "my_mod:seen_total") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/secret_seen.json", SecretEventFile());
    root.Write("my_mod/streaks/seen_total.json",
               StreakFile(identifier, streak_body));
    return ScanModsDirectory(root.root.string());
}

std::vector<LoadError> Validate(const LoadResult& loaded) {
    match::modload::SemanticValidator validator(SchemaDir());
    return validator.ValidateMod(loaded.mods.at(0));
}

}  // namespace

TEST_CASE("streak definition: a version 1 file loads with its fields") {
    const LoadResult loaded = LoadMod(kSkipRunBody);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.mods.size() == 1);
    REQUIRE(loaded.mods[0].streaks.size() == 1);
    const match::modload::StreakDef& streak = loaded.mods[0].streaks[0];
    CHECK(streak.streak_id == "my_mod:seen_total");
    CHECK(streak.namespace_id == "my_mod");
    CHECK(streak.id == "seen_total");
    CHECK(streak.type == "run");
    CHECK(streak.matcher.event == "turn_skipped");
    CHECK(streak.group_by == "target");
    REQUIRE(streak.reset_on.size() == 1);
    CHECK(streak.reset_on[0].event == "direction_changed");
}

TEST_CASE("streak definition: matcher tests keep their parameters") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "card_played", "uni:tests": [
        { "uni:test": "has_tag", "uni:tag": "uni:skip", "uni:negate": true },
        { "uni:test": "in", "uni:field": "player",
          "uni:values": ["a", "b"] },
        { "uni:test": "equals", "uni:state": "turn", "uni:value": 3 } ] })");
    REQUIRE(loaded.ok());
    const auto& tests = loaded.mods[0].streaks[0].matcher.tests;
    REQUIRE(tests.size() == 3);
    CHECK(tests[0].test == "has_tag");
    CHECK(tests[0].tag == "uni:skip");
    CHECK(tests[0].negate);
    CHECK(tests[1].field == "player");
    CHECK(tests[1].values == json::array({"a", "b"}));
    CHECK(tests[2].state == "turn");
    CHECK(tests[2].value == 3);
    CHECK_FALSE(tests[2].negate);
}

TEST_CASE("streak definition: a last_value streak keeps its value field") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "last_value",
      "uni:matcher": { "uni:event": "active_type_changed" },
      "uni:value_field": "color")");
    REQUIRE(loaded.ok());
    CHECK(loaded.mods[0].streaks[0].value_field == "color");
}

TEST_CASE("streak namespace rule: a mod cannot define a uni streak") {
    const LoadResult loaded = LoadMod(kSkipRunBody, "uni:seen_total");
    CHECK_FALSE(loaded.ok());
    bool found = false;
    for (const LoadError& error : loaded.errors) {
        found = found || error.check == "streak.id.namespace";
    }
    CHECK(found);
}

TEST_CASE("streak namespace rule: a foreign namespace is rejected") {
    const LoadResult loaded = LoadMod(kSkipRunBody, "other_mod:seen_total");
    CHECK_FALSE(loaded.ok());
    bool found = false;
    for (const LoadError& error : loaded.errors) {
        found = found || error.check == "streak.id.namespace";
    }
    CHECK(found);
}

TEST_CASE("streak namespace rule: the uni mod may define a uni streak") {
    TempModsRoot root;
    root.Write("uni/mod.json", ManifestFor("uni"));
    root.Write("uni/streaks/skip_run.json",
               StreakFile("uni:skip_run", kSkipRunBody));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    CHECK(loaded.ok());
}

TEST_CASE("streak file: a bad shape is a loader error") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/streaks/a.json", R"({"uni:streak": {}})");
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "streak.format_version"));
}

TEST_CASE("streak definition: the schema and the key table agree") {
    std::ifstream in(fs::path(SchemaDir()) / "streaks.schema.json");
    REQUIRE(in.is_open());
    const json schema = json::parse(in);
    const json& top = schema.at("properties");
    CHECK(top.contains(std::string(keys::kFormatVersion)));
    CHECK(top.contains(std::string(keys::kStreak)));
    const json& streak = top.at(std::string(keys::kStreak)).at("properties");
    for (const std::string_view key :
         {keys::kDescription, keys::kType, keys::kMatcher, keys::kGroupBy,
          keys::kValueField, keys::kResetOn}) {
        CHECK(streak.contains(std::string(key)));
    }
    const json& definitions = schema.at("$defs");
    const json& matcher = definitions.at("matcher").at("properties");
    for (const std::string_view key : {keys::kMatcherEvent, keys::kTests}) {
        CHECK(matcher.contains(std::string(key)));
    }
    const json& test = definitions.at("test").at("properties");
    for (const std::string_view key :
         {keys::kTest, keys::kField, keys::kState, keys::kValue, keys::kValues,
          keys::kTag, keys::kRelation, keys::kNegate}) {
        CHECK(test.contains(std::string(key)));
    }
    std::vector<std::string> types;
    for (const json& word : streak.at(std::string(keys::kType)).at("enum")) {
        types.push_back(word.get<std::string>());
    }
    const std::vector<std::string> expected(keys::kStreakTypes.begin(),
                                            keys::kStreakTypes.end());
    CHECK(types == expected);
}

TEST_CASE("streak validation: the file schema rejects an unknown type") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "average",
      "uni:matcher": { "uni:event": "turn_skipped" })");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "schema.invalid"));
}

TEST_CASE("streak validation: the checker rejects an unknown type too") {
    LoadResult loaded = LoadMod(kSkipRunBody);
    REQUIRE(loaded.ok());
    loaded.mods[0].streaks[0].type = "average";
    CHECK(HasCheck(Validate(loaded), "streak.type.unknown"));
}

TEST_CASE("streak validation: last_value needs a value field") {
    LoadResult loaded = LoadMod(R"(
      "uni:type": "last_value",
      "uni:matcher": { "uni:event": "active_type_changed" })");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.value_field.missing"));
}

TEST_CASE("streak validation: a value field on a run is rejected") {
    LoadResult loaded = LoadMod(R"(
      "uni:type": "run",
      "uni:matcher": { "uni:event": "turn_skipped" },
      "uni:value_field": "target")");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.value_field.unexpected"));
}

TEST_CASE("streak validation: a valid streak passes") {
    const LoadResult loaded = LoadMod(kSkipRunBody);
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
}

TEST_CASE("streak validation: the group field must be in the payload") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "run",
      "uni:matcher": { "uni:event": "turn_skipped" },
      "uni:group_by": "nobody")");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.field.unknown"));
}

TEST_CASE("streak validation: an unknown matched event is rejected") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "no_such_event" })");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.event.unknown"));
}

TEST_CASE("streak validation: a core event with no public field is rejected") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "play_rejected" })");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.event.unknown"));
}

TEST_CASE("streak validation: a mod event hidden field is not readable") {
    for (const char* body : {
             R"("uni:type": "run",
                "uni:matcher": { "uni:event": "my_mod:secret_seen" },
                "uni:group_by": "secret")",
             R"("uni:type": "last_value",
                "uni:matcher": { "uni:event": "my_mod:secret_seen" },
                "uni:value_field": "secret")",
             R"("uni:type": "total",
                "uni:matcher": { "uni:event": "my_mod:secret_seen",
                  "uni:tests": [ { "uni:test": "equals",
                    "uni:field": "secret", "uni:value": 1 } ] })",
             R"("uni:type": "run",
                "uni:matcher": { "uni:event": "my_mod:secret_seen" },
                "uni:reset_on": [ { "uni:event": "my_mod:secret_seen",
                  "uni:tests": [ { "uni:test": "exists",
                    "uni:field": "secret" } ] } ])"}) {
        const LoadResult loaded = LoadMod(body);
        REQUIRE(loaded.ok());
        CHECK_MESSAGE(HasCheck(Validate(loaded), "streak.field.audience"),
                      body);
    }
}

TEST_CASE("streak validation: the public field of the same event passes") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "run",
      "uni:matcher": { "uni:event": "my_mod:secret_seen" },
      "uni:group_by": "color")");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
}

TEST_CASE("streak validation: a core event hidden field is not readable") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "last_value",
      "uni:matcher": { "uni:event": "cards_drawn" },
      "uni:value_field": "cards")");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.field.audience"));
}

TEST_CASE("streak validation: the relation test is for presentations") {
    const LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "turn_skipped", "uni:tests": [
        { "uni:test": "relation", "uni:relation": "self" } ] })");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.test.relation"));
}

TEST_CASE("streak validation: an unknown test is rejected") {
    LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "turn_skipped", "uni:tests": [
        { "uni:test": "equals", "uni:field": "target",
          "uni:value": "a" } ] })");
    REQUIRE(loaded.ok());
    loaded.mods[0].streaks[0].matcher.tests[0].test = "greater_than";
    CHECK(HasCheck(Validate(loaded), "streak.test.unknown"));
}

TEST_CASE("streak validation: a state value must be in the closed list") {
    const std::vector<std::string_view> expected = {"hand_size", "turn",
                                                    "round"};
    const std::vector<std::string_view> listed(
        match::streak::kStateValueNames.begin(),
        match::streak::kStateValueNames.end());
    CHECK(listed == expected);

    LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "turn_skipped", "uni:tests": [
        { "uni:test": "equals", "uni:state": "turn", "uni:value": 1 } ] })");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    loaded.mods[0].streaks[0].matcher.tests[0].state = "hidden_hand";
    CHECK(HasCheck(Validate(loaded), "streak.state.unknown"));
}

TEST_CASE("streak validation: has_tag needs a namespaced tag") {
    LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "card_played", "uni:tests": [
        { "uni:test": "has_tag", "uni:tag": "my_mod:fancy" } ] })");
    REQUIRE(loaded.ok());
    // INFO: the tag has the namespace of this mod, so a card of this mod
    //       must carry it.
    CHECK(HasCheck(Validate(loaded), "streak.tag.unknown"));
    loaded.mods[0].streaks[0].matcher.tests[0].tag = "bare";
    CHECK(HasCheck(Validate(loaded), "streak.tag.syntax"));
}

TEST_CASE("streak validation: has_tag passes when a card carries the tag") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/cards/fancy_card.json",
               R"({"id":"fancy_card","title":"Fancy",)"
               R"("face":{"kind":"blank"},"tags":["my_mod:fancy"]})");
    root.Write("my_mod/streaks/fancy_total.json",
               StreakFile("my_mod:fancy_total", R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "card_played", "uni:tests": [
        { "uni:test": "has_tag", "uni:tag": "my_mod:fancy" } ] })"));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
}

TEST_CASE("streak validation: has_tag needs a public tags field") {
    LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "turn_skipped", "uni:tests": [
        { "uni:test": "has_tag", "uni:tag": "my_mod:fancy" } ] })");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.field.unknown"));
}

TEST_CASE("streak context: a name that resolves to no streak is rejected") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/secret_seen.json", SecretEventFile());
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.context.unresolved"));
}

TEST_CASE("streak context: a name in another namespace waits for the set") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/secret_seen.json",
               R"({
  "uni:format_version": "1.0.0",
  "uni:event": {
    "uni:description": { "uni:identifier": "my_mod:secret_seen" },
    "uni:context": ["uni:skip_run"]
  }
})");
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
}

TEST_CASE("streak context: the match set resolves a name of another mod") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/secret_seen.json",
               R"({
  "uni:format_version": "1.0.0",
  "uni:event": {
    "uni:description": { "uni:identifier": "my_mod:secret_seen" },
    "uni:context": ["uni:skip_run"]
  }
})");
    root.Write("my_mod/decks/d.json",
               R"({"id":"d","name":"d","mods":["my_mod"],"cards":{}})");
    root.Write("uni/mod.json", ManifestFor("uni"));
    root.Write("uni/streaks/skip_run.json",
               StreakFile("uni:skip_run", kSkipRunBody));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    std::vector<match::modload::LoadedMod> both;
    std::vector<match::modload::LoadedMod> alone;
    match::modload::DeckDef deck;
    for (const auto& mod : loaded.mods) {
        both.push_back(mod);
        if (mod.manifest.id == "my_mod") {
            alone.push_back(mod);
            deck = mod.decks.at(0);
        }
    }
    match::modload::SemanticValidator validator(SchemaDir());
    CHECK(validator.ValidateMatchSet(both, deck).empty());
    CHECK(HasCheck(validator.ValidateMatchSet(alone, deck),
                   "streak.context.unresolved"));
}

TEST_CASE("validation order: an event error comes before a streak error") {
    LoadResult loaded = LoadMod(R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "no_such_event" })");
    REQUIRE(loaded.ok());
    match::modload::SemanticValidator validator(SchemaDir());
    const auto streak_only = validator.ValidateMod(loaded.mods[0]);
    CHECK(HasCheck(streak_only, "streak.event.unknown"));
    loaded.mods[0].events[0].event_id = "my_mod_without_colon";
    const auto both = validator.ValidateMod(loaded.mods[0]);
    CHECK(HasCheck(both, "event.id.namespace"));
    CHECK_FALSE(HasCheck(both, "streak.event.unknown"));
}

TEST_CASE("validation order: a streak error comes before a presentation "
          "error") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/streaks/seen_total.json",
               StreakFile("my_mod:seen_total", R"(
      "uni:type": "total",
      "uni:matcher": { "uni:event": "no_such_event" })"));
    root.Write("my_mod/presentations/p.json",
               R"({"id":"p","target":"hand","modifiers":[)"
               R"({"kind":"no.such.kind"}]})");
    LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CHECK(HasCheck(errors, "streak.event.unknown"));
    CHECK_FALSE(HasCheck(errors, "presentation.kind.unknown"));
}
