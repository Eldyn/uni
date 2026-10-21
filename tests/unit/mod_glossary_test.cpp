#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/modload/glossary_entries.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/view/defs_builder.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file mod_glossary_test.cpp
 * @brief The glossary entry file kind: the key pattern, the loader, the
 *        namespace rule, the automatic entries of the cards, the override
 *        rule, and the validator checks.
 */

namespace fs = std::filesystem;
namespace keys = match::modload::glossary_keys;

using match::modload::GlossaryEntryView;
using match::modload::GlossarySource;
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
             / ("uni_glossary_" + std::to_string(::getpid()) + "_"
                + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempModsRoot() { fs::remove_all(root); }

    void Write(const std::string& relative,
               const std::string& contents) const {
        const fs::path file = root / relative;
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << contents;
    }
};

std::string ManifestFor(const std::string& id) {
    return R"({"id":")" + id + R"(","name":"t","version":"1","api":"1"})";
}

std::string EntryFile(const std::string& identifier,
                      const std::string& body = R"(
      "uni:label_key": "my_mod:foo.label",
      "uni:description_key": "my_mod:foo.description")") {
    return R"({
  "uni:format_version": "1.0.0",
  "uni:glossary_entry": {
    "uni:description": { "uni:identifier": ")" + identifier + R"(" },)"
           + body + R"(
  }
})";
}

std::string CardFile(const std::string& id, const std::string& extra = "") {
    return R"({ "format_version": "1.0.0", "kind": { "identifier": "my_mod:)" + id +
           R"(", "title_format": "T", "face": { "kind": "blank" })" + extra + " } }";
}

const char* kEnglish = R"({
  "my_mod:foo.label": "Foo",
  "my_mod:foo.description": "Foo does a thing."
})";

using Files = std::vector<std::pair<std::string, std::string>>;

LoadResult LoadMod(const Files& files, const std::string& mod_id = "my_mod") {
    TempModsRoot root;
    root.Write(mod_id + "/mod.json", ManifestFor(mod_id));
    for (const auto& [name, contents] : files) {
        root.Write(mod_id + "/" + name, contents);
    }
    return ScanModsDirectory(root.root.string());
}

std::vector<LoadError> Validate(const LoadResult& loaded) {
    match::modload::SemanticValidator validator(SchemaDir());
    return validator.ValidateMod(loaded.mods.at(0));
}

std::vector<LoadWarning> Warnings(const LoadResult& loaded) {
    match::modload::SemanticValidator validator(SchemaDir());
    return validator.WarningsOf(loaded.mods.at(0));
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

std::string Describe(const std::vector<LoadError>& errors) {
    std::string text;
    for (const LoadError& e : errors) text += e.check + ": " + e.message + "\n";
    return text;
}

const GlossaryEntryView* Find(const std::vector<GlossaryEntryView>& entries,
                              const std::string& id) {
    for (const GlossaryEntryView& entry : entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

fs::path FixtureRoot() {
    return ProjectRoot() / "tests" / "fixtures" / "golden_glossary";
}

void CheckGolden(const fs::path& golden, const json& actual) {
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(2) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}

json BuildDefs(const LoadResult& loaded) {
    auto content = std::make_shared<match::engine::AssemblyContent>();
    for (const auto& mod : loaded.mods) {
        content->mods.push_back({mod.manifest.id, mod.manifest.version});
        content->kinds_by_mod.emplace_back();
    }
    match::engine::MatchRegistries registries;
    registries.content = std::move(content);
    return match::view::DefsBuilder::Build(registries, loaded.mods);
}

}  // namespace

// --- key pattern -----------------------------------------------------------

TEST_CASE("glossary key: the pattern is namespace, colon, dotted words") {
    using match::modload::IsValidTextKey;
    for (const char* key :
         {"my_mod:foo", "my_mod:foo.label", "a:b.c.d", "uni:draw.title",
          "mod_2:card_9.description"}) {
        CHECK_MESSAGE(IsValidTextKey(key), key);
    }
    for (const char* key :
         {"", "foo", "my_mod:", ":foo", "My_mod:foo", "my_mod:Foo",
          "my_mod:foo bar", "my_mod:foo.", "my_mod:.foo", "my_mod:foo..bar",
          "my_mod:foo:bar", "my_mod:foo-bar", "Draw two cards.",
          "my_mod:foo\n", "my_mod:fo\xC3\xB2"}) {
        CAPTURE(key);
        CHECK_FALSE(IsValidTextKey(key));
    }
}

TEST_CASE("glossary key: the length limit applies") {
    using match::modload::IsValidTextKey;
    const std::string prefix = "my_mod:";
    CHECK(IsValidTextKey(
        prefix + std::string(keys::kMaxKeyBytes - prefix.size(), 'a')));
    CHECK_FALSE(IsValidTextKey(
        prefix + std::string(keys::kMaxKeyBytes - prefix.size() + 1, 'a')));
    CHECK_FALSE(IsValidTextKey(std::string(33, 'a') + ":foo"));
}

// --- the file kind: loader ---------------------------------------------------

TEST_CASE("glossary file: the loader reads one explicit entry") {
    const LoadResult loaded =
        LoadMod({{"glossary/foo.json", EntryFile("my_mod:foo")}});
    REQUIRE(loaded.ok());
    const auto& entries = loaded.mods.at(0).glossary;
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].entry_id == "my_mod:foo");
    CHECK(entries[0].namespace_id == "my_mod");
    CHECK(entries[0].label_key == "my_mod:foo.label");
    CHECK(entries[0].description_key == "my_mod:foo.description");
}

TEST_CASE("glossary file: a mod without a glossary folder has no entry") {
    const LoadResult loaded = LoadMod({});
    REQUIRE(loaded.ok());
    CHECK(loaded.mods.at(0).glossary.empty());
}

TEST_CASE("glossary file: an entry of another namespace is rejected") {
    const LoadResult loaded =
        LoadMod({{"glossary/foo.json", EntryFile("other_mod:foo")}});
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "glossary.id.namespace"));
}

TEST_CASE("glossary file: a uni entry from another mod is rejected") {
    const LoadResult loaded =
        LoadMod({{"glossary/draw.json", EntryFile("uni:draw")}});
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "glossary.id.namespace"));
}

TEST_CASE("glossary file: the uni mod defines uni entries") {
    const LoadResult loaded = LoadMod(
        {{"glossary/draw.json",
          EntryFile("uni:draw", R"(
      "uni:label_key": "uni:draw.label",
      "uni:description_key": "uni:draw.description")")}},
        "uni");
    CHECK(loaded.ok());
}

TEST_CASE("glossary file: the uni mod cannot define an entry of a mod") {
    const LoadResult loaded =
        LoadMod({{"glossary/foo.json", EntryFile("my_mod:foo")}}, "uni");
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "glossary.id.namespace"));
}

TEST_CASE("glossary file: a duplicate entry id is rejected") {
    const LoadResult loaded =
        LoadMod({{"glossary/one.json", EntryFile("my_mod:foo")},
                 {"glossary/two.json", EntryFile("my_mod:foo")}});
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "glossary.id.duplicate"));
}

TEST_CASE("glossary file: a missing key field is rejected") {
    const LoadResult no_description = LoadMod(
        {{"glossary/foo.json",
          EntryFile("my_mod:foo", R"("uni:label_key": "my_mod:foo.label")")}});
    CHECK(HasCheck(no_description.errors, "glossary.keys"));
    const LoadResult no_label = LoadMod(
        {{"glossary/foo.json",
          EntryFile("my_mod:foo",
                    R"("uni:description_key": "my_mod:foo.description")")}});
    CHECK(HasCheck(no_label.errors, "glossary.keys"));
}

TEST_CASE("glossary file: a key field that is no string is rejected") {
    const LoadResult loaded = LoadMod(
        {{"glossary/foo.json", EntryFile("my_mod:foo", R"(
      "uni:label_key": 5,
      "uni:description_key": "my_mod:foo.description")")}});
    CHECK(HasCheck(loaded.errors, "glossary.keys"));
}

TEST_CASE("glossary file: a wrong format version is rejected") {
    const LoadResult loaded = LoadMod({{"glossary/foo.json", R"({
  "uni:format_version": "2.0.0",
  "uni:glossary_entry": {
    "uni:description": { "uni:identifier": "my_mod:foo" },
    "uni:label_key": "my_mod:foo.label",
    "uni:description_key": "my_mod:foo.description"
  }
})"}});
    CHECK(HasCheck(loaded.errors, "glossary.format_version"));
}

TEST_CASE("glossary file: a file without the entry object is rejected") {
    const LoadResult loaded = LoadMod(
        {{"glossary/foo.json", R"({ "uni:format_version": "1.0.0" })"}});
    CHECK(HasCheck(loaded.errors, "glossary.shape"));
    const LoadResult not_object =
        LoadMod({{"glossary/foo.json", R"([1, 2])"}});
    CHECK(HasCheck(not_object.errors, "glossary.shape"));
}

TEST_CASE("glossary file: an entry without an identifier is rejected") {
    const LoadResult loaded = LoadMod({{"glossary/foo.json", R"({
  "uni:format_version": "1.0.0",
  "uni:glossary_entry": {
    "uni:label_key": "my_mod:foo.label",
    "uni:description_key": "my_mod:foo.description"
  }
})"}});
    CHECK(HasCheck(loaded.errors, "glossary.identifier"));
}

TEST_CASE("glossary file: a glossary file changes the content hash") {
    const LoadResult without = LoadMod({});
    const LoadResult with =
        LoadMod({{"glossary/foo.json", EntryFile("my_mod:foo")}});
    CHECK(without.mods.at(0).content_hash != with.mods.at(0).content_hash);
}

TEST_CASE("glossary file: the schema and the key table agree") {
    std::ifstream in(fs::path(SchemaDir()) / "glossary.schema.json");
    REQUIRE(in.is_open());
    const json schema = json::parse(in);
    const json& top = schema.at("properties");
    CHECK(top.contains(std::string(keys::kFormatVersion)));
    CHECK(top.contains(std::string(keys::kGlossaryEntry)));
    const json& entry =
        top.at(std::string(keys::kGlossaryEntry)).at("properties");
    for (const std::string_view key :
         {keys::kDescription, keys::kLabelKey, keys::kDescriptionKey}) {
        CHECK_MESSAGE(entry.contains(std::string(key)), key);
    }
}

TEST_CASE("glossary file: the schema rejects an unknown key") {
    const LoadResult loaded = LoadMod(
        {{"glossary/foo.json",
          EntryFile("my_mod:foo", R"(
      "uni:label_key": "my_mod:foo.label",
      "uni:description_key": "my_mod:foo.description",
      "uni:surprise": 1)")},
         {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "schema.invalid"));
}

// --- card files ------------------------------------------------------------

TEST_CASE("glossary card: a card reads its optional key fields") {
    const LoadResult loaded = LoadMod(
        {{"kinds/foo.json",
          CardFile("foo", R"(, "label_key": "my_mod:foo.name",
                          "description_key": "my_mod:foo.text")")}});
    REQUIRE(loaded.ok());
    CHECK(loaded.mods.at(0).cards.at(0).label_key == "my_mod:foo.name");
    CHECK(loaded.mods.at(0).cards.at(0).description_key == "my_mod:foo.text");
}

TEST_CASE("glossary card: a key field that is no string is rejected") {
    const LoadResult loaded = LoadMod(
        {{"kinds/foo.json", CardFile("foo", R"(, "label_key": 7)")}});
    CHECK_FALSE(loaded.ok());
    CHECK(HasCheck(loaded.errors, "card_schema"));
}

// --- automatic entries and the override rule --------------------------------

TEST_CASE("glossary entries: each card creates an entry with derived keys") {
    const LoadResult loaded = LoadMod({{"kinds/foo.json", CardFile("foo")},
                                       {"kinds/bar.json", CardFile("bar")}});
    REQUIRE(loaded.ok());
    const auto entries = match::modload::BuildGlossaryEntries(
        loaded.mods[0], {loaded.mods[0].manifest.id});
    REQUIRE(entries.size() == 2);
    const GlossaryEntryView* foo = Find(entries, "my_mod:foo");
    REQUIRE(foo != nullptr);
    CHECK(foo->label_key == "my_mod:foo.label");
    CHECK(foo->description_key == "my_mod:foo.description");
    CHECK(foo->source == GlossarySource::kCard);
    CHECK(Find(entries, "my_mod:bar") != nullptr);
}

TEST_CASE("glossary entries: a card with declared keys uses them") {
    const LoadResult loaded = LoadMod(
        {{"kinds/foo.json",
          CardFile("foo", R"(, "label_key": "my_mod:name.foo")")}});
    REQUIRE(loaded.ok());
    const auto entries = match::modload::BuildGlossaryEntries(
        loaded.mods[0], {loaded.mods[0].manifest.id});
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].label_key == "my_mod:name.foo");
    CHECK(entries[0].description_key == "my_mod:foo.description");
}

TEST_CASE("glossary entries: an explicit entry overrides the automatic one") {
    const LoadResult loaded = LoadMod(
        {{"kinds/foo.json", CardFile("foo")},
         {"glossary/foo.json",
          EntryFile("my_mod:foo", R"(
      "uni:label_key": "my_mod:custom.label",
      "uni:description_key": "my_mod:custom.description")")}});
    REQUIRE(loaded.ok());
    const auto entries = match::modload::BuildGlossaryEntries(
        loaded.mods[0], {loaded.mods[0].manifest.id});
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].id == "my_mod:foo");
    CHECK(entries[0].label_key == "my_mod:custom.label");
    CHECK(entries[0].description_key == "my_mod:custom.description");
    CHECK(entries[0].source == GlossarySource::kExplicit);
}

TEST_CASE("glossary entries: an explicit entry needs no card") {
    const LoadResult loaded = LoadMod({{"kinds/foo.json", CardFile("foo")},
                                       {"glossary/combo.json",
                                        EntryFile("my_mod:combo")}});
    REQUIRE(loaded.ok());
    const auto entries = match::modload::BuildGlossaryEntries(
        loaded.mods[0], {loaded.mods[0].manifest.id});
    REQUIRE(entries.size() == 2);
    CHECK(Find(entries, "my_mod:combo")->source == GlossarySource::kExplicit);
    CHECK(Find(entries, "my_mod:foo")->source == GlossarySource::kCard);
}

TEST_CASE("glossary entries: the list is sorted by id") {
    const LoadResult loaded =
        LoadMod({{"kinds/zeta.json", CardFile("zeta")},
                 {"kinds/alpha.json", CardFile("alpha")},
                 {"glossary/mid.json", EntryFile("my_mod:mid")}});
    REQUIRE(loaded.ok());
    const auto entries = match::modload::BuildGlossaryEntries(
        loaded.mods[0], {loaded.mods[0].manifest.id});
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].id == "my_mod:alpha");
    CHECK(entries[1].id == "my_mod:mid");
    CHECK(entries[2].id == "my_mod:zeta");
}

TEST_CASE("glossary entries: a mod without a card or a file has none") {
    const LoadResult loaded = LoadMod({});
    REQUIRE(loaded.ok());
    CHECK(match::modload::BuildGlossaryEntries(
              loaded.mods[0], {loaded.mods[0].manifest.id})
              .empty());
}

TEST_CASE("glossary entries: the source names are card and explicit") {
    CHECK(std::string(match::modload::GlossarySourceName(
              GlossarySource::kCard)) == "card");
    CHECK(std::string(match::modload::GlossarySourceName(
              GlossarySource::kExplicit)) == "explicit");
}

// --- validator ------------------------------------------------------------

TEST_CASE("glossary validation: a valid explicit entry passes") {
    const LoadResult loaded =
        LoadMod({{"glossary/foo.json", EntryFile("my_mod:foo")},
                 {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    CAPTURE(Describe(Validate(loaded)));
    CHECK(Validate(loaded).empty());
    CHECK(Warnings(loaded).empty());
}

TEST_CASE("glossary validation: a text in a key field is rejected") {
    for (const char* text :
         {"Draw two cards.", "my_mod:foo bar", "hello", "my_mod:foo\\n"}) {
        const LoadResult loaded = LoadMod(
            {{"glossary/foo.json",
              EntryFile("my_mod:foo",
                        std::string(R"("uni:label_key": ")") + text
                            + R"(", "uni:description_key": )"
                              R"("my_mod:foo.description")")},
             {"texts/en.json", kEnglish}});
        REQUIRE(loaded.ok());
        CAPTURE(text);
        CHECK(HasCheck(Validate(loaded), "glossary.key.syntax"));
    }
}

TEST_CASE("glossary validation: a description text is rejected too") {
    const LoadResult loaded = LoadMod(
        {{"glossary/foo.json", EntryFile("my_mod:foo", R"(
      "uni:label_key": "my_mod:foo.label",
      "uni:description_key": "Draw two cards and skip.")")},
         {"texts/en.json", kEnglish}});
    CHECK(HasCheck(Validate(loaded), "glossary.key.syntax"));
}

TEST_CASE("glossary validation: a key over the length limit is rejected") {
    const std::string long_key =
        "my_mod:" + std::string(keys::kMaxKeyBytes, 'a');
    const LoadResult loaded = LoadMod(
        {{"glossary/foo.json",
          EntryFile("my_mod:foo",
                    R"("uni:label_key": ")" + long_key
                        + R"(", "uni:description_key": )"
                          R"("my_mod:foo.description")")},
         {"texts/en.json", kEnglish}});
    CHECK(HasCheck(Validate(loaded), "glossary.key.syntax"));
}

TEST_CASE("glossary validation: a key of another namespace is rejected") {
    const LoadResult loaded = LoadMod(
        {{"glossary/foo.json", EntryFile("my_mod:foo", R"(
      "uni:label_key": "other_mod:foo.label",
      "uni:description_key": "uni:draw.description")")},
         {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CHECK(HasCheck(errors, "glossary.key.namespace"));
    CHECK(std::count_if(errors.begin(), errors.end(), [](const LoadError& e) {
              return e.check == "glossary.key.namespace";
          }) == 2);
}

TEST_CASE("glossary validation: a key missing in en rejects the mod") {
    const LoadResult missing_label = LoadMod(
        {{"glossary/foo.json", EntryFile("my_mod:foo")},
         {"texts/en.json", R"({ "my_mod:foo.description": "D" })"}});
    REQUIRE(missing_label.ok());
    const auto label_errors = Validate(missing_label);
    CHECK(HasCheck(label_errors, "texts.key.missing"));
    const LoadResult missing_description = LoadMod(
        {{"glossary/foo.json", EntryFile("my_mod:foo")},
         {"texts/en.json", R"({ "my_mod:foo.label": "L" })"}});
    CHECK(HasCheck(Validate(missing_description), "texts.key.missing"));
    const LoadResult no_texts =
        LoadMod({{"glossary/foo.json", EntryFile("my_mod:foo")}});
    CHECK(HasCheck(Validate(no_texts), "texts.key.missing"));
}

TEST_CASE("glossary validation: a key missing in it only warns") {
    const LoadResult loaded = LoadMod(
        {{"glossary/foo.json", EntryFile("my_mod:foo")},
         {"texts/en.json", kEnglish},
         {"texts/it.json", R"({ "my_mod:foo.label": "Pippo" })"}});
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    const auto warnings = Warnings(loaded);
    REQUIRE(HasWarning(warnings, "texts.key.missing.locale"));
    CHECK(warnings.size() == 1);
    CHECK(warnings[0].message.find("foo.description") != std::string::npos);
}

TEST_CASE("glossary validation: a card with declared keys needs them in en") {
    const LoadResult loaded = LoadMod(
        {{"kinds/foo.json",
          CardFile("foo", R"(, "label_key": "my_mod:name.foo")")},
         {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    REQUIRE(HasCheck(errors, "texts.key.missing"));
    CHECK(errors.at(0).message.find("my_mod:name.foo") != std::string::npos);
}

TEST_CASE("glossary validation: declared card keys that are no keys fail") {
    const LoadResult loaded = LoadMod(
        {{"kinds/foo.json",
          CardFile("foo", R"(, "description_key": "Draw two.")")}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "glossary.key.syntax"));
}

TEST_CASE("glossary validation: an overridden card key is not used") {
    const LoadResult loaded = LoadMod(
        {{"kinds/foo.json",
          CardFile("foo", R"(, "label_key": "my_mod:name.foo")")},
         {"glossary/foo.json", EntryFile("my_mod:foo")},
         {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
}

TEST_CASE("glossary validation: derived card keys are no error") {
    const LoadResult loaded = LoadMod({{"kinds/foo.json", CardFile("foo")}});
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    CHECK(Warnings(loaded).empty());
}

TEST_CASE("glossary validation: a derived key missing in en only warns") {
    const LoadResult loaded =
        LoadMod({{"kinds/foo.json", CardFile("foo")},
                 {"texts/en.json", R"({ "my_mod:other": "x" })"}});
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    const auto warnings = Warnings(loaded);
    CHECK(std::count_if(warnings.begin(), warnings.end(),
                        [](const LoadWarning& w) {
                            return w.check == "glossary.auto.key.missing";
                        })
          == 2);
}

TEST_CASE("glossary validation: derived keys present in en do not warn") {
    const LoadResult loaded = LoadMod({{"kinds/foo.json", CardFile("foo")},
                                       {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    CHECK(Warnings(loaded).empty());
}

TEST_CASE("glossary validation: the glossary keys join the used keys") {
    const LoadResult loaded =
        LoadMod({{"glossary/foo.json", EntryFile("my_mod:foo")},
                 {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    CHECK(match::modload::UsedGlossaryKeys(
              loaded.mods[0], {loaded.mods[0].manifest.id})
          == std::set<std::string>{"my_mod:foo.description",
                                   "my_mod:foo.label"});
}

TEST_CASE("glossary validation: the load scan drops the mod") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/glossary/foo.json", EntryFile("my_mod:foo"));
    root.Write("my_mod/texts/en.json", R"({ "my_mod:other": "x" })");
    LoadResult loaded = ScanModsDirectory(root.root.string());
    match::modload::SemanticValidator validator(SchemaDir());
    match::modload::ValidateLoadedMods(loaded, validator);
    CHECK(loaded.mods.empty());
    CHECK(HasCheck(loaded.errors, "texts.key.missing"));
}

TEST_CASE("glossary file: the glossary folder is a known entry of the mod") {
    const LoadResult loaded =
        LoadMod({{"glossary/foo.json", EntryFile("my_mod:foo")},
                 {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    CHECK_FALSE(HasWarning(loaded.warnings, "mod.unknown_entry"));
}

// --- defs listing ----------------------------------------------------------

TEST_CASE("defs: a mod lists its glossary entries with keys and source") {
    const LoadResult loaded = LoadMod(
        {{"kinds/foo.json", CardFile("foo")},
         {"kinds/bar.json", CardFile("bar")},
         {"glossary/bar.json",
          EntryFile("my_mod:bar", R"(
      "uni:label_key": "my_mod:bar.custom",
      "uni:description_key": "my_mod:bar.custom_description")")},
         {"texts/en.json", kEnglish}});
    REQUIRE(loaded.ok());
    const json listing = BuildDefs(loaded)["mods"][0]["glossary"];
    REQUIRE(listing.size() == 2);
    CHECK(listing[0]
          == json{{"id", "my_mod:bar"},
                  {"label_key", "my_mod:bar.custom"},
                  {"description_key", "my_mod:bar.custom_description"},
                  {"source", "explicit"}});
    CHECK(listing[1] == json{{"id", "my_mod:foo"},
                             {"label_key", "my_mod:foo.label"},
                             {"description_key", "my_mod:foo.description"},
                             {"source", "card"}});
}

TEST_CASE("defs: a mod with no card and no glossary file has no listing") {
    const LoadResult loaded = LoadMod({});
    REQUIRE(loaded.ok());
    CHECK_FALSE(BuildDefs(loaded)["mods"][0].contains("glossary"));
}

TEST_CASE("defs: the listing holds no text and no other field") {
    const LoadResult loaded = LoadMod({{"kinds/foo.json", CardFile("foo")}});
    REQUIRE(loaded.ok());
    const json entry = BuildDefs(loaded)["mods"][0]["glossary"][0];
    std::set<std::string> names;
    for (const auto& item : entry.items()) names.insert(item.key());
    CHECK(names == std::set<std::string>{"id", "label_key", "description_key",
                                         "source"});
}

TEST_CASE("defs: a changed glossary entry changes the digest") {
    const LoadResult first = LoadMod({{"kinds/foo.json", CardFile("foo")}});
    const LoadResult second = LoadMod(
        {{"kinds/foo.json",
          CardFile("foo", R"(, "label_key": "my_mod:other")")}});
    CHECK(BuildDefs(first)["defs_digest"] != BuildDefs(second)["defs_digest"]);
}

// --- golden files ----------------------------------------------------------

TEST_CASE("glossary golden: the defs listing of the fixture mod") {
    const LoadResult loaded =
        ScanModsDirectory((FixtureRoot() / "mods").string());
    REQUIRE(loaded.ok());
    const json defs = BuildDefs(loaded);
    CheckGolden(FixtureRoot() / "defs_glossary.json",
                json{{"mods", defs["mods"]},
                     {"defs_digest", defs["defs_digest"]}});
}

TEST_CASE("glossary golden: the fixture mod validates clean") {
    const LoadResult loaded =
        ScanModsDirectory((FixtureRoot() / "mods").string());
    REQUIRE(loaded.ok());
    CAPTURE(Describe(Validate(loaded)));
    CHECK(Validate(loaded).empty());
    CHECK(Warnings(loaded).empty());
}

TEST_CASE("glossary golden: the validator reports of the bad fixtures") {
    const fs::path bad_root = FixtureRoot() / "bad_mods";
    LoadResult loaded = ScanModsDirectory(bad_root.string());
    match::modload::SemanticValidator validator(SchemaDir());
    match::modload::ValidateLoadedMods(loaded, validator);

    const std::string prefix = bad_root.string() + "/";
    const auto relative = [&](const std::string& path) {
        return path.rfind(prefix, 0) == 0 ? path.substr(prefix.size()) : path;
    };
    json reports = json::object();
    for (const auto& report : loaded.reports) {
        json errors = json::array();
        for (const LoadError& error : report.errors) {
            errors.push_back({{"check", error.check},
                              {"path", relative(error.path)},
                              {"message", error.message}});
        }
        json warnings = json::array();
        for (const LoadWarning& warning : report.warnings) {
            warnings.push_back({{"check", warning.check},
                                {"path", relative(warning.path)},
                                {"message", warning.message}});
        }
        reports[report.mod_id] = {{"loaded", report.ok},
                                  {"errors", errors},
                                  {"warnings", warnings}};
    }
    CheckGolden(FixtureRoot() / "validator_reports.json", reports);
}
