#include <doctest/doctest.h>

#include <match/modload/event_definition_keys.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>

#include <nlohmann/json.hpp>

#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file event_definitions_test.cpp
 * @brief The event definition file kind: loader, namespace rule, validation
 *        order, and the shape of the key table.
 */

namespace fs = std::filesystem;
namespace keys = match::modload::event_keys;

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
             / ("uni_event_definitions_" + std::to_string(::getpid()) + "_"
                + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempModsRoot() { fs::remove_all(root); }

    void Write(const std::string& relative, const std::string& contents) const {
        const fs::path file = root / relative;
        fs::create_directories(file.parent_path());
        std::ofstream out(file);
        out << contents;
    }
};

std::string ManifestFor(const std::string& id) {
    return R"({"id":")" + id + R"(","name":"t","version":"1","api":"1"})";
}

std::string EventFile(const std::string& identifier,
                      const std::string& extra_event_keys = "") {
    return R"({
  "uni:format_version": "1.0.0",
  "uni:event": {
    "uni:description": { "uni:identifier": ")" + identifier + R"(" },
    "uni:payload": {
      "uni:fields": {
        "target": { "uni:type": "seat", "uni:audience": "all" },
        "color": { "uni:type": "color", "uni:audience": "actor" },
        "secret": { "uni:type": "integer" }
      }
    }
    )" + extra_event_keys + R"(
  }
})";
}

bool HasCheck(const std::vector<LoadError>& errors, const std::string& check) {
    for (const LoadError& error : errors) {
        if (error.check == check) return true;
    }
    return false;
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

LoadResult LoadSingleMod(const std::string& mod_id,
                         const std::string& identifier) {
    TempModsRoot root;
    root.Write(mod_id + "/mod.json", ManifestFor(mod_id));
    root.Write(mod_id + "/events/apple_eaten.json", EventFile(identifier));
    return ScanModsDirectory(root.root.string());
}

}  // namespace

TEST_CASE("event definition: a version 1 file loads with its fields") {
    const LoadResult loaded = LoadSingleMod("my_mod", "my_mod:apple_eaten");
    REQUIRE(loaded.ok());
    REQUIRE(loaded.mods.size() == 1);
    REQUIRE(loaded.mods[0].events.size() == 1);
    const match::modload::EventDef& event = loaded.mods[0].events[0];
    CHECK(event.event_id == "my_mod:apple_eaten");
    CHECK(event.namespace_id == "my_mod");
    CHECK(event.retention == "log");
    REQUIRE(event.fields.size() == 3);
    CHECK(event.fields[0].name == "color");
    CHECK(event.fields[0].type == "color");
    CHECK(event.fields[0].audience == "actor");
    CHECK(event.fields[2].name == "target");
    CHECK(event.fields[2].audience == "all");
}

TEST_CASE("event definition: a field with no audience defaults to none") {
    const LoadResult loaded = LoadSingleMod("my_mod", "my_mod:apple_eaten");
    REQUIRE(loaded.ok());
    for (const auto& field : loaded.mods[0].events[0].fields) {
        if (field.name == "secret") CHECK(field.audience == "none");
    }
}

TEST_CASE("event definition: the file may set the retention") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/apple_eaten.json",
               EventFile("my_mod:apple_eaten",
                         R"(, "uni:retention": "until_turn_end")"));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.mods[0].events[0].retention == "until_turn_end");
}

TEST_CASE("event definition: old style files and a new style file share "
          "one mod") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/entities/dice.json",
               R"({"id":"dice","components":{"lifetime.ttl":{"seconds":3.0}}})");
    root.Write("my_mod/presentations/glow.json",
               R"({"id":"glow","target":"hand","modifiers":[)"
               R"({"kind":"face.override","source":"seeded-random-tile"}]})");
    root.Write("my_mod/events/apple_eaten.json",
               EventFile("my_mod:apple_eaten"));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.mods[0].entities.size() == 1);
    CHECK(loaded.mods[0].presentations.size() == 1);
    CHECK(loaded.mods[0].events.size() == 1);
    match::modload::SemanticValidator validator(SchemaDir());
    CHECK(validator.ValidateMod(loaded.mods[0]).empty());
}

TEST_CASE("namespace rule: a mod cannot define a uni id") {
    const LoadResult loaded = LoadSingleMod("my_mod", "uni:my_event");
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.mods.empty());
    REQUIRE(HasCheck(loaded.errors, "event.id.namespace"));
    bool names_the_rule = false;
    for (const LoadError& error : loaded.errors) {
        if (error.message.find("only the uni mod") != std::string::npos) {
            names_the_rule = true;
        }
    }
    CHECK(names_the_rule);
}

TEST_CASE("namespace rule: a mod cannot define an id in another namespace") {
    const LoadResult loaded = LoadSingleMod("my_mod", "other_mod:x");
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "event.id.namespace"));
}

TEST_CASE("namespace rule: the uni mod may define a uni id") {
    TempModsRoot root;
    root.Write("uni/mod.json", ManifestFor("uni"));
    root.Write("uni/events/apple_eaten.json", EventFile("uni:apple_eaten"));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    CHECK(loaded.ok());
}

TEST_CASE("namespace rule: an id with no namespace is rejected") {
    const LoadResult loaded = LoadSingleMod("my_mod", "cards_drawn");
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "event.id.namespace"));
}

TEST_CASE("namespace rule: only the uni folder can be the uni mod") {
    TempModsRoot root;
    root.Write("impostor/mod.json", ManifestFor("uni"));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "manifest.id.reserved"));
}

TEST_CASE("namespace rule: a restriction id in a foreign namespace is "
          "rejected, a reference to a uni id is allowed") {
    const auto restriction_rule = [](const std::string& restriction_id) {
        return R"({
  "id": "limit",
  "hooks": [{
    "on": "after:play",
    "nodes": [{
      "id": "n1",
      "op": "add_restriction",
      "args": { "entry_def": {
        "id": ")" + restriction_id + R"(",
        "phase": "play",
        "condition": { "outcome_is": { "key": "never" } }
      } }
    }]
  }]
})";
    };
    {
        TempModsRoot root;
        root.Write("my_mod/mod.json", ManifestFor("my_mod"));
        root.Write("my_mod/rules/limit.json", restriction_rule("uni:forged"));
        const LoadResult loaded = ScanModsDirectory(root.root.string());
        REQUIRE(loaded.ok());
        match::modload::SemanticValidator validator(SchemaDir());
        CHECK(HasCheck(validator.ValidateMod(loaded.mods[0]), "id.namespace"));
    }
    {
        TempModsRoot root;
        root.Write("my_mod/mod.json", ManifestFor("my_mod"));
        root.Write("my_mod/rules/limit.json", restriction_rule("my_mod:own"));
        const LoadResult loaded = ScanModsDirectory(root.root.string());
        REQUIRE(loaded.ok());
        match::modload::SemanticValidator validator(SchemaDir());
        CHECK_FALSE(
            HasCheck(validator.ValidateMod(loaded.mods[0]), "id.namespace"));
    }
}

TEST_CASE("namespace rule: the shipped mods, which reference uni ids, "
          "still load") {
    fs::path path(__FILE__);
    while (!path.empty() && !fs::is_directory(path / "mods" / "uni")) {
        path = path.parent_path();
    }
    const LoadResult loaded = ScanModsDirectory((path / "mods").string());
    CHECK(loaded.ok());
}

TEST_CASE("event definition: a bad format version, a missing identifier "
          "and a bad audience are rejected") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/a.json", R"({"uni:event":{}})");
    CHECK(HasCheck(ScanModsDirectory(root.root.string()).errors,
                   "event.format_version"));

    TempModsRoot second;
    second.Write("my_mod/mod.json", ManifestFor("my_mod"));
    second.Write("my_mod/events/a.json",
                 R"({"uni:format_version":"1.0.0","uni:event":{}})");
    CHECK(HasCheck(ScanModsDirectory(second.root.string()).errors,
                   "event.identifier"));

    TempModsRoot third;
    third.Write("my_mod/mod.json", ManifestFor("my_mod"));
    third.Write("my_mod/events/a.json", R"({
      "uni:format_version":"1.0.0",
      "uni:event":{
        "uni:description":{"uni:identifier":"my_mod:a"},
        "uni:payload":{"uni:fields":{
          "x":{"uni:type":"seat","uni:audience":"everybody"}}}}})");
    const LoadResult loaded = ScanModsDirectory(third.root.string());
    REQUIRE(loaded.ok());
    match::modload::SemanticValidator validator(SchemaDir());
    CHECK(HasCheck(validator.ValidateMod(loaded.mods[0]), "schema.invalid"));
}

TEST_CASE("event definition: the schema and the key table agree") {
    std::ifstream in(fs::path(SchemaDir()) / "events.schema.json");
    REQUIRE(in.is_open());
    const json schema = json::parse(in);
    const json& top = schema.at("properties");
    CHECK(top.contains(std::string(keys::kFormatVersion)));
    CHECK(top.contains(std::string(keys::kEvent)));
    const json& event = top.at(std::string(keys::kEvent)).at("properties");
    for (const std::string_view key :
         {keys::kDescription, keys::kRetention, keys::kPayload,
          keys::kContext}) {
        CHECK(event.contains(std::string(key)));
    }
}

TEST_CASE("validation order: uni, events, streaks, presentations, texts") {
    const std::vector<std::string_view> order(
        match::modload::kValidationStageOrder.begin(),
        match::modload::kValidationStageOrder.end());
    const std::vector<std::string_view> expected = {
        "uni", "events", "streaks", "presentations", "texts"};
    CHECK(order == expected);
}

TEST_CASE("validation order: an event error comes before a presentation "
          "error") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/a.json", EventFile("my_mod:a"));
    root.Write("my_mod/presentations/p.json",
               R"({"id":"p","target":"hand","modifiers":[)"
               R"({"kind":"no.such.kind"}]})");
    LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    match::modload::SemanticValidator validator(SchemaDir());
    // A presentation error alone is reported.
    const auto presentation_only = validator.ValidateMod(loaded.mods[0]);
    CHECK(HasCheck(presentation_only, "presentation.kind.unknown"));
    // With a broken event as well, the event error wins.
    loaded.mods[0].events[0].event_id = "my_mod_without_colon";
    const auto both = validator.ValidateMod(loaded.mods[0]);
    CHECK(HasCheck(both, "event.id.namespace"));
    CHECK_FALSE(HasCheck(both, "presentation.kind.unknown"));
}

TEST_CASE("validation order: the uni mod errors come first") {
    fs::path project = fs::path(__FILE__);
    while (!project.empty() && !fs::is_directory(project / "mods" / "uni")) {
        project = project.parent_path();
    }
    TempModsRoot root;
    fs::copy(project / "mods" / "uni", root.root / "uni",
             fs::copy_options::recursive);
    root.Write("uni/presentations/p.json",
               R"({"id":"p","target":"hand","modifiers":[)"
               R"({"kind":"no.such.kind"}]})");
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/presentations/q.json",
               R"({"id":"q","target":"hand","modifiers":[)"
               R"({"kind":"no.such.kind"}]})");
    LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    match::modload::DeckDef deck;
    for (const auto& mod : loaded.mods) {
        for (const auto& candidate : mod.decks) {
            if (candidate.deck_id == "uni:classic") deck = candidate;
        }
    }
    deck.mods = {"uni"};
    match::modload::SemanticValidator validator(SchemaDir());
    const auto errors = validator.ValidateMatchSet(loaded.mods, deck);
    REQUIRE_FALSE(errors.empty());
    CHECK(HasCheck(errors, "presentation.kind.unknown"));
    CHECK(errors.front().path.find("/uni/") != std::string::npos);
}

TEST_CASE("emit_event: the validator checks the event reference") {
    const auto rule_emitting = [](const std::string& event_id) {
        return R"({
  "id": "eat",
  "hooks": [{
    "on": "after:play",
    "nodes": [{
      "id": "n1",
      "op": "emit_event",
      "args": { "id": ")" + event_id + R"(" }
    }]
  }]
})";
    };
    const auto validate = [&](const std::string& event_id) {
        TempModsRoot root;
        root.Write("my_mod/mod.json", ManifestFor("my_mod"));
        root.Write("my_mod/events/apple_eaten.json",
                   EventFile("my_mod:apple_eaten"));
        root.Write("my_mod/rules/eat.json", rule_emitting(event_id));
        const LoadResult loaded = ScanModsDirectory(root.root.string());
        REQUIRE(loaded.ok());
        match::modload::SemanticValidator validator(SchemaDir());
        return validator.ValidateMod(loaded.mods[0]);
    };
    CHECK(validate("my_mod:apple_eaten").empty());
    CHECK(HasCheck(validate("my_mod:missing"), "ref.event"));
    CHECK(HasCheck(validate("uni:cards_drawn"), "ref.event.namespace"));
}

TEST_CASE("emit_signal: the validator checks the optional event "
          "reference") {
    const auto validate = [&](const std::string& signal_args) {
        const std::string rule = R"({
  "id": "eat",
  "hooks": [{
    "on": "after:play",
    "nodes": [{
      "id": "n1",
      "op": "emit_signal",
      "args": )" + signal_args + R"(
    }]
  }]
})";
        TempModsRoot root;
        root.Write("my_mod/mod.json", ManifestFor("my_mod"));
        root.Write("my_mod/events/apple_eaten.json",
                   EventFile("my_mod:apple_eaten"));
        root.Write("my_mod/rules/eat.json", rule);
        const LoadResult loaded = ScanModsDirectory(root.root.string());
        REQUIRE(loaded.ok());
        match::modload::SemanticValidator validator(SchemaDir());
        return validator.ValidateMod(loaded.mods[0]);
    };
    CHECK(validate(R"({"name":"ping"})").empty());
    CHECK(validate(R"({"name":"ping","event":"my_mod:apple_eaten",)"
                   R"("fields":{"count":1}})")
              .empty());
    CHECK(HasCheck(validate(R"({"name":"ping","event":"my_mod:missing"})"),
                   "ref.event"));
    CHECK(HasCheck(validate(R"({"name":"ping","event":"uni:cards_drawn"})"),
                   "ref.event.namespace"));
    CHECK(HasCheck(validate(R"({"name":"ping","event":5})"), "op.type"));
    CHECK(HasCheck(validate(R"({"name":"ping","fields":5})"), "op.type"));
}

TEST_CASE("emit_event: the shipped test mod validates") {
    fs::path project = fs::path(__FILE__);
    while (!project.empty() && !fs::is_directory(project / "mods" / "uni")) {
        project = project.parent_path();
    }
    const LoadResult loaded = ScanModsDirectory((project / "mods").string());
    REQUIRE(loaded.ok());
    match::modload::SemanticValidator validator(SchemaDir());
    for (const auto& mod : loaded.mods) {
        if (mod.manifest.id != "test") continue;
        CHECK(validator.ValidateMod(mod).empty());
        CHECK(mod.events.size() == 1);
    }
}

namespace {

std::string EventWithFields(int field_count) {
    json fields = json::object();
    for (int index = 0; index < field_count; ++index) {
        fields["f" + std::to_string(index)] = {{"uni:type", "integer"}};
    }
    json file = {
        {"uni:format_version", "1.0.0"},
        {"uni:event",
         {{"uni:description", {{"uni:identifier", "my_mod:many"}}},
          {"uni:payload", {{"uni:fields", fields}}}}}};
    return file.dump();
}

LoadResult LoadEventText(const std::string& text) {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/events/a.json", text);
    return ScanModsDirectory(root.root.string());
}

}  // namespace

TEST_CASE("event limits: the field count is capped by the loader") {
    CHECK(LoadEventText(EventWithFields(keys::kMaxEventFields)).ok());
    const LoadResult over =
        LoadEventText(EventWithFields(keys::kMaxEventFields + 1));
    CHECK_FALSE(over.ok());
    CHECK(HasCheck(over.errors, "event.fields.count"));
}

TEST_CASE("event roles: a field named target or owner must be a seat") {
    for (const std::string role : {"target", "owner"}) {
        const std::string text = R"({
  "uni:format_version": "1.0.0",
  "uni:event": {
    "uni:description": { "uni:identifier": "my_mod:a" },
    "uni:payload": { "uni:fields": {
      ")" + role + R"(": { "uni:type": "string", "uni:audience": "all" } } }
  }
})";
        const LoadResult loaded = LoadEventText(text);
        CHECK_FALSE(loaded.ok());
        CHECK(HasCheck(loaded.errors, "event.field.role_type"));
    }
}

TEST_CASE("event roles: the validator rejects a non-seat role field too") {
    LoadResult loaded = LoadSingleMod("my_mod", "my_mod:apple_eaten");
    REQUIRE(loaded.ok());
    for (auto& field : loaded.mods[0].events[0].fields) {
        if (field.name == "target") field.type = "string";
    }
    match::modload::SemanticValidator validator(SchemaDir());
    CHECK(HasCheck(validator.ValidateMod(loaded.mods[0]),
                   "event.field.role_type"));
}

TEST_CASE("event file: a wrong JSON type is an error, not ignored") {
    const auto with_field = [](const std::string& field_body) {
        return R"({"uni:format_version":"1.0.0","uni:event":{
          "uni:description":{"uni:identifier":"my_mod:a"},
          "uni:payload":{"uni:fields":{"x":)" + field_body + "}}}}";
    };
    CHECK(HasCheck(LoadEventText(with_field(R"({"uni:type":7})")).errors,
                   "event.field.type"));
    CHECK(HasCheck(
        LoadEventText(with_field(R"({"uni:type":"seat","uni:audience":1})"))
            .errors,
        "event.field.audience"));
    CHECK(HasCheck(LoadEventText(with_field("5")).errors, "event.field.shape"));
    const std::string bad_retention = R"({"uni:format_version":"1.0.0",
      "uni:event":{"uni:description":{"uni:identifier":"my_mod:a"},
      "uni:retention":3}})";
    CHECK(HasCheck(LoadEventText(bad_retention).errors, "event.retention"));
}

TEST_CASE("event file: the format version is parsed strictly") {
    const auto with_version = [](const std::string& version) {
        return R"({"uni:format_version":")" + version
               + R"(","uni:event":{"uni:description":)"
                 R"({"uni:identifier":"my_mod:a"}}})";
    };
    CHECK(LoadEventText(with_version("1")).ok());
    CHECK(LoadEventText(with_version("1.0.0")).ok());
    for (const std::string bad : {"1abc", "1.x", "2.0.0", "", "1..0", "1.0.0.0"}) {
        CAPTURE(bad);
        CHECK(HasCheck(LoadEventText(with_version(bad)).errors,
                       "event.format_version"));
    }
}

TEST_CASE("mod manifest: a version with a cache key separator is rejected") {
    for (const std::string version : {"1.0.0", "1.2.3-beta+5", "7"}) {
        TempModsRoot root;
        root.Write("my_mod/mod.json",
                   R"({"id":"my_mod","name":"t","version":")" + version
                       + R"(","api":"1"})");
        CHECK(ScanModsDirectory(root.root.string()).ok());
    }
    for (const std::string version : {"1|2", "1#a", "a b", ""}) {
        TempModsRoot root;
        root.Write("my_mod/mod.json",
                   R"({"id":"my_mod","name":"t","version":")" + version
                       + R"(","api":"1"})");
        CAPTURE(version);
        CHECK(HasCheck(ScanModsDirectory(root.root.string()).errors,
                       "manifest.version"));
    }
}

TEST_CASE("mod hash: a file that cannot be read rejects the mod") {
    if (::geteuid() == 0) return;
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/notes.txt", "secret");
    fs::permissions(root.root / "my_mod/notes.txt", fs::perms::none);
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    fs::permissions(root.root / "my_mod/notes.txt", fs::perms::owner_all);
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "mod.hash"));
    CHECK(loaded.mods.empty());
}

TEST_CASE("load validation: the ordered stages run on the loaded mods") {
    const auto load = [](const std::string& rule_event_id,
                         const std::string& audience) {
        TempModsRoot root;
        root.Write("my_mod/mod.json", ManifestFor("my_mod"));
        root.Write("my_mod/events/a.json",
                   R"({"uni:format_version":"1.0.0","uni:event":{
                     "uni:description":{"uni:identifier":"my_mod:a"},
                     "uni:payload":{"uni:fields":{"x":{"uni:type":"seat",
                     "uni:audience":")" + audience + R"("}}}}})");
        root.Write("my_mod/rules/r.json", R"({"id":"r","hooks":[{
          "on":"after:play","nodes":[{"id":"n1","op":"emit_event",
          "args":{"id":")" + rule_event_id + R"("}}]}]})");
        LoadResult result = ScanModsDirectory(root.root.string());
        REQUIRE(result.ok());
        match::modload::SemanticValidator validator(SchemaDir());
        match::modload::ValidateLoadedMods(result, validator);
        return result;
    };
    const LoadResult clean = load("my_mod:a", "all");
    CHECK(clean.ok());
    CHECK(clean.mods.size() == 1);

    const LoadResult bad_reference = load("my_mod:missing", "all");
    CHECK(bad_reference.mods.empty());
    CHECK(HasCheck(bad_reference.errors, "ref.event"));

    const LoadResult bad_audience = load("my_mod:a", "x");
    CHECK(bad_audience.mods.empty());
    CHECK(HasCheck(bad_audience.errors, "schema.invalid"));
    CHECK_FALSE(bad_audience.reports.empty());
    CHECK_FALSE(bad_audience.reports[0].ok);
}

TEST_CASE("load validation: a missing schema directory skips the stages") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    LoadResult result = ScanModsDirectory(root.root.string());
    match::modload::SemanticValidator validator("/nonexistent/schemas");
    match::modload::ValidateLoadedMods(result, validator);
    CHECK(result.mods.size() == 1);
}
