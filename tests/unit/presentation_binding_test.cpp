#include <doctest/doctest.h>

#include <match/content_wiring.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/modload/binding_definition_keys.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/view/defs_builder.hpp>
#include <match/view/event_audience.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file presentation_binding_test.cpp
 * @brief The presentation binding file kind: loader, namespace rule, schema
 *        and key table, validator checks (events, matcher, audiences,
 *        relation, beats, sounds, log line), vocabulary guards against the
 *        client sources, and the `defs` manifest with its golden file.
 *
 * Set `UNI_UPDATE_GOLDEN=1` to rewrite the golden file. A normal run never
 * rewrites it.
 */

namespace fs = std::filesystem;
namespace keys = match::modload::binding_keys;

using match::modload::LoadError;
using match::modload::LoadResult;
using match::modload::LoadWarning;
using match::modload::ScanModsDirectory;
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

std::string SchemaDir() {
    return (ProjectRoot() / "contract" / "schemas").string();
}

struct TempModsRoot {
    fs::path root;
    TempModsRoot() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_binding_" + std::to_string(::getpid()) + "_"
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

bool HasCheck(const std::vector<LoadError>& errors, const std::string& check) {
    return std::any_of(errors.begin(), errors.end(),
                       [&](const LoadError& e) { return e.check == check; });
}

bool HasWarning(const std::vector<LoadWarning>& warnings,
                const std::string& check) {
    return std::any_of(
        warnings.begin(), warnings.end(),
        [&](const LoadWarning& w) { return w.check == check; });
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "cannot read " << path);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

const char* kAppleEvent = R"({
  "uni:format_version": "1.0.0",
  "uni:event": {
    "uni:description": { "uni:identifier": "my_mod:apple_eaten" },
    "uni:payload": {
      "uni:fields": {
        "target": { "uni:type": "seat", "uni:audience": "all" },
        "color": { "uni:type": "color", "uni:audience": "actor" },
        "count": { "uni:type": "integer", "uni:audience": "all" },
        "secret": { "uni:type": "string" }
      }
    }
  }
})";

const char* kSoundBundle = R"({
  "id": "sfx", "license": "CC0-1.0", "author": "a",
  "slots": { "sound": [ { "tier": "low", "value": "munch" } ] }
})";

std::string PresentBlock(const std::string& extra = "") {
    return R"({
      "uni:beats": [ [ { "op": "shake", "target": "screen" } ] ],
      "uni:sounds": [ { "uni:sound": "sfx.ui.tick" } ],
      "uni:log": { "uni:variants": [ "log_a", "log_b" ],
                   "uni:params": { "count": { "uni:field": "count" },
                                   "who": { "uni:actor": true } } },
      "uni:glossary": "my_mod:apple")" + extra + R"(
    })";
}

std::string BindingFile(const std::string& identifier,
                        const std::string& event,
                        const std::string& cases) {
    return R"({
  "uni:format_version": "1.0.0",
  "uni:binding": {
    "uni:description": { "uni:identifier": ")" + identifier + R"(" },
    "uni:event": ")" + event + R"(",
    "uni:cases": )" + cases + R"(
  }
})";
}

std::string OneCase(const std::string& tests = "[]",
                    const std::string& present = "") {
    return "[ { \"uni:tests\": " + tests + ", \"uni:present\": "
           + (present.empty() ? PresentBlock() : present) + " } ]";
}

LoadResult LoadMod(const std::string& binding_file,
                   const std::string& mod_id = "my_mod") {
    TempModsRoot root;
    root.Write(mod_id + "/mod.json", ManifestFor(mod_id));
    root.Write(mod_id + "/events/apple_eaten.json", kAppleEvent);
    root.Write(mod_id + "/assets/sfx/index.json", kSoundBundle);
    root.Write(mod_id + "/cards/fruit.json",
               R"({"id":"fruit","title":"F","face":{"kind":"blank"},)"
               R"("tags":["my_mod:fruit"]})");
    root.Write(mod_id + "/presentations/shown.json", binding_file);
    return ScanModsDirectory(root.root.string());
}

LoadResult LoadApple(const std::string& tests = "[]",
                     const std::string& present = "") {
    return LoadMod(BindingFile("my_mod:shown", "my_mod:apple_eaten",
                               OneCase(tests, present)));
}

std::vector<LoadError> Validate(const LoadResult& loaded) {
    match::modload::SemanticValidator validator(SchemaDir());
    return validator.ValidateMod(loaded.mods.at(0));
}

std::vector<LoadWarning> Warnings(const LoadResult& loaded) {
    match::modload::SemanticValidator validator(SchemaDir());
    return validator.WarningsOf(loaded.mods.at(0));
}

std::string Describe(const std::vector<LoadError>& errors) {
    std::string text;
    for (const LoadError& e : errors) text += e.check + ": " + e.message + "\n";
    return text;
}

}  // namespace

// --- loader ----------------------------------------------------------------

TEST_CASE("binding: a version 1 file loads with its cases") {
    const LoadResult loaded = LoadApple(
        R"([ { "uni:test": "relation", "uni:role": "actor",
               "uni:relation": "self" } ])");
    REQUIRE(loaded.ok());
    REQUIRE(loaded.mods.size() == 1);
    REQUIRE(loaded.mods[0].bindings.size() == 1);
    const match::modload::BindingDef& binding = loaded.mods[0].bindings[0];
    CHECK(binding.binding_id == "my_mod:shown");
    CHECK(binding.id == "shown");
    CHECK(binding.namespace_id == "my_mod");
    CHECK(binding.event == "my_mod:apple_eaten");
    REQUIRE(binding.cases.size() == 1);
    const auto& binding_case = binding.cases[0];
    REQUIRE(binding_case.tests.size() == 1);
    CHECK(binding_case.tests[0].test == "relation");
    CHECK(binding_case.tests[0].role == "actor");
    CHECK(binding_case.tests[0].relation == "self");
    CHECK(binding_case.beats.size() == 1);
    REQUIRE(binding_case.sounds.size() == 1);
    CHECK(binding_case.sounds[0].sound == "sfx.ui.tick");
    REQUIRE(binding_case.log.has_value());
    CHECK(binding_case.log->variants == std::vector<std::string>{"log_a", "log_b"});
    CHECK(binding_case.log->params.at("count").field == "count");
    CHECK(binding_case.log->params.at("who").actor);
    CHECK(binding_case.glossary == "my_mod:apple");
}

TEST_CASE("binding: the old presentation kind loads next to a binding") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/apple_eaten.json", kAppleEvent);
    root.Write("my_mod/presentations/lsd.json",
               R"({"id":"lsd","target":"hand","modifiers":[)"
               R"({"kind":"face.override"}]})");
    root.Write("my_mod/presentations/shown.json",
               BindingFile("my_mod:shown", "my_mod:apple_eaten", OneCase()));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.mods[0].presentations.size() == 1);
    CHECK(loaded.mods[0].bindings.size() == 1);
}

TEST_CASE("binding: a mod with no presentation loads and validates") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/apple_eaten.json", kAppleEvent);
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.mods[0].bindings.empty());
    CHECK(loaded.mods[0].presentations.empty());
    CHECK(Validate(loaded).empty());
}

TEST_CASE("binding namespace rule: a mod defines binding ids in its own "
          "namespace only") {
    const LoadResult foreign = LoadMod(
        BindingFile("other_mod:shown", "my_mod:apple_eaten", OneCase()));
    CHECK_FALSE(foreign.ok());
    CHECK(HasCheck(foreign.errors, "binding.id.namespace"));
    const LoadResult uni_id = LoadMod(
        BindingFile("uni:shown", "my_mod:apple_eaten", OneCase()));
    CHECK(HasCheck(uni_id.errors, "binding.id.namespace"));
}

TEST_CASE("binding namespace rule: the uni mod may define a uni binding") {
    TempModsRoot root;
    root.Write("uni/mod.json", ManifestFor("uni"));
    root.Write("uni/presentations/skip.json",
               BindingFile("uni:skip_shown", "turn_skipped", OneCase()));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.mods[0].bindings[0].binding_id == "uni:skip_shown");
}

TEST_CASE("binding file: two files with one id are a loader error") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/presentations/a.json",
               BindingFile("my_mod:same", "turn_skipped", OneCase()));
    root.Write("my_mod/presentations/b.json",
               BindingFile("my_mod:same", "turn_skipped", OneCase()));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    CHECK(HasCheck(loaded.errors, "binding.id.duplicate"));
}

TEST_CASE("binding file: a bad shape is a loader error") {
    const auto load = [](const std::string& text) {
        return LoadMod(text).errors;
    };
    CHECK(HasCheck(load(R"({"uni:format_version":"1.0.0",)"
                        R"("uni:binding":[]})"),
                   "binding.shape"));
    CHECK(HasCheck(load(R"({"uni:format_version":"9.0.0",)"
                        R"("uni:binding":{}})"),
                   "binding.format_version"));
    CHECK(HasCheck(load(R"({"uni:format_version":"1.0.0",)"
                        R"("uni:binding":{"uni:description":{}}})"),
                   "binding.identifier"));
    CHECK(HasCheck(load(R"({"uni:format_version":"1.0.0","uni:binding":{)"
                        R"("uni:description":{"uni:identifier":"my_mod:s"}}})"),
                   "binding.event"));
    CHECK(HasCheck(load(R"({"uni:format_version":"1.0.0","uni:binding":{)"
                        R"("uni:description":{"uni:identifier":"my_mod:s"},)"
                        R"("uni:event":"turn_skipped"}})"),
                   "binding.cases"));
    CHECK(HasCheck(load(R"({"uni:format_version":"1.0.0","uni:binding":{)"
                        R"("uni:description":{"uni:identifier":"my_mod:s"},)"
                        R"("uni:event":"turn_skipped",)"
                        R"("uni:cases":[{"uni:present":[]}]}})"),
                   "binding.present"));
}

TEST_CASE("binding file: more cases than the limit is a loader error") {
    std::string cases = "[";
    for (std::size_t index = 0; index <= keys::kMaxCases; ++index) {
        cases += (index == 0 ? "" : ",") + std::string(R"({"uni:present":{)")
                 + R"("uni:glossary":"my_mod:apple"}})";
    }
    cases += "]";
    const LoadResult loaded =
        LoadMod(BindingFile("my_mod:shown", "turn_skipped", cases));
    CHECK(HasCheck(loaded.errors, "binding.cases"));
}

// --- schema and key table --------------------------------------------------

TEST_CASE("binding: the schema and the key table agree") {
    std::ifstream in(fs::path(SchemaDir()) / "bindings.schema.json");
    REQUIRE(in.is_open());
    const json schema = json::parse(in);
    const json& top = schema.at("properties");
    CHECK(top.contains(std::string(keys::kFormatVersion)));
    const json& binding =
        top.at(std::string(keys::kBinding)).at("properties");
    for (const std::string_view key :
         {keys::kDescription, keys::kEvent, keys::kCases, keys::kSelect}) {
        CHECK_MESSAGE(binding.contains(std::string(key)), key);
    }
    CHECK(binding.at(std::string(keys::kSelect)).at("enum")
          == json::array({keys::kSelectAll, keys::kSelectFirst}));
    const json& definitions = schema.at("$defs");
    const json& case_properties = definitions.at("case").at("properties");
    CHECK(case_properties.contains(std::string(keys::kTests)));
    CHECK(case_properties.contains(std::string(keys::kPresent)));
    const json& present = definitions.at("present").at("properties");
    for (const std::string_view key :
         {keys::kBeats, keys::kSounds, keys::kLog, keys::kGlossary}) {
        CHECK_MESSAGE(present.contains(std::string(key)), key);
    }
    const json& sound =
        present.at(std::string(keys::kSounds)).at("items").at("properties");
    for (const std::string_view key :
         {keys::kSound, keys::kVolume, keys::kPitch}) {
        CHECK_MESSAGE(sound.contains(std::string(key)), key);
    }
    const json& log =
        present.at(std::string(keys::kLog)).at("properties");
    CHECK(log.contains(std::string(keys::kVariants)));
    CHECK(log.contains(std::string(keys::kParams)));
    const json& param =
        log.at(std::string(keys::kParams)).at("additionalProperties")
            .at("properties");
    for (const std::string_view key :
         {keys::kParamField, keys::kParamValue, keys::kParamActor,
          keys::kParamStreak, keys::kParamKey, keys::kParamKeyField,
          keys::kParamDefault, keys::kParamCardPart}) {
        CHECK_MESSAGE(param.contains(std::string(key)), key);
    }
    const json& test = definitions.at("test").at("properties");
    for (const std::string_view key :
         {keys::kParamStreak, keys::kParamKey, keys::kParamKeyField,
          keys::kParamActor}) {
        CHECK_MESSAGE(test.contains(std::string(key)), key);
    }
    CHECK(test.at("uni:test").at("enum")
          == json(std::vector<std::string_view>(keys::kBindingTests.begin(),
                                                keys::kBindingTests.end())));
    CHECK(keys::kBindingTests.size() == 10);
    CHECK(keys::kBindingTests.back() == keys::kCausedBy);
    CHECK(test.contains(std::string(keys::kRole)));
    CHECK(test.at(std::string(keys::kRole)).at("enum")
          == json::array({keys::kActorRole, keys::kTargetRole}));
    CHECK(test.at("uni:relation").at("enum")
          == json::array({keys::kSelf, keys::kOther}));
}

TEST_CASE("binding validation: the file schema rejects an unknown key") {
    const LoadResult loaded = LoadApple(
        "[]", PresentBlock(R"(, "uni:surprise": 1)"));
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "schema.invalid"));
}

// --- validation: events ----------------------------------------------------

TEST_CASE("binding validation: a valid binding passes without warnings") {
    const LoadResult loaded = LoadApple(
        R"([ { "uni:test": "equals", "uni:field": "count", "uni:value": 3 },
             { "uni:test": "relation", "uni:role": "target",
               "uni:relation": "other" } ])");
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CAPTURE(Describe(errors));
    CHECK(errors.empty());
    CHECK(Warnings(loaded).empty());
}

TEST_CASE("binding validation: the event must be a known event") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& binding = loaded.mods[0].bindings[0];
    binding.event = "my_mod:no_such_event";
    CHECK(HasCheck(Validate(loaded), "binding.event.unknown"));
    binding.event = "no_such_core_event";
    CHECK(HasCheck(Validate(loaded), "binding.event.unknown"));
    binding.event = "mod_event";
    CHECK(HasCheck(Validate(loaded), "binding.event.unknown"));
}

TEST_CASE("binding validation: a binding may name a core event") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& binding = loaded.mods[0].bindings[0];
    binding.event = "turn_skipped";
    binding.cases[0].log->params.clear();
    CAPTURE(Describe(Validate(loaded)));
    CHECK(Validate(loaded).empty());
    binding.event = "near_win_entered";
    CHECK(Validate(loaded).empty());
}

TEST_CASE("binding validation: a mod cannot bind to the event of another "
          "mod") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& binding = loaded.mods[0].bindings[0];
    binding.event = "other_mod:apple_eaten";
    CHECK(HasCheck(Validate(loaded), "binding.event.foreign"));
    binding.event = "my_mod:apple_eaten";
    CHECK_FALSE(HasCheck(Validate(loaded), "binding.event.foreign"));
    binding.event = "uni:apple_eaten";
    CHECK_FALSE(HasCheck(Validate(loaded), "binding.event.foreign"));
}

// --- validation: matcher and audiences -------------------------------------

TEST_CASE("binding validation: a test field must be a payload field") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    loaded.mods[0].bindings[0].cases[0].tests[0].field = "nobody";
    CHECK(HasCheck(Validate(loaded), "binding.field.unknown"));
}

TEST_CASE("binding validation: a field with the audience none is not "
          "readable") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    loaded.mods[0].bindings[0].cases[0].tests[0].field = "secret";
    const auto errors = Validate(loaded);
    CHECK(HasCheck(errors, "binding.field.audience"));
}

TEST_CASE("binding validation: a field with a partial audience is a "
          "warning") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "color" } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    const auto warnings = Warnings(loaded);
    CHECK(HasWarning(warnings, "binding.field.audience.partial"));
    CHECK_FALSE(HasWarning(warnings, "binding.field.audience"));
}

TEST_CASE("binding validation: a core field with the audience owner warns") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    auto& binding = loaded.mods[0].bindings[0];
    binding.event = "cards_drawn";
    binding.cases[0].log->params.clear();
    binding.cases[0].tests[0].field = "cards";
    CHECK(Validate(loaded).empty());
    CHECK(HasWarning(Warnings(loaded), "binding.field.audience.partial"));
}

TEST_CASE("binding validation: a log parameter obeys the field rules") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    params["count"].field = "nobody";
    CHECK(HasCheck(Validate(loaded), "binding.field.unknown"));
    params["count"].field = "secret";
    CHECK(HasCheck(Validate(loaded), "binding.field.audience"));
    params["count"].field = "color";
    CHECK(Validate(loaded).empty());
    CHECK(HasWarning(Warnings(loaded), "binding.field.audience.partial"));
}

TEST_CASE("binding validation: a log parameter has exactly one source") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    params["count"].actor = true;
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
    params["count"] = match::modload::BindingParamDef{};
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}

TEST_CASE("binding validation: a step target may read a payload field") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& beat = loaded.mods[0].bindings[0].cases[0].beats[0];
    beat[0]["target"] = "$payload.target";
    CHECK(Validate(loaded).empty());
    beat[0]["target"] = "$payload.nobody";
    CHECK(HasCheck(Validate(loaded), "binding.field.unknown"));
    beat[0]["target"] = "$payload.secret";
    CHECK(HasCheck(Validate(loaded), "binding.field.audience"));
}

TEST_CASE("binding validation: unknown test words are errors") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    loaded.mods[0].bindings[0].cases[0].tests[0].test = "greater_than";
    CHECK(HasCheck(Validate(loaded), "binding.test.unknown"));
}

TEST_CASE("binding validation: a test needs its parameters") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    test.field.clear();
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: has_tag reads a tag that a card carries") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& binding = loaded.mods[0].bindings[0];
    binding.event = "card_played";
    binding.cases[0].log->params.clear();
    match::modload::StreakTestDef test;
    test.test = "has_tag";
    test.tag = "my_mod:fruit";
    binding.cases[0].tests = {test};
    CAPTURE(Describe(Validate(loaded)));
    CHECK(Validate(loaded).empty());
    binding.cases[0].tests[0].tag = "my_mod:no_such_tag";
    CHECK(HasCheck(Validate(loaded), "binding.tag.unknown"));
    binding.cases[0].tests[0].tag = "badtag";
    CHECK(HasCheck(Validate(loaded), "binding.tag.syntax"));
    binding.cases[0].tests[0].tag = "my_mod:fruit";
    binding.event = "my_mod:apple_eaten";
    CHECK(HasCheck(Validate(loaded), "binding.field.unknown"));
}

// --- validation: relation --------------------------------------------------

TEST_CASE("binding validation: relation is allowed in a binding") {
    for (const char* role : {"actor", "target"}) {
        for (const char* relation : {"self", "other"}) {
            LoadResult loaded = LoadApple(
                std::string(R"([ { "uni:test": "relation", "uni:role": ")")
                + role + R"(", "uni:relation": ")" + relation + R"(" } ])");
            REQUIRE(loaded.ok());
            CAPTURE(role);
            CAPTURE(relation);
            CHECK(Validate(loaded).empty());
        }
    }
}

TEST_CASE("binding validation: a relation needs a known role and value") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "relation", "uni:role": "actor",
               "uni:relation": "self" } ])");
    REQUIRE(loaded.ok());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    test.relation = "friend";
    CHECK(HasCheck(Validate(loaded), "binding.relation.value"));
    test.relation = "self";
    test.role = "owner";
    CHECK(HasCheck(Validate(loaded), "binding.relation.role"));
    test.role.clear();
    CHECK(HasCheck(Validate(loaded), "binding.relation.role"));
}

TEST_CASE("binding validation: the target role needs a target payload "
          "field") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "relation", "uni:role": "target",
               "uni:relation": "self" } ])");
    REQUIRE(loaded.ok());
    auto& binding = loaded.mods[0].bindings[0];
    binding.event = "card_played";
    binding.cases[0].log->params.clear();
    CHECK(HasCheck(Validate(loaded), "binding.relation.target"));
    binding.event = "turn_skipped";
    CHECK(Validate(loaded).empty());
}

TEST_CASE("binding validation: a streak cannot use relation") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/streaks/s.json", R"({
  "uni:format_version": "1.0.0",
  "uni:streak": {
    "uni:description": { "uni:identifier": "my_mod:s" },
    "uni:type": "total",
    "uni:matcher": { "uni:event": "turn_skipped", "uni:tests": [
      { "uni:test": "relation", "uni:relation": "self" } ] } } })");
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "streak.test.relation"));
}

// --- validation: outputs ---------------------------------------------------

TEST_CASE("binding validation: a beat op must be in the vocabulary") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& beat = loaded.mods[0].bindings[0].cases[0].beats[0];
    beat[0]["op"] = "teleport";
    CHECK(HasCheck(Validate(loaded), "binding.beat.op"));
    for (const std::string_view op : keys::kBeatOps) {
        beat[0]["op"] = std::string(op);
        CHECK_MESSAGE(Validate(loaded).empty(), op);
    }
}

TEST_CASE("binding validation: a beat step needs an op and a target") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& beat = loaded.mods[0].bindings[0].cases[0].beats[0];
    beat[0].erase("target");
    CHECK(HasCheck(Validate(loaded), "binding.beat.shape"));
    beat = json::array();
    CHECK(HasCheck(Validate(loaded), "binding.beat.shape"));
}

TEST_CASE("binding validation: a sound id must exist") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& sounds = loaded.mods[0].bindings[0].cases[0].sounds;
    sounds[0].sound = "sfx.no.such.sound";
    CHECK(HasCheck(Validate(loaded), "binding.sound.unknown"));
    sounds[0].sound = "munch";
    CAPTURE(Describe(Validate(loaded)));
    CHECK(Validate(loaded).empty());
    for (const std::string_view sound : keys::kCoreSoundIds) {
        sounds[0].sound = std::string(sound);
        CHECK_MESSAGE(Validate(loaded).empty(), sound);
    }
}

TEST_CASE("binding validation: a sound value of another mod is unknown") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    loaded.mods[0].bindings[0].cases[0].sounds[0].sound = "other_munch";
    CHECK(HasCheck(Validate(loaded), "binding.sound.unknown"));
}

TEST_CASE("binding validation: log variant keys are checked") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& log = *loaded.mods[0].bindings[0].cases[0].log;
    log.variants = {"has space"};
    CHECK(HasCheck(Validate(loaded), "binding.log.key"));
    log.variants = {};
    CHECK(HasCheck(Validate(loaded), "binding.log.key"));
    log.variants = {"log_a", "log_a"};
    CHECK(HasCheck(Validate(loaded), "binding.log.key"));
}

TEST_CASE("binding validation: a case must present something") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& binding_case = loaded.mods[0].bindings[0].cases[0];
    binding_case.beats.clear();
    binding_case.sounds.clear();
    binding_case.log.reset();
    binding_case.glossary.clear();
    CHECK(HasCheck(Validate(loaded), "binding.empty"));
}

TEST_CASE("binding validation: the stage comes after derived and before "
          "texts") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/presentations/shown.json",
               BindingFile("my_mod:shown", "no_such_event", OneCase()));
    root.Write("my_mod/presentations/lsd.json",
               R"({"id":"lsd","target":"hand","modifiers":[)"
               R"({"kind":"no.such.kind"}]})");
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CHECK(HasCheck(errors, "binding.event.unknown"));
    CHECK(HasCheck(errors, "presentation.kind.unknown"));
}

TEST_CASE("binding validation: a presentation error never hides an earlier "
          "stage") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/streaks/s.json", R"({
  "uni:format_version": "1.0.0",
  "uni:streak": {
    "uni:description": { "uni:identifier": "my_mod:s" },
    "uni:type": "total",
    "uni:matcher": { "uni:event": "no_such_event" } } })");
    root.Write("my_mod/presentations/shown.json",
               BindingFile("my_mod:shown", "no_such_event", OneCase()));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CHECK(HasCheck(errors, "streak.event.unknown"));
    CHECK_FALSE(HasCheck(errors, "binding.event.unknown"));
}

TEST_CASE("binding validation: a derived error comes before a binding "
          "error") {
    TempModsRoot root;
    root.Write("uni/mod.json", ManifestFor("uni"));
    root.Write("uni/derived/d.json", R"({
  "uni:format_version": "1.0.0",
  "uni:derived_event": {
    "uni:description": { "uni:identifier": "uni:near_win_entered" },
    "uni:emits": "no_such_event",
    "uni:open_on": [ { "uni:event": "card_played",
                       "uni:seat_field": "player" } ] } })");
    root.Write("uni/presentations/shown.json",
               BindingFile("uni:shown", "no_such_event", OneCase()));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CHECK(HasCheck(errors, "derived.emits.unknown"));
    CHECK_FALSE(HasCheck(errors, "binding.event.unknown"));
}

TEST_CASE("binding validation: a warning is reported and keeps the mod") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/apple_eaten.json", kAppleEvent);
    root.Write("my_mod/presentations/shown.json",
               BindingFile("my_mod:shown", "my_mod:apple_eaten",
                           OneCase(R"([ { "uni:test": "exists",
                                          "uni:field": "color" } ])")));
    LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    match::modload::SemanticValidator validator(SchemaDir());
    match::modload::ValidateLoadedMods(loaded, validator);
    CHECK(loaded.mods.size() == 1);
    CHECK(HasWarning(loaded.warnings, "binding.field.audience.partial"));
    REQUIRE(loaded.reports.size() == 1);
    CHECK(loaded.reports[0].ok);
    CHECK(HasWarning(loaded.reports[0].warnings,
                     "binding.field.audience.partial"));
}

TEST_CASE("binding validation: an error removes the mod") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/presentations/shown.json",
               BindingFile("my_mod:shown", "no_such_event", OneCase()));
    LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    match::modload::SemanticValidator validator(SchemaDir());
    match::modload::ValidateLoadedMods(loaded, validator);
    CHECK(loaded.mods.empty());
    CHECK(HasCheck(loaded.errors, "binding.event.unknown"));
}

// --- vocabulary guards -----------------------------------------------------

TEST_CASE("binding vocabulary: the beat ops match the client registry") {
    const std::string source = ReadText(
        ProjectRoot() / "frontend" / "src" / "lib" / "components" / "game"
        / "animation" / "cardRegistry.svelte.ts");
    const std::size_t start = source.find("#registry: Record<string");
    REQUIRE(start != std::string::npos);
    const std::size_t end = source.find("};", start);
    const std::string table = source.substr(start, end - start);
    std::vector<std::string> client_ops;
    std::size_t cursor = table.find('{');
    while ((cursor = table.find('\n', cursor)) != std::string::npos) {
        const std::size_t colon = table.find(':', cursor);
        if (colon == std::string::npos) break;
        std::string name = table.substr(cursor + 1, colon - cursor - 1);
        name.erase(std::remove_if(name.begin(), name.end(), ::isspace),
                   name.end());
        if (!name.empty()) client_ops.push_back(name);
        cursor = colon;
    }
    std::vector<std::string> server_ops(keys::kBeatOps.begin(),
                                        keys::kBeatOps.end());
    std::sort(client_ops.begin(), client_ops.end());
    std::sort(server_ops.begin(), server_ops.end());
    CHECK(client_ops == server_ops);
}

TEST_CASE("binding vocabulary: the core sound ids match the client "
          "catalog") {
    const std::string source = ReadText(
        ProjectRoot() / "frontend" / "src" / "lib" / "data"
        / "audioCatalogs.ts");
    const std::size_t start = source.find("SFX_CATALOG");
    REQUIRE(start != std::string::npos);
    std::vector<std::string> client_ids;
    std::size_t cursor = start;
    const std::string needle = "\n\t\"sfx.";
    while ((cursor = source.find(needle, cursor)) != std::string::npos) {
        const std::size_t quote = cursor + 2;
        const std::size_t close = source.find('"', quote + 1);
        client_ids.push_back(source.substr(quote + 1, close - quote - 1));
        cursor = close;
    }
    std::vector<std::string> server_ids(keys::kCoreSoundIds.begin(),
                                        keys::kCoreSoundIds.end());
    std::sort(client_ids.begin(), client_ids.end());
    std::sort(server_ids.begin(), server_ids.end());
    CHECK(client_ids == server_ids);
}

// --- registry and defs manifest --------------------------------------------

namespace {

struct FixtureDefs {
    LoadResult loaded;
    match::engine::MatchRegistries registries;
};

FixtureDefs LoadFixtureDefs(const fs::path& mods_root) {
    FixtureDefs fixture;
    fixture.loaded = ScanModsDirectory(mods_root.string());
    REQUIRE(fixture.loaded.ok());
    std::vector<match::modload::EventDef> events;
    std::vector<match::modload::BindingDef> bindings;
    std::vector<match::modload::PresentationDef> presentations;
    for (const auto& mod : fixture.loaded.mods) {
        fixture.registries.mods.push_back(
            {mod.manifest.id, mod.manifest.version});
        fixture.registries.kinds_by_mod.emplace_back();
        events.insert(events.end(), mod.events.begin(), mod.events.end());
        bindings.insert(bindings.end(), mod.bindings.begin(),
                        mod.bindings.end());
        presentations.insert(presentations.end(), mod.presentations.begin(),
                             mod.presentations.end());
    }
    match::wiring::LoadEvents(events, fixture.registries.events);
    match::wiring::LoadBindings(bindings, fixture.registries.bindings);
    match::wiring::LoadPresentations(presentations,
                                     fixture.registries.presentations);
    return fixture;
}

fs::path FixtureMods() {
    return ProjectRoot() / "tests" / "fixtures" / "golden_presentation_binding"
           / "mods";
}

}  // namespace

TEST_CASE("binding wiring: the registry is keyed by the full id") {
    FixtureDefs fixture = LoadFixtureDefs(FixtureMods());
    REQUIRE(fixture.registries.bindings.size() == 1);
    CHECK(fixture.registries.bindings.count("fixture_mod:apple_shown") == 1);
}

TEST_CASE("defs: the presentations manifest lists a binding with its id and "
          "hash") {
    FixtureDefs fixture = LoadFixtureDefs(FixtureMods());
    const json defs = match::view::DefsBuilder::Build(fixture.registries,
                                                      fixture.loaded.mods);
    const json& presentations = defs["presentations"];
    REQUIRE(presentations.size() == 2);
    const json* binding = nullptr;
    for (const json& entry : presentations) {
        if (entry.contains(std::string(keys::kBinding))) binding = &entry;
    }
    REQUIRE(binding != nullptr);
    CHECK((*binding)["id"] == "fixture_mod:apple_shown");
    CHECK((*binding)["def_hash"].get<std::string>().size() == 16);
    CHECK((*binding)[std::string(keys::kBinding)]
                    [std::string(keys::kEvent)]
          == "fixture_mod:apple_eaten");
}

TEST_CASE("defs: a binding carries no audience and no hidden field value") {
    FixtureDefs fixture = LoadFixtureDefs(FixtureMods());
    const json defs = match::view::DefsBuilder::Build(fixture.registries,
                                                      fixture.loaded.mods);
    const std::string text = defs["presentations"].dump();
    CHECK(text.find("audience") == std::string::npos);
}

TEST_CASE("defs: a changed binding changes the entry hash and the digest") {
    FixtureDefs before = LoadFixtureDefs(FixtureMods());
    const json first = match::view::DefsBuilder::Build(before.registries,
                                                       before.loaded.mods);
    before.registries.bindings["fixture_mod:apple_shown"]
        .raw[std::string(keys::kBinding)][std::string(keys::kEvent)] =
        "turn_skipped";
    const json second = match::view::DefsBuilder::Build(before.registries,
                                                        before.loaded.mods);
    CHECK(first["defs_digest"] != second["defs_digest"]);
    CHECK(first["presentations"] != second["presentations"]);
}

TEST_CASE("defs: the presentations manifest matches the golden file") {
    FixtureDefs fixture = LoadFixtureDefs(FixtureMods());
    const json defs = match::view::DefsBuilder::Build(fixture.registries,
                                                      fixture.loaded.mods);
    const json actual = {{"mods", defs["mods"]},
                         {"presentations", defs["presentations"]},
                         {"events", defs["events"]},
                         {"defs_digest", defs["defs_digest"]}};
    const fs::path golden = ProjectRoot() / "tests" / "fixtures"
                            / "golden_presentation_binding"
                            / "defs_presentations.json";
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(2) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}

// --- the test mod ----------------------------------------------------------

namespace {

LoadResult LoadRealMods() {
    return ScanModsDirectory((ProjectRoot() / "mods").string());
}

const match::modload::LoadedMod& TestMod(const LoadResult& loaded) {
    for (const auto& mod : loaded.mods) {
        if (mod.manifest.id == "test") return mod;
    }
    FAIL("the test mod is not loaded");
    return loaded.mods.front();
}

}  // namespace

TEST_CASE("test mod: it binds a presentation to its own apple event") {
    const LoadResult loaded = LoadRealMods();
    const auto& mod = TestMod(loaded);
    REQUIRE(mod.bindings.size() == 1);
    const auto& binding = mod.bindings[0];
    CHECK(binding.binding_id == "test:apple_eaten_shown");
    CHECK(binding.event == "test:apple_eaten");
    REQUIRE(binding.cases.size() == 2);
    CHECK(binding.cases[0].tests[0].relation == "self");
    CHECK(binding.cases[1].tests[0].relation == "other");
    CHECK_FALSE(binding.cases[0].beats.empty());
    CHECK_FALSE(binding.cases[0].sounds.empty());
    CHECK(binding.cases[0].log.has_value());
    CHECK(binding.cases[1].log.has_value());
    CHECK(binding.cases[0].log->variants != binding.cases[1].log->variants);
}

TEST_CASE("test mod: the binding validates without an error or a warning") {
    const LoadResult loaded = LoadRealMods();
    match::modload::SemanticValidator validator(SchemaDir());
    const auto errors = validator.ValidateMod(TestMod(loaded));
    CAPTURE(Describe(errors));
    CHECK(errors.empty());
    CHECK(validator.WarningsOf(TestMod(loaded)).empty());
}

TEST_CASE("standard mods: each binding validates cleanly") {
    const LoadResult loaded = LoadRealMods();
    match::modload::SemanticValidator validator(SchemaDir());
    const std::vector<const char*> kStandardModsWithBindings = {
        "draw_stacking", "jump_in", "seven_zero", "progressive"};
    for (const char* mod_id : kStandardModsWithBindings) {
        const match::modload::LoadedMod* found = nullptr;
        for (const auto& mod : loaded.mods) {
            if (mod.manifest.id == mod_id) found = &mod;
        }
        REQUIRE_MESSAGE(found != nullptr, "the mod is not loaded: " << mod_id);
        CAPTURE(mod_id);
        CHECK_FALSE(found->bindings.empty());
        const auto errors = validator.ValidateMod(*found);
        CAPTURE(Describe(errors));
        CHECK(errors.empty());
        CHECK(validator.WarningsOf(*found).empty());
    }
}

TEST_CASE("test mod: the bindings manifest matches the golden file") {
    const LoadResult loaded = LoadRealMods();
    const auto& mod = TestMod(loaded);
    match::engine::MatchRegistries registries;
    registries.mods.push_back({mod.manifest.id, mod.manifest.version});
    registries.kinds_by_mod.emplace_back();
    match::wiring::LoadEvents(mod.events, registries.events);
    match::wiring::LoadBindings(mod.bindings, registries.bindings);
    const json defs = match::view::DefsBuilder::Build(registries, {mod});
    json bindings = json::array();
    for (const json& entry : defs["presentations"]) {
        if (entry.contains(std::string(keys::kBinding))) {
            bindings.push_back(entry);
        }
    }
    const json actual = {{"presentations", bindings},
                         {"events", defs["events"]}};
    const fs::path golden = ProjectRoot() / "tests" / "fixtures"
                            / "golden_presentation_binding"
                            / "defs_test_mod_bindings.json";
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(2) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}

TEST_CASE("binding loader: streak source, select and numeric words parse") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "gte", "uni:streak": "uni:draw_run",
               "uni:key_field": "target", "uni:value": 4 } ])");
    REQUIRE(loaded.ok());
    const auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    CHECK(test.test == "gte");
    CHECK(test.streak == "uni:draw_run");
    CHECK(test.key_field == "target");
    CHECK(test.key.empty());
    CHECK(loaded.mods[0].bindings[0].select == "all");
}

TEST_CASE("binding loader: an actor test parses") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:actor": true },
             { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    const auto& tests = loaded.mods[0].bindings[0].cases[0].tests;
    CHECK(tests[0].actor);
    CHECK(tests[0].field.empty());
    CHECK_FALSE(tests[1].actor);
}

TEST_CASE("binding loader: a caused_by test parses") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "caused_by", "uni:value": "card_played",
               "uni:tag": "wild" } ])");
    REQUIRE(loaded.ok());
    const auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    CHECK(test.test == "caused_by");
    CHECK(test.value == "card_played");
    CHECK(test.tag == "wild");
    CHECK(test.field.empty());
}

TEST_CASE("binding loader: select, param streak and test key parse") {
    const std::string present = R"({
      "uni:log": { "uni:variants": [ "log_a" ],
                   "uni:params": {
                     "run": { "uni:streak": "uni:skip_run",
                              "uni:key": "a" },
                     "by_seat": { "uni:streak": "uni:skip_run",
                                  "uni:key_field": "target" } } } })";
    const std::string file = BindingFile(
        "my_mod:shown", "my_mod:apple_eaten",
        "[ { \"uni:tests\": [ { \"uni:test\": \"gte\","
        " \"uni:streak\": \"uni:skip_run\", \"uni:key\": \"a\","
        " \"uni:value\": 2 } ], \"uni:present\": " + present + " } ]");
    const std::string with_select =
        file.substr(0, file.find("\"uni:cases\""))
        + "\"uni:select\": \"first\", "
        + file.substr(file.find("\"uni:cases\""));
    LoadResult loaded = LoadMod(with_select);
    REQUIRE(loaded.ok());
    const auto& binding = loaded.mods[0].bindings[0];
    CHECK(binding.select == "first");
    const auto& binding_case = binding.cases[0];
    CHECK(binding_case.tests[0].key == "a");
    const auto& params = binding_case.log->params;
    CHECK(params.at("run").streak == "uni:skip_run");
    CHECK(params.at("run").key == "a");
    CHECK(params.at("by_seat").streak == "uni:skip_run");
    CHECK(params.at("by_seat").key_field == "target");
}

TEST_CASE("binding loader: a param default parses") {
    const std::string present = R"({
      "uni:log": { "uni:variants": [ "log_a" ],
                   "uni:params": {
                     "run": { "uni:streak": "uni:skip_run",
                              "uni:default": 0 },
                     "who": { "uni:field": "target",
                              "uni:default": "nobody" },
                     "bad": { "uni:field": "target",
                              "uni:default": [ 1 ] },
                     "plain": { "uni:field": "target" } } } })";
    LoadResult loaded = LoadMod(BindingFile(
        "my_mod:shown", "my_mod:apple_eaten",
        "[ { \"uni:present\": " + present + " } ]"));
    REQUIRE(loaded.ok());
    const auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    CHECK(params.at("run").default_value == json(0));
    CHECK(params.at("who").default_value == json("nobody"));
    CHECK_FALSE(params.at("bad").default_value.has_value());
    CHECK_FALSE(params.at("plain").default_value.has_value());
}

TEST_CASE("binding validation: a param default is a string or a number") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    params["count"].default_value = "none";
    CHECK(Validate(loaded).empty());
    params["count"].default_value = 0;
    CHECK(Validate(loaded).empty());
    params["count"].default_value = json::object();
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}

TEST_CASE("binding loader: a param card part parses") {
    const std::string present = R"({
      "uni:log": { "uni:variants": [ "log_a" ],
                   "uni:params": {
                     "kind": { "uni:field": "card",
                               "uni:card_part": "kind" },
                     "plain": { "uni:field": "card" } } } })";
    LoadResult loaded = LoadMod(BindingFile(
        "my_mod:shown", "my_mod:apple_eaten",
        "[ { \"uni:present\": " + present + " } ]"));
    REQUIRE(loaded.ok());
    const auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    CHECK(params.at("kind").card_part == "kind");
    CHECK(params.at("plain").card_part.empty());
}

TEST_CASE("binding validation: a card part reads a card from a field") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    params["count"].card_part = "kind";
    CHECK(Validate(loaded).empty());
    params["count"].card_part = "color";
    CHECK(Validate(loaded).empty());
    params["count"].card_part = "shape";
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}

TEST_CASE("binding validation: a card part needs a field source") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& param = loaded.mods[0].bindings[0].cases[0].log->params["count"];
    param.card_part = "kind";
    param.field.clear();
    param.value = "x";
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}

TEST_CASE("binding validation: a card part needs no actor or streak") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& param = loaded.mods[0].bindings[0].cases[0].log->params["count"];
    param.card_part = "kind";
    param.field.clear();
    param.actor = true;
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
    param.actor = false;
    param.streak = "uni:skip_run";
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}

TEST_CASE("binding validation: the schema rejects a non-string card part") {
    LoadResult loaded = LoadApple(
        "[]", R"({ "uni:log": { "uni:variants": [ "log_a" ],
                     "uni:params": { "c": { "uni:field": "count",
                                            "uni:card_part": 3 } } } })");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "schema.invalid"));
}

TEST_CASE("binding validation: numeric words need a number value") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "gt", "uni:field": "count", "uni:value": 2 } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    loaded.mods[0].bindings[0].cases[0].tests[0].value = "two";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: a test reads a field or a streak, not both") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "gte", "uni:streak": "uni:skip_run",
               "uni:value": 2 } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    test.field = "count";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.field.clear();
    test.key = "a";
    test.key_field = "target";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: an actor test has no other source") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:actor": true } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    test.field = "count";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.field.clear();
    test.streak = "uni:skip_run";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.streak.clear();
    CHECK(Validate(loaded).empty());
}

TEST_CASE("binding validation: only exists, equals and in read the actor") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "equals", "uni:actor": true,
               "uni:value": "ann" } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    test.test = "in";
    test.values = nlohmann::json::array({"ann"});
    CHECK(Validate(loaded).empty());
    test.test = "gte";
    test.value = 1;
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.test = "has_tag";
    test.tag = "my_mod:fruit";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: caused_by needs a known event id value") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "caused_by", "uni:value": "card_played",
               "uni:tag": "wild" } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    test.tag.clear();
    CHECK(Validate(loaded).empty());
    test.value = "no_such_event";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.value = 3;
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.value = "";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.value = nlohmann::json();
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: caused_by may name an own or foreign event") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "caused_by", "uni:value": "my_mod:apple_eaten" } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    test.value = "other_mod:thing";
    CHECK_FALSE(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: a tag belongs to has_tag and caused_by only") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "equals", "uni:field": "count",
               "uni:value": 1, "uni:tag": "my_mod:fruit" } ])");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: caused_by has no other source") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "caused_by", "uni:value": "card_played" } ])");
    REQUIRE(loaded.ok());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    test.field = "count";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.field.clear();
    test.streak = "uni:skip_run";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.streak.clear();
    test.key = "ann";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.key.clear();
    test.state = "x";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.state.clear();
    test.actor = true;
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
    test.actor = false;
    CHECK(Validate(loaded).empty());
}

TEST_CASE("binding validation: a field test still needs its field") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    loaded.mods[0].bindings[0].cases[0].tests[0].field.clear();
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: select is all or first") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    loaded.mods[0].bindings[0].select = "first";
    CHECK(Validate(loaded).empty());
    loaded.mods[0].bindings[0].select = "some";
    CHECK(HasCheck(Validate(loaded), "binding.select"));
}

TEST_CASE("binding validation: a streak param has one source") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    params["count"] = match::modload::BindingParamDef{};
    params["count"].streak = "uni:skip_run";
    CHECK(Validate(loaded).empty());
    params["count"].field = "count";
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}

TEST_CASE("binding validation: a field test cannot take a streak key") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "exists", "uni:field": "count" } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    loaded.mods[0].bindings[0].cases[0].tests[0].key = "a";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: a field param cannot take a key") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    CHECK(Validate(loaded).empty());
    params["count"].key = "a";
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}

TEST_CASE("binding validation: a streak param takes key or key_field") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    params["count"] = match::modload::BindingParamDef{};
    params["count"].streak = "uni:skip_run";
    params["count"].key = "a";
    CHECK(Validate(loaded).empty());
    params["count"].key_field = "target";
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}

TEST_CASE("binding validation: a streak id must be well formed") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "gte", "uni:streak": "uni:skip_run",
               "uni:value": 1 } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    loaded.mods[0].bindings[0].cases[0].tests[0].streak = "bad id!";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: a has_tag test cannot read a streak") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& binding = loaded.mods[0].bindings[0];
    binding.event = "card_played";
    binding.cases[0].log->params.clear();
    match::modload::StreakTestDef test;
    test.test = "has_tag";
    test.tag = "my_mod:fruit";
    binding.cases[0].tests = {test};
    CHECK(Validate(loaded).empty());
    binding.cases[0].tests[0].streak = "uni:skip_run";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: a relation test cannot read a streak") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "relation", "uni:role": "actor",
               "uni:relation": "self" } ])");
    REQUIRE(loaded.ok());
    auto& tests = loaded.mods[0].bindings[0].cases[0].tests;
    CHECK(Validate(loaded).empty());
    tests[0].streak = "uni:skip_run";
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: a streak test key_field must be a field") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "gte", "uni:streak": "uni:skip_run",
               "uni:value": 2, "uni:key_field": "target" } ])");
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    loaded.mods[0].bindings[0].cases[0].tests[0].key_field = "nobody";
    CHECK(HasCheck(Validate(loaded), "binding.field.unknown"));
}

TEST_CASE("binding validation: a streak param key_field must be a field") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    params["count"] = match::modload::BindingParamDef{};
    params["count"].streak = "uni:skip_run";
    params["count"].key_field = "target";
    CHECK(Validate(loaded).empty());
    params["count"].key_field = "nobody";
    CHECK(HasCheck(Validate(loaded), "binding.field.unknown"));
}

TEST_CASE("binding validation: a streak in test needs values") {
    LoadResult loaded = LoadApple(
        R"([ { "uni:test": "in", "uni:streak": "uni:skip_run",
               "uni:values": [ 1, 2 ] } ])");
    REQUIRE(loaded.ok());
    auto& test = loaded.mods[0].bindings[0].cases[0].tests[0];
    REQUIRE(!test.values.empty());
    CHECK(Validate(loaded).empty());
    test.values.clear();
    CHECK(HasCheck(Validate(loaded), "binding.test.shape"));
}

TEST_CASE("binding validation: a streak param id must be well formed") {
    LoadResult loaded = LoadApple();
    REQUIRE(loaded.ok());
    auto& params = loaded.mods[0].bindings[0].cases[0].log->params;
    params["count"] = match::modload::BindingParamDef{};
    params["count"].streak = "uni:skip_run";
    CHECK(Validate(loaded).empty());
    params["count"].streak = "bad id!";
    CHECK(HasCheck(Validate(loaded), "binding.log.param"));
}
