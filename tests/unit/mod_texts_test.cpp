#include <doctest/doctest.h>

#include <common/crypto_hash.hpp>
#include <match/content_wiring.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/modload/asset_index.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/modload/texts_limits.hpp>
#include <match/view/defs_builder.hpp>

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
 * @file mod_texts_test.cpp
 * @brief The `texts` file kind: loader, limits, namespace rule, used keys,
 *        the `en` rule, the asset slot, the `defs` listing, and the golden
 *        files.
 *
 * Set `UNI_UPDATE_GOLDEN=1` to rewrite the golden files. A normal run never
 * rewrites them.
 */

namespace fs = std::filesystem;
namespace limits = match::modload::texts_limits;

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

fs::path FixtureRoot() {
    return ProjectRoot() / "tests" / "fixtures" / "golden_mod_texts";
}

struct TempModsRoot {
    fs::path root;
    TempModsRoot() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_texts_" + std::to_string(::getpid()) + "_"
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

std::string LogBinding(const std::string& mod_id, const std::string& variants) {
    return R"({
  "uni:format_version": "1.0.0",
  "uni:binding": {
    "uni:description": { "uni:identifier": ")" + mod_id + R"(:shown" },
    "uni:event": "turn_skipped",
    "uni:cases": [ { "uni:tests": [],
      "uni:present": { "uni:log": { "uni:variants": )" + variants + R"( } } } ]
  }
})";
}

/** @brief A mod `my_mod` with one binding and the given texts files. */
LoadResult LoadMod(const std::string& variants,
                   const std::vector<std::pair<std::string, std::string>>&
                       texts_files,
                   const std::string& mod_id = "my_mod") {
    TempModsRoot root;
    root.Write(mod_id + "/mod.json", ManifestFor(mod_id));
    root.Write(mod_id + "/presentations/shown.json",
               LogBinding(mod_id, variants));
    for (const auto& [name, contents] : texts_files) {
        root.Write(mod_id + "/texts/" + name, contents);
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

bool HasLoadError(const std::vector<LoadError>& errors,
                  const std::string& check) {
    return HasCheck(errors, check);
}

std::string Describe(const std::vector<LoadError>& errors) {
    std::string text;
    for (const LoadError& e : errors) text += e.check + ": " + e.message + "\n";
    return text;
}

const char* kEnglish = R"({ "my_mod:greeting": "Hello" })";

void CheckGolden(const fs::path& golden, const json& actual) {
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(2) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}

}  // namespace

// --- locale tag ------------------------------------------------------------

TEST_CASE("texts: a locale tag has a language and optional subtags") {
    for (const char* tag : {"en", "it", "pt-BR", "zh-Hant-TW", "fil", "es-419"}) {
        CAPTURE(tag);
        CHECK(match::modload::IsValidLocaleTag(tag));
    }
    for (const char* tag : {"", "e", "english", "en_US", "en-", "-en",
                            "en--US", "EN", "en-toolongsubtag", "pseudo",
                            "../en", "en us"}) {
        CAPTURE(tag);
        CHECK_FALSE(match::modload::IsValidLocaleTag(tag));
    }
}

// --- loader ----------------------------------------------------------------

TEST_CASE("texts: the loader reads each locale file with its hash and size") {
    const std::string english = kEnglish;
    const LoadResult loaded = LoadMod(R"(["my_mod:greeting"])",
                                      {{"en.json", english},
                                       {"pt-BR.json", R"({"my_mod:greeting":"Oi"})"}});
    REQUIRE(loaded.ok());
    const auto& texts = loaded.mods.at(0).texts;
    REQUIRE(texts.size() == 2);
    CHECK(texts[0].locale == "en");
    CHECK(texts[1].locale == "pt-BR");
    CHECK(texts[0].size == english.size());
    CHECK(texts[0].hash == Sha256Hex(english));
    CHECK(texts[0].content.at("my_mod:greeting") == "Hello");
}

TEST_CASE("texts: the loader does not read a file over the size limit") {
    std::string over_limit = kEnglish;
    over_limit.resize(limits::kMaxTextsFileBytes + 1, ' ');
    const LoadResult loaded =
        LoadMod(R"(["log_a"])", {{"en.json", over_limit}});
    REQUIRE(loaded.ok());
    const auto& texts = loaded.mods.at(0).texts.at(0);
    CHECK(texts.size == over_limit.size());
    CHECK(texts.content.is_null());
    CHECK(texts.hash.empty());
    CHECK_FALSE(texts.read_error.empty());
}

TEST_CASE("texts: a symlink that leaves the mod folder is rejected") {
    TempModsRoot root;
    root.Write("mods/my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("mods/my_mod/presentations/shown.json",
               LogBinding("my_mod", R"(["my_mod:greeting"])"));
    root.Write("outside/secret.json", kEnglish);
    fs::create_directories(root.root / "mods" / "my_mod" / "texts");
    std::error_code link_error;
    fs::create_symlink(root.root / "outside" / "secret.json",
                       root.root / "mods" / "my_mod" / "texts" / "en.json",
                       link_error);
    if (link_error) {
        MESSAGE("skipped: this platform cannot make symlinks");
        return;
    }
    const LoadResult loaded =
        ScanModsDirectory((root.root / "mods").string());
    CHECK(loaded.mods.empty());
    CHECK(HasLoadError(loaded.errors, "texts.symlink"));
}

TEST_CASE("texts: a symlink inside the mod folder is rejected too") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/presentations/shown.json",
               LogBinding("my_mod", R"(["my_mod:greeting"])"));
    root.Write("my_mod/shared/english.json", kEnglish);
    fs::create_directories(root.root / "my_mod" / "texts");
    std::error_code link_error;
    fs::create_symlink(root.root / "my_mod" / "shared" / "english.json",
                       root.root / "my_mod" / "texts" / "en.json",
                       link_error);
    if (link_error) {
        MESSAGE("skipped: this platform cannot make symlinks");
        return;
    }
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    CHECK(loaded.mods.empty());
    CHECK(HasLoadError(loaded.errors, "texts.symlink"));
}

TEST_CASE("texts: a texts folder that leaves the mod folder is rejected") {
    TempModsRoot root;
    root.Write("mods/my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("mods/my_mod/presentations/shown.json",
               LogBinding("my_mod", R"(["my_mod:greeting"])"));
    root.Write("outside/texts/en.json", kEnglish);
    std::error_code link_error;
    fs::create_directory_symlink(root.root / "outside" / "texts",
                                 root.root / "mods" / "my_mod" / "texts",
                                 link_error);
    if (link_error) {
        MESSAGE("skipped: this platform cannot make symlinks");
        return;
    }
    const LoadResult loaded = ScanModsDirectory((root.root / "mods").string());
    CHECK(loaded.mods.empty());
    CHECK(HasLoadError(loaded.errors, "texts.symlink"));
}

TEST_CASE("texts: a texts file changes the content hash of the mod") {
    const LoadResult without = LoadMod(R"(["log_a"])", {});
    const LoadResult with = LoadMod(R"(["log_a"])", {{"en.json", "{}"}});
    CHECK(without.mods.at(0).content_hash != with.mods.at(0).content_hash);
}

TEST_CASE("texts: the texts folder is a known entry of the mod") {
    const LoadResult loaded = LoadMod(R"(["log_a"])", {{"en.json", "{}"}});
    CHECK_FALSE(HasWarning(loaded.warnings, "mod.unknown_entry"));
}

// --- validator: file rules -------------------------------------------------

TEST_CASE("texts: a valid mod has no error and no warning") {
    const LoadResult loaded = LoadMod(
        R"(["my_mod:greeting"])",
        {{"en.json", kEnglish}, {"it.json", R"({"my_mod:greeting":"Ciao"})"}});
    REQUIRE(loaded.ok());
    CAPTURE(Describe(Validate(loaded)));
    CHECK(Validate(loaded).empty());
    CHECK(Warnings(loaded).empty());
}

TEST_CASE("texts: malformed JSON is rejected") {
    const LoadResult loaded =
        LoadMod(R"(["log_a"])", {{"en.json", R"({"my_mod:a": )"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.json"));
}

TEST_CASE("texts: a file that is not a flat object is rejected") {
    const LoadResult loaded = LoadMod(R"(["log_a"])", {{"en.json", "[1,2]"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.json"));
}

TEST_CASE("texts: a value that is no string is rejected") {
    const LoadResult loaded = LoadMod(
        R"(["log_a"])", {{"en.json", R"({"my_mod:a": 5})"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.value"));
}

TEST_CASE("texts: a nested object is rejected") {
    const LoadResult loaded = LoadMod(
        R"(["log_a"])", {{"en.json", R"({"my_mod:a": {"b": "c"}})"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.value"));
}

TEST_CASE("texts: a value over the length limit is rejected") {
    const std::string at_limit(limits::kMaxTextValueBytes, 'a');
    const LoadResult accepted = LoadMod(
        R"(["log_a"])", {{"en.json", R"({"my_mod:a": ")" + at_limit + "\"}"}});
    CAPTURE(Describe(Validate(accepted)));
    CHECK(Validate(accepted).empty());

    const std::string over_limit(limits::kMaxTextValueBytes + 1, 'a');
    const LoadResult rejected = LoadMod(
        R"(["log_a"])", {{"en.json", R"({"my_mod:a": ")" + over_limit + "\"}"}});
    CHECK(HasCheck(Validate(rejected), "texts.value"));
}

TEST_CASE("texts: a file over the size limit is rejected") {
    std::string at_limit = kEnglish;
    at_limit.resize(limits::kMaxTextsFileBytes, ' ');
    const LoadResult accepted = LoadMod(R"(["log_a"])", {{"en.json", at_limit}});
    CAPTURE(Describe(Validate(accepted)));
    CHECK(Validate(accepted).empty());

    std::string over_limit = kEnglish;
    over_limit.resize(limits::kMaxTextsFileBytes + 1, ' ');
    const LoadResult rejected =
        LoadMod(R"(["log_a"])", {{"en.json", over_limit}});
    REQUIRE(rejected.ok());
    CHECK(HasCheck(Validate(rejected), "texts.size"));
}

namespace {

std::string FileWithKeys(std::size_t count) {
    json object = json::object();
    for (std::size_t i = 0; i < count; ++i) {
        object["my_mod:k" + std::to_string(i)] = "v";
    }
    return object.dump();
}

}  // namespace

TEST_CASE("texts: a file over the key limit is rejected") {
    const LoadResult accepted = LoadMod(
        R"(["log_a"])", {{"en.json", FileWithKeys(limits::kMaxTextKeys)}});
    CAPTURE(Describe(Validate(accepted)));
    CHECK(Validate(accepted).empty());

    const LoadResult rejected = LoadMod(
        R"(["log_a"])",
        {{"en.json", FileWithKeys(limits::kMaxTextKeys + 1)}});
    CHECK(HasCheck(Validate(rejected), "texts.keys"));
}

TEST_CASE("texts: a file name that is no locale tag is rejected") {
    const LoadResult loaded =
        LoadMod(R"(["log_a"])", {{"english!.json", "{}"}, {"en.json", "{}"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.locale"));
}

TEST_CASE("texts: the pseudo locale is made at run time and cannot ship") {
    const LoadResult loaded = LoadMod(R"(["log_a"])", {{"pseudo.json", "{}"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.locale"));
}

// --- validator: namespace --------------------------------------------------

TEST_CASE("texts: a key outside the namespace of the mod is rejected") {
    const LoadResult loaded = LoadMod(
        R"(["log_a"])", {{"en.json", R"({"other_mod:a": "x"})"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.key.namespace"));
}

TEST_CASE("texts: a mod cannot define a uni key") {
    const LoadResult loaded = LoadMod(
        R"(["log_a"])", {{"en.json", R"({"uni:glossary.zero": "x"})"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.key.namespace"));
}

TEST_CASE("texts: a key without a namespace is rejected") {
    const LoadResult loaded =
        LoadMod(R"(["log_a"])", {{"en.json", R"({"plain": "x"})"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.key.namespace"));
}

TEST_CASE("texts: the uni mod defines uni keys") {
    const LoadResult loaded = LoadMod(
        R"(["uni:greeting"])", {{"en.json", R"({"uni:greeting": "Hi"})"}},
        "uni");
    REQUIRE(loaded.ok());
    CAPTURE(Describe(Validate(loaded)));
    CHECK(Validate(loaded).empty());
}

// --- validator: used keys --------------------------------------------------

TEST_CASE("texts: a used key that is missing in en rejects the mod") {
    const LoadResult loaded = LoadMod(
        R"(["my_mod:greeting", "my_mod:farewell"])",
        {{"en.json", kEnglish}});
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    REQUIRE(HasCheck(errors, "texts.key.missing"));
    const auto missing = std::find_if(
        errors.begin(), errors.end(),
        [](const LoadError& e) { return e.check == "texts.key.missing"; });
    CHECK(missing->message.find("my_mod:farewell") != std::string::npos);
    CHECK(missing->message.find("my_mod:greeting") == std::string::npos);
}

TEST_CASE("texts: a mod with used keys and no en file is rejected") {
    const LoadResult loaded = LoadMod(
        R"(["my_mod:greeting"])", {{"it.json", R"({"my_mod:greeting":"Ciao"})"}});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.key.missing"));
}

TEST_CASE("texts: a mod with used keys and no texts is rejected") {
    const LoadResult loaded = LoadMod(R"(["my_mod:greeting"])", {});
    REQUIRE(loaded.ok());
    CHECK(HasCheck(Validate(loaded), "texts.key.missing"));
}

TEST_CASE("texts: the load scan drops the mod whose used key is missing") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/presentations/shown.json",
               LogBinding("my_mod", R"(["my_mod:missing"])"));
    root.Write("my_mod/texts/en.json", kEnglish);
    LoadResult loaded = ScanModsDirectory(root.root.string());
    match::modload::SemanticValidator validator(SchemaDir());
    match::modload::ValidateLoadedMods(loaded, validator);
    CHECK(loaded.mods.empty());
    CHECK(HasCheck(loaded.errors, "texts.key.missing"));
}

TEST_CASE("texts: a used key that is missing in another locale only warns") {
    const LoadResult loaded = LoadMod(
        R"(["my_mod:greeting"])",
        {{"en.json", kEnglish}, {"it.json", R"({"my_mod:other": "Altro"})"}});
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    const auto warnings = Warnings(loaded);
    REQUIRE(HasWarning(warnings, "texts.key.missing.locale"));
    const auto found = std::find_if(
        warnings.begin(), warnings.end(), [](const LoadWarning& w) {
            return w.check == "texts.key.missing.locale";
        });
    CHECK(found->message.find("'it'") != std::string::npos);
}

TEST_CASE("texts: a mod with no texts and no own used key loads clean") {
    const LoadResult loaded = LoadMod(R"(["log_a", "log_b"])", {});
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
    CHECK(Warnings(loaded).empty());
}

TEST_CASE("texts: a key of the core catalog is not checked") {
    const LoadResult loaded =
        LoadMod(R"(["log_a", "uni:log.skip"])", {{"en.json", kEnglish}});
    REQUIRE(loaded.ok());
    CHECK(Validate(loaded).empty());
}

TEST_CASE("texts: an earlier stage reports before the texts stage") {
    TempModsRoot root;
    root.Write("my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("my_mod/presentations/shown.json",
               LogBinding("my_mod", R"(["my_mod:missing"])"));
    root.Write("my_mod/presentations/broken.json", R"({
  "uni:format_version": "1.0.0",
  "uni:binding": {
    "uni:description": { "uni:identifier": "my_mod:broken" },
    "uni:event": "no_such_event",
    "uni:cases": [ { "uni:tests": [], "uni:present": {} } ]
  }
})");
    root.Write("my_mod/texts/en.json", kEnglish);
    const LoadResult loaded = ScanModsDirectory(root.root.string());
    REQUIRE(loaded.ok());
    const auto errors = Validate(loaded);
    CAPTURE(Describe(errors));
    CHECK(HasCheck(errors, "binding.event.unknown"));
    CHECK_FALSE(HasCheck(errors, "texts.key.missing"));
}

TEST_CASE("texts: the texts stage is the last stage") {
    CHECK(match::modload::kValidationStageOrder.back() == "texts");
}

// --- asset slot ------------------------------------------------------------

TEST_CASE("texts: the asset index serves a locale file by id and hash") {
    const LoadResult loaded = ScanModsDirectory((FixtureRoot() / "mods").string());
    REQUIRE(loaded.ok());
    std::ifstream input(FixtureRoot() / "mods" / "fixture_mod" / "texts"
                        / "it.json", std::ios::binary);
    const std::string italian((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    const auto index = match::modload::AssetIndex::Build(loaded);
    const auto* entry = index.Find("fixture_mod", "texts", "it",
                                   match::modload::AssetTier::kLow);
    REQUIRE(entry != nullptr);
    CHECK(entry->hash == match::modload::ShortContentHash(italian));
    CHECK(entry->content_type == "application/json");
    CHECK(entry->size == italian.size());
    CHECK(index.Resolve("fixture_mod", "texts", "it", "low", entry->hash)
          == entry);
    CHECK(index.Resolve("fixture_mod", "texts", "it", "low",
                        "0000000000000000")
          == nullptr);
    CHECK(match::modload::AssetIndex::Url(
              "fixture_mod", "texts", "it", match::modload::AssetTier::kLow,
              entry->hash)
          == "/assets/fixture_mod/texts/it/low/" + entry->hash);
}

TEST_CASE("texts: the index keeps the hash of the defs when the file changes") {
    TempModsRoot root;
    root.Write("mods/my_mod/mod.json", ManifestFor("my_mod"));
    root.Write("mods/my_mod/presentations/shown.json",
               LogBinding("my_mod", R"(["my_mod:greeting"])"));
    root.Write("mods/my_mod/texts/en.json", kEnglish);
    const LoadResult loaded = ScanModsDirectory((root.root / "mods").string());
    REQUIRE(loaded.ok());
    const auto& scanned = loaded.mods.at(0).texts.at(0);
    root.Write("mods/my_mod/texts/en.json",
               R"({ "my_mod:greeting": "Changed after the scan" })");
    const auto index = match::modload::AssetIndex::Build(loaded);
    const auto* entry = index.Find("my_mod", "texts", "en",
                                   match::modload::AssetTier::kLow);
    REQUIRE(entry != nullptr);
    CHECK(entry->hash == scanned.hash.substr(0, entry->hash.size()));
    CHECK(entry->size == scanned.size);
}

// --- defs listing ----------------------------------------------------------

namespace {

struct FixtureDefs {
    LoadResult loaded;
    match::engine::MatchRegistries registries;
};

FixtureDefs LoadFixtureDefs(const fs::path& mods_root) {
    FixtureDefs fixture;
    fixture.loaded = ScanModsDirectory(mods_root.string());
    REQUIRE(fixture.loaded.ok());
    auto content = std::make_shared<match::engine::AssemblyContent>();
    for (const auto& mod : fixture.loaded.mods) {
        content->mods.push_back({mod.manifest.id, mod.manifest.version});
        content->kinds_by_mod.emplace_back();
    }
    fixture.registries.content = std::move(content);
    return fixture;
}

}  // namespace

TEST_CASE("defs: a mod lists its texts files with locale, hash, and size") {
    const std::string english = kEnglish;
    const LoadResult loaded = LoadMod(
        R"(["my_mod:greeting"])",
        {{"en.json", english}, {"it.json", R"({"my_mod:greeting":"Ciao"})"}});
    auto content = std::make_shared<match::engine::AssemblyContent>();
    content->mods.push_back({"my_mod", "1"});
    content->kinds_by_mod.emplace_back();
    match::engine::MatchRegistries registries;
    registries.content = std::move(content);
    const json defs =
        match::view::DefsBuilder::Build(registries, loaded.mods);
    const json& listing = defs["mods"][0]["texts"];
    REQUIRE(listing.size() == 2);
    CHECK(listing[0]["locale"] == "en");
    CHECK(listing[0]["hash"] == Sha256Hex(english));
    CHECK(listing[0]["size"] == english.size());
    CHECK(listing[1]["locale"] == "it");
}

TEST_CASE("defs: a mod with no texts has no texts key") {
    const LoadResult loaded = LoadMod(R"(["log_a"])", {});
    auto content = std::make_shared<match::engine::AssemblyContent>();
    content->mods.push_back({"my_mod", "1"});
    content->kinds_by_mod.emplace_back();
    match::engine::MatchRegistries registries;
    registries.content = std::move(content);
    const json defs =
        match::view::DefsBuilder::Build(registries, loaded.mods);
    CHECK_FALSE(defs["mods"][0].contains("texts"));
}

TEST_CASE("defs: a changed texts file changes the digest") {
    const LoadResult first = LoadMod(R"(["log_a"])", {{"en.json", "{}"}});
    const LoadResult second =
        LoadMod(R"(["log_a"])", {{"en.json", R"({"my_mod:a":"b"})"}});
    auto content = std::make_shared<match::engine::AssemblyContent>();
    content->mods.push_back({"my_mod", "1"});
    content->kinds_by_mod.emplace_back();
    match::engine::MatchRegistries registries;
    registries.content = std::move(content);
    CHECK(match::view::DefsBuilder::Build(registries, first.mods)["defs_digest"]
          != match::view::DefsBuilder::Build(registries,
                                             second.mods)["defs_digest"]);
}

// --- golden files ----------------------------------------------------------

TEST_CASE("defs: the texts listing of the fixture mod matches the golden "
          "file") {
    FixtureDefs fixture = LoadFixtureDefs(FixtureRoot() / "mods");
    const json defs = match::view::DefsBuilder::Build(fixture.registries,
                                                      fixture.loaded.mods);
    const json actual = {{"mods", defs["mods"]},
                         {"defs_digest", defs["defs_digest"]}};
    CheckGolden(FixtureRoot() / "defs_texts.json", actual);
}

TEST_CASE("texts: the fixture mod validates without an error or a warning") {
    const LoadResult loaded = ScanModsDirectory((FixtureRoot() / "mods").string());
    REQUIRE(loaded.ok());
    CAPTURE(Describe(Validate(loaded)));
    CHECK(Validate(loaded).empty());
    CHECK(Warnings(loaded).empty());
}

TEST_CASE("texts: the validator report of the bad fixtures matches the "
          "golden file") {
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
