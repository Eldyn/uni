#include <doctest/doctest.h>

#include <match/modload/derived_definition_keys.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/view/event_audience.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file derived_definitions_test.cpp
 * @brief The derived event file kind: loader, namespace rule, schema and key
 *        table, validator checks, and validation order.
 */

namespace fs = std::filesystem;
namespace keys = match::modload::derived_keys;

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
             / ("uni_derived_definitions_" + std::to_string(::getpid()) + "_"
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

std::string DerivedFile(const std::string& identifier,
                        const std::string& body) {
    return R"({
  "uni:format_version": "1.0.0",
  "uni:derived_event": {
    "uni:description": { "uni:identifier": ")" + identifier + R"(" },
    )" + body + R"(
  }
})";
}

const char* kWindowBody = R"(
    "uni:emits": "near_win_entered",
    "uni:open_on": [
      { "uni:event": "card_played", "uni:seat_field": "player",
        "uni:tests": [
          { "uni:test": "equals", "uni:state": "hand_size", "uni:value": 1 },
          { "uni:test": "has_tag", "uni:tag": "uni:draw_penalty",
            "uni:negate": true } ] } ],
    "uni:close_on": [
      { "uni:event": "cards_drawn", "uni:seat_field": "target" } ])";

const char* kBlockBody = R"(
    "uni:emits": "blocked",
    "uni:window_of": "uni:near_win_entered",
    "uni:block_on": [
      { "uni:event": "turn_skipped", "uni:seat_field": "target" } ])";

/** @brief A uni mod with a draw penalty card and the given derived files. */
LoadResult LoadUni(const std::vector<std::pair<std::string, std::string>>& files) {
    TempModsRoot root;
    root.Write("uni/mod.json", ManifestFor("uni"));
    root.Write("uni/kinds/penalty.json",
               R"({"format_version":"1.0.0","kind":{"identifier":"uni:penalty",)"
               R"("title_format":"P","face":{"kind":"blank"},)"
               R"("tags":["uni:draw_penalty"]}})");
    for (const auto& [name, contents] : files) {
        root.Write("uni/derived/" + name + ".json", contents);
    }
    return ScanModsDirectory(root.root.string());
}

std::vector<LoadError> Validate(const LoadResult& loaded) {
    match::modload::SemanticValidator validator(SchemaDir());
    return validator.ValidateMod(loaded.mods.at(0));
}

LoadResult LoadWindow(const std::string& body = kWindowBody) {
    return LoadUni({{"near_win_entered",
                     DerivedFile("uni:near_win_entered", body)}});
}

}  // namespace

TEST_CASE("derived definition: a window file loads with its fields") {
    const LoadResult loaded = LoadWindow();
    REQUIRE(loaded.ok());
    REQUIRE(loaded.mods.size() == 1);
    REQUIRE(loaded.mods[0].derived_events.size() == 1);
    const match::modload::DerivedEventDef& derived =
        loaded.mods[0].derived_events[0];
    CHECK(derived.derived_id == "uni:near_win_entered");
    CHECK(derived.namespace_id == "uni");
    CHECK(derived.id == "near_win_entered");
    CHECK(derived.emits == "near_win_entered");
    CHECK(derived.kind == "window");
    REQUIRE(derived.open_on.size() == 1);
    CHECK(derived.open_on[0].matcher.event == "card_played");
    CHECK(derived.open_on[0].seat_field == "player");
    REQUIRE(derived.open_on[0].matcher.tests.size() == 2);
    CHECK(derived.open_on[0].matcher.tests[0].state == "hand_size");
    CHECK(derived.open_on[0].matcher.tests[0].value == 1);
    CHECK(derived.open_on[0].matcher.tests[1].negate);
    REQUIRE(derived.close_on.size() == 1);
    CHECK(derived.close_on[0].seat_field == "target");
}

TEST_CASE("derived definition: a block file loads with its fields") {
    const LoadResult loaded =
        LoadUni({{"blocked", DerivedFile("uni:blocked", kBlockBody)}});
    REQUIRE(loaded.ok());
    const match::modload::DerivedEventDef& derived =
        loaded.mods[0].derived_events.at(0);
    CHECK(derived.kind == "block");
    CHECK(derived.emits == "blocked");
    CHECK(derived.window_of == "uni:near_win_entered");
    REQUIRE(derived.block_on.size() == 1);
    CHECK(derived.block_on[0].matcher.event == "turn_skipped");
}

TEST_CASE("derived namespace rule: only the uni mod defines derived events") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/derived/a.json",
               DerivedFile("my_mod:near_win_entered", kWindowBody));
    const LoadResult mod_own = ScanModsDirectory(root.root.string());
    CHECK_FALSE(mod_own.ok());
    CHECK(HasCheck(mod_own.errors, "derived.id.namespace"));

    const LoadResult foreign = [] {
        TempModsRoot other;
        other.Write("my_mod/mod.json", ManifestFor("my_mod"));
        other.Write("my_mod/derived/a.json",
                    DerivedFile("uni:near_win_entered", kWindowBody));
        return ScanModsDirectory(other.root.string());
    }();
    CHECK_FALSE(foreign.ok());
    CHECK(HasCheck(foreign.errors, "derived.id.namespace"));
}

TEST_CASE("derived file: a bad shape is a loader error") {
    const LoadResult no_version =
        LoadUni({{"a", R"({"uni:derived_event": {}})"}});
    CHECK_FALSE(no_version.ok());
    CHECK(HasCheck(no_version.errors, "derived.format_version"));

    const LoadResult no_emits = LoadWindow(R"(
      "uni:open_on": [ { "uni:event": "card_played",
                         "uni:seat_field": "player" } ])");
    CHECK_FALSE(no_emits.ok());
    CHECK(HasCheck(no_emits.errors, "derived.emits"));

    const LoadResult no_kind = LoadWindow(R"("uni:emits": "blocked")");
    CHECK_FALSE(no_kind.ok());
    CHECK(HasCheck(no_kind.errors, "derived.kind"));

    const LoadResult both_kinds = LoadWindow(R"(
      "uni:emits": "blocked",
      "uni:open_on": [ { "uni:event": "card_played",
                         "uni:seat_field": "player" } ],
      "uni:window_of": "uni:near_win_entered",
      "uni:block_on": [ { "uni:event": "turn_skipped",
                          "uni:seat_field": "target" } ])");
    CHECK_FALSE(both_kinds.ok());
    CHECK(HasCheck(both_kinds.errors, "derived.kind"));

    const LoadResult no_seat = LoadWindow(R"(
      "uni:emits": "near_win_entered",
      "uni:open_on": [ { "uni:event": "card_played" } ])");
    CHECK_FALSE(no_seat.ok());
    CHECK(HasCheck(no_seat.errors, "derived.matcher"));
}

TEST_CASE("derived definition: the schema and the key table agree") {
    std::ifstream in(fs::path(SchemaDir()) / "derived.schema.json");
    REQUIRE(in.is_open());
    const json schema = json::parse(in);
    const json& top = schema.at("properties");
    CHECK(top.contains("uni:format_version"));
    CHECK(top.contains(std::string(keys::kDerivedEvent)));
    const json& derived =
        top.at(std::string(keys::kDerivedEvent)).at("properties");
    for (const std::string_view key :
         {keys::kEmits, keys::kOpenOn, keys::kCloseOn, keys::kBlockOn,
          keys::kWindowOf}) {
        CHECK_MESSAGE(derived.contains(std::string(key)), key);
    }
    const json& seat_matcher = schema.at("$defs").at("seat_matcher");
    CHECK(seat_matcher.at("properties").contains(std::string(keys::kSeatField)));
}

TEST_CASE("derived validation: a valid pair passes") {
    const LoadResult loaded =
        LoadUni({{"near_win_entered",
                  DerivedFile("uni:near_win_entered", kWindowBody)},
                 {"blocked", DerivedFile("uni:blocked", kBlockBody)}});
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    for (const LoadError& error : errors) {
        MESSAGE(error.check << " " << error.path << " " << error.message);
    }
    CHECK(errors.empty());
}

TEST_CASE("derived validation: the file schema rejects an unknown key") {
    const LoadResult loaded = LoadWindow(R"(
      "uni:emits": "near_win_entered",
      "uni:open_on": [ { "uni:event": "card_played",
                         "uni:seat_field": "player" } ],
      "uni:surprise": 1)");
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "schema.invalid"));
}

TEST_CASE("derived validation: emits must be a derived core event") {
    LoadResult loaded = LoadWindow();
    REQUIRE(loaded.ok());
    loaded.mods[0].derived_events[0].emits = "card_played";
    CHECK(HasCheck(Validate(loaded), "derived.emits.unknown"));
    loaded.mods[0].derived_events[0].emits = "no_such_event";
    CHECK(HasCheck(Validate(loaded), "derived.emits.unknown"));
}

TEST_CASE("derived validation: two definitions cannot emit one event") {
    const LoadResult loaded = LoadUni(
        {{"near_win_entered", DerivedFile("uni:near_win_entered", kWindowBody)},
         {"again", DerivedFile("uni:again", kWindowBody)}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "derived.emits.duplicate"));
}

TEST_CASE("derived validation: a matched event must be a base event") {
    LoadResult loaded = LoadWindow();
    REQUIRE(loaded.ok());
    auto& definition = loaded.mods[0].derived_events[0];
    definition.open_on[0].matcher.event = "near_win_entered";
    CHECK(HasCheck(Validate(loaded), "derived.event.unknown"));
    definition.open_on[0].matcher.event = "blocked";
    CHECK(HasCheck(Validate(loaded), "derived.event.unknown"));
    definition.open_on[0].matcher.event = "no_such_event";
    CHECK(HasCheck(Validate(loaded), "derived.event.unknown"));
}

TEST_CASE("derived validation: the seat field must be a public payload "
          "field") {
    LoadResult loaded = LoadWindow();
    REQUIRE(loaded.ok());
    auto& definition = loaded.mods[0].derived_events[0];
    definition.open_on[0].seat_field = "nobody";
    CHECK(HasCheck(Validate(loaded), "derived.field.unknown"));
    definition.open_on[0].seat_field = "player";
    definition.close_on[0].matcher.event = "play_rejected";
    CHECK(HasCheck(Validate(loaded), "derived.event.unknown"));
}

TEST_CASE("derived validation: a hidden field is not readable") {
    LoadResult loaded = LoadWindow();
    REQUIRE(loaded.ok());
    auto& definition = loaded.mods[0].derived_events[0];
    // INFO: the `cards` field of cards_drawn has the audience owner.
    definition.close_on[0].seat_field = "cards";
    CHECK(HasCheck(Validate(loaded), "derived.field.audience"));
}

TEST_CASE("derived validation: a matcher test is checked as in a streak") {
    LoadResult loaded = LoadWindow();
    REQUIRE(loaded.ok());
    auto& tests = loaded.mods[0].derived_events[0].open_on[0].matcher.tests;
    tests[0].state = "no_such_state";
    CHECK(HasCheck(Validate(loaded), "derived.state.unknown"));
    tests[0].state = "hand_size";
    tests[1].tag = "uni:no_such_tag";
    CHECK(HasCheck(Validate(loaded), "derived.tag.unknown"));
    tests[1].tag = "uni:draw_penalty";
    tests[1].test = "relation";
    CHECK(HasCheck(Validate(loaded), "derived.test.relation"));
}

TEST_CASE("derived validation: a block names an existing window") {
    LoadResult loaded =
        LoadUni({{"near_win_entered",
                  DerivedFile("uni:near_win_entered", kWindowBody)},
                 {"blocked", DerivedFile("uni:blocked", kBlockBody)}});
    REQUIRE(loaded.ok());
    match::modload::DerivedEventDef* block = nullptr;
    for (auto& derived : loaded.mods[0].derived_events) {
        if (derived.kind == "block") block = &derived;
    }
    REQUIRE(block != nullptr);
    block->window_of = "uni:no_such_window";
    CHECK(HasCheck(Validate(loaded), "derived.window.unknown"));
    block->window_of = "uni:blocked";
    CHECK(HasCheck(Validate(loaded), "derived.window.unknown"));
}

TEST_CASE("derived validation: the stage comes after streaks and before "
          "presentations") {
    const std::vector<std::string_view> order(
        match::modload::kValidationStageOrder.begin(),
        match::modload::kValidationStageOrder.end());
    const std::vector<std::string_view> expected = {
        "uni", "events", "streaks", "derived", "presentations", "texts"};
    CHECK(order == expected);
}

TEST_CASE("derived validation: a streak error comes before a derived error") {
    TempModsRoot root;
    root.Write("uni/mod.json", ManifestFor("uni"));
    root.Write("uni/streaks/s.json", R"({
  "uni:format_version": "1.0.0",
  "uni:streak": {
    "uni:description": { "uni:identifier": "uni:s" },
    "uni:type": "total",
    "uni:matcher": { "uni:event": "no_such_event" } } })");
    root.Write("uni/derived/d.json",
               DerivedFile("uni:near_win_entered", R"(
      "uni:emits": "no_such_event",
      "uni:open_on": [ { "uni:event": "card_played",
                         "uni:seat_field": "player" } ])"));
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CHECK(HasCheck(errors, "streak.event.unknown"));
    CHECK_FALSE(HasCheck(errors, "derived.emits.unknown"));
}

TEST_CASE("derived validation: a derived error comes before a presentation "
          "error") {
    TempModsRoot root;
    root.Write("uni/mod.json", ManifestFor("uni"));
    root.Write("uni/derived/d.json",
               DerivedFile("uni:near_win_entered", R"(
      "uni:emits": "no_such_event",
      "uni:open_on": [ { "uni:event": "card_played",
                         "uni:seat_field": "player" } ])"));
    root.Write("uni/presentations/p.json",
               R"({"id":"p","target":"hand","modifiers":[)"
               R"({"kind":"no.such.kind"}]})");
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CHECK(HasCheck(errors, "derived.emits.unknown"));
    CHECK_FALSE(HasCheck(errors, "presentation.kind.unknown"));
}

TEST_CASE("derived events: the audience table lists them with the log "
          "retention") {
    for (const char* event : {"near_win_entered", "blocked"}) {
        const match::view::EventRule* rule = match::view::FindEventRule(event);
        REQUIRE_MESSAGE(rule != nullptr, event);
        CHECK(rule->retention == match::view::Retention::kLog);
        CHECK(match::view::AudienceOf(event, "target")
              == match::view::Audience::kAll);
        CHECK(match::view::IsDerivedEvent(event));
        CHECK_FALSE(match::view::IsStreakFeedable(event));
    }
    CHECK_FALSE(match::view::IsDerivedEvent("card_played"));
}
