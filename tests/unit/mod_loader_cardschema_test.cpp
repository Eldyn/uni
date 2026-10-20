#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include <match/modload/mod_loader.hpp>

#include "cardschema_test_support.hpp"

namespace {

namespace fs = std::filesystem;

using match::modload::CardDef;
using match::modload::LoadError;
using match::modload::LoadResult;
using match::modload::LoadedMod;

// INFO: walk up from this test file to the repository root, so the fixture
// mods resolve no matter where the test binary is started.
fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        const fs::path parent = path.parent_path();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return {};
}

fs::path FixtureUni() {
    return ProjectRoot() / "tests" / "fixtures" / "card_schema" / "clean" / "uni";
}

// INFO: a scratch `mods/` root built from the shared cardschema temp dir. The
// clean mini mod is copied from the fixture folder; a test may add a second mod.
struct TempModsRoot {
    CardSchemaTempDir directory;

    const fs::path& root() const { return directory.root; }

    void Write(const std::string& relative, const std::string& text) const {
        directory.Write(relative, text);
    }

    void CopyMiniUni() const {
        fs::copy(FixtureUni(), directory.root / "uni",
                 fs::copy_options::recursive);
    }
};

const char* kManifest =
    R"({"id":"uni","name":"U","version":"1.0.0","api":"1"})";

// INFO: the numbered kind with a bad face placeholder; the loader accepts the
// file (only `identifier_format` and `title_format` are checked at load time),
// so the `unknown placeholder` error can only come from the catalog expansion.
const char* kBadFaceKind =
    R"({"format_version":"1.0.0","kind":{"identifier":"uni:numbered",)"
    R"("tags":["numbered"],"attributes":[{"attribute":"uni:color"},)"
    R"({"attribute":"uni:value"}],"identifier_format":"{color}_{value}",)"
    R"("title_format":"{color} {value}",)"
    R"("face":{"kind":"text","color":"{color}","label":"{colour}",)"
    R"("art_version":1}}})";

// INFO: one deck entry per generated card field, so the conversion can be
// checked field by field.
const char* kTrickKind =
    R"({"format_version":"1.0.0","kind":{"identifier":"uni:trick",)"
    R"("label_key":"card.trick.label","description_key":"card.trick.description",)"
    R"("behavior":{"on_play":{"nodes":[{"id":"n1","op":"advance_turn",)"
    R"("args":{}}]}},"window":{"when_played":{"responders":"@others",)"
    R"("duration":"env","on_response":"resolve_stack","default":"resolve_stack"}},)"
    R"("auto_trigger":{"condition":{"always":true},"graph":{"nodes":[]}},)"
    R"("face":{"kind":"text","color":"white","label":"T","art_version":1}}})";

const char* kAlphaCardKind =
    R"({"format_version":"1.0.0","kind":{"identifier":"alpha:card",)"
    R"("face":{"kind":"text","color":"white","label":"A","art_version":1}}})";

// INFO: beta's template has a slot over alpha's attribute, so expanding it needs
// alpha's member; the card therefore names alpha in `requires_mods`.
const char* kBetaCardKind =
    R"({"format_version":"1.0.0","kind":{"identifier":"beta:card",)"
    R"("attributes":[{"attribute":"alpha:color"}],)"
    R"("identifier_format":"{color}","title_format":"{color}",)"
    R"("face":{"kind":"text","color":"{color}","label":"B","art_version":1}}})";

const char* kAlphaAttribute =
    R"({"format_version":"1.0.0","attribute":{"identifier":"alpha:color",)"
    R"("visibility":"public"}})";

const char* kAlphaMember =
    R"({"format_version":"1.0.0","attribute_member":{"attribute":"alpha:color",)"
    R"("identifier":"alpha:red","tags":["colored"],"order":0}})";

const char* kBareManifest =
    R"({"id":"%s","name":"M","version":"1.0.0","api":"1"})";

const LoadedMod* FindMod(const LoadResult& result, const std::string& mod_id) {
    for (const LoadedMod& mod : result.mods) {
        if (mod.manifest.id == mod_id) return &mod;
    }
    return nullptr;
}

const CardDef* FindCard(const LoadedMod& mod, const std::string& kind_id) {
    for (const CardDef& card : mod.cards) {
        if (card.kind_id == kind_id) return &card;
    }
    return nullptr;
}

const LoadError* FindError(const std::vector<LoadError>& errors,
                           const std::string& text) {
    for (const LoadError& error : errors) {
        if (error.message.find(text) != std::string::npos) return &error;
    }
    return nullptr;
}

// INFO: a mod with a manifest and `deck_count` empty decks named `deck_<n>`.
void WriteEmptyDecks(const TempModsRoot& mods, const std::string& mod_id,
                     int deck_count) {
    mods.Write(mod_id + "/mod.json", std::string(kBareManifest).replace(
                                        std::string(kBareManifest).find("%s"), 2,
                                        mod_id));
    for (int index = 0; index < deck_count; ++index) {
        const std::string name = "deck_" + std::to_string(index);
        mods.Write(mod_id + "/decks/" + name + ".json",
                   "{\"format_version\":\"1.0.0\",\"deck\":{\"identifier\":\"" +
                       mod_id + ":" + name + "\",\"name\":\"D\",\"cards\":[]}}");
    }
}

}  // namespace

TEST_SUITE("mod loader card schema") {

TEST_CASE("generates_cards_and_deck") {
    TempModsRoot mods;
    mods.CopyMiniUni();

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);
    const LoadedMod& uni = result.mods[0];
    CHECK(uni.manifest.id == "uni");

    std::vector<std::string> kind_ids;
    for (const CardDef& card : uni.cards) kind_ids.push_back(card.kind_id);
    CHECK(kind_ids == std::vector<std::string>{
                          "uni:blue_0",           "uni:blue_1",
                          "uni:blue_heart_alpha", "uni:blue_heart_beta",
                          "uni:blue_spade_alpha", "uni:blue_spade_beta",
                          "uni:red_0",            "uni:red_1",
                          "uni:red_heart_alpha",  "uni:red_heart_beta",
                          "uni:red_spade_alpha",  "uni:red_spade_beta",
                          "uni:wild"});

    REQUIRE(uni.decks.size() == 1);
    CHECK(uni.decks[0].deck_id == "uni:mini");
    int total = 0;
    for (const auto& [kind_id, count] : uni.decks[0].cards) total += count;
    CHECK(total == 8);
}

TEST_CASE("clean_mod_has_no_warnings") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("uni/rules/standard.json",
               R"({"id":"standard","title":"S","hooks":[]})");
    mods.Write("uni/statuses/debt.json",
               R"({"id":"debt","title":"D","stack_policy":"replace"})");
    mods.Write("uni/mutations/nerf.json",
               R"({"id":"nerf","target":"uni:wild","mode":"replace"})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);
    CHECK(result.warnings.empty());
}

TEST_CASE("card_def_fields") {
    TempModsRoot mods;
    mods.CopyMiniUni();

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());
    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);

    const CardDef* card = FindCard(result.mods[0], "uni:red_1");
    REQUIRE(card != nullptr);
    CHECK(card->face.color == "red");
    CHECK(card->face.label == "1");
    CHECK(card->tags == std::vector<std::string>{"numbered", "colored", "red"});
    CHECK(card->title == "Red 1");
    CHECK(card->raw["id"] == "red_1");
    CHECK(card->template_id == "uni:numbered");
    CHECK(card->requires_mods.empty());
}

TEST_CASE("deck_def_fields") {
    TempModsRoot mods;
    mods.CopyMiniUni();

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());
    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);

    const LoadedMod& uni = result.mods[0];
    REQUIRE(uni.decks.size() == 1);
    const match::modload::DeckDef& deck = uni.decks[0];
    CHECK(deck.id == "mini");
    CHECK(deck.namespace_id == "uni");
    CHECK(deck.deck_id == "uni:mini");
    CHECK(deck.name == "Mini");
    CHECK(deck.mods == std::vector<std::string>{"uni"});
    CHECK(deck.raw["id"] == "mini");
    CHECK(deck.raw["name"] == "Mini");
    CHECK(deck.raw["namespace"] == "uni");
    CHECK(deck.raw["mods"] == nlohmann::json::array({"uni"}));
    CHECK(deck.raw["cards"]["uni:wild"] == 4);
    CHECK(deck.settings == nlohmann::json{{"count_wild", 4}});
}

TEST_CASE("generated_card_keeps_the_template_fields") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("uni/kinds/trick.json", kTrickKind);

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());
    REQUIRE(result.ok());
    REQUIRE(result.mods.size() == 1);

    const CardDef* card = FindCard(result.mods[0], "uni:trick");
    REQUIRE(card != nullptr);
    CHECK(card->label_key == "card.trick.label");
    CHECK(card->description_key == "card.trick.description");
    REQUIRE(card->behaviors.size() == 1);
    CHECK(card->behaviors[0].hook == "on_play");
    REQUIRE(card->window.has_value());
    CHECK(card->window->responders == "@others");
    CHECK(card->window->on_response == "resolve_stack");
    REQUIRE(card->auto_trigger.has_value());
    CHECK(card->auto_trigger->condition == nlohmann::json{{"always", true}});
    CHECK(card->template_id == "uni:trick");
}

TEST_CASE("card_conversion_error_names_the_owner_kind_file") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("uni/kinds/numbered.json",
               R"({"format_version":"1.0.0","kind":{"identifier":"uni:numbered",)"
               R"("tags":["numbered"],"attributes":[{"attribute":"uni:color"},)"
               R"({"attribute":"uni:value"}],"identifier_format":"{color}_{value}",)"
               R"("title_format":"{color} {value}",)"
               R"("face":{"kind":"bogus","label":"{value}","art_version":1}}})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    REQUIRE(result.errors.size() == 4);
    for (const LoadError& error : result.errors) {
        CHECK(error.check == "card.face");
        CHECK(error.artifact == "cards");
        CHECK(error.path == "mods/uni/kinds/numbered.json");
        CHECK(error.message == "unknown face kind 'bogus'");
    }
}

TEST_CASE("old_cards_dir_is_an_error") {
    TempModsRoot mods;
    mods.Write("uni/mod.json", kManifest);
    mods.Write("uni/cards/legacy.json", R"({"id":"legacy"})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "card_schema");
    CHECK(result.errors[0].artifact == "cards");
    CHECK(result.errors[0].path == (mods.root() / "uni" / "cards").string());
    CHECK(result.errors[0].message ==
          "cards/ is the removed schema 0 form; move the files to kinds/ (see docs)");
}

TEST_CASE("old_cards_dir_with_an_old_deck_shows_the_migration_hint") {
    TempModsRoot mods;
    mods.Write("uni/mod.json", kManifest);
    mods.Write("uni/cards/legacy.json", R"({"id":"legacy"})");
    mods.Write("uni/decks/classic.json",
               R"({"id":"classic","name":"C","namespace":"uni",)"
               R"("mods":["uni"],"cards":{}})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    const LoadError* hint = FindError(result.errors, "removed schema 0 form");
    REQUIRE(hint != nullptr);
    CHECK(hint->check == "card_schema");
    CHECK(hint->path == (mods.root() / "uni" / "cards").string());
    CHECK(hint->message ==
          "cards/ is the removed schema 0 form; move the files to kinds/ (see docs)");
}

TEST_CASE("old_cards_dir_with_kinds_shows_the_migration_hint") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("uni/cards/legacy.json", R"({"id":"legacy"})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    const LoadError* hint = FindError(result.errors, "removed schema 0 form");
    REQUIRE(hint != nullptr);
    CHECK(hint->message ==
          "cards/ is the removed schema 0 form; move the files to kinds/ (see docs)");
}

TEST_CASE("catalog_error_drops_the_mod") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("uni/kinds/numbered.json", kBadFaceKind);

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "card_schema");
    CHECK(result.errors[0].artifact == "cards");
    CHECK(result.errors[0].path == "mods/uni/kinds/numbered.json");
    CHECK(result.errors[0].message ==
          "mods/uni/kinds/numbered.json#/kind/face/label: unknown placeholder "
          "\"{colour}\" (did you mean \"color\")");
}

TEST_CASE("catalog_error_keeps_the_other_mod_isolated") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("uni/kinds/numbered.json", kBadFaceKind);
    mods.Write("beta/mod.json",
               R"({"id":"beta","name":"B","version":"1.0.0","api":"1"})");
    mods.Write("beta/attributes/color.json",
               R"({"format_version":"1.0.0","attribute":{"identifier":"beta:color",)"
               R"("visibility":"public"}})");
    mods.Write("beta/attributes/color/red.json",
               R"({"format_version":"1.0.0","attribute_member":)"
               R"({"attribute":"beta:color","identifier":"beta:red",)"
               R"("tags":["colored"],"order":0}})");
    mods.Write("beta/kinds/card.json",
               R"({"format_version":"1.0.0","kind":{"identifier":"beta:card",)"
               R"("attributes":[{"attribute":"beta:color"}],)"
               R"("identifier_format":"{color}","title_format":"{color}",)"
               R"("face":{"kind":"text","color":"{color}","label":"B",)"
               R"("art_version":1}}})");
    mods.Write("beta/decks/one.json",
               R"({"format_version":"1.0.0","deck":{"identifier":"beta:one",)"
               R"("name":"One","mods":["beta"],"cards":[{"kind":"beta:card",)"
               R"("copies":1}]}})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    REQUIRE(result.mods.size() == 1);
    const LoadedMod& beta = result.mods[0];
    CHECK(beta.manifest.id == "beta");
    REQUIRE(beta.cards.size() == 1);
    CHECK(beta.cards[0].kind_id == "beta:red");
    CHECK(beta.cards[0].requires_mods.empty());
    REQUIRE(beta.decks.size() == 1);
    REQUIRE(beta.decks[0].cards.size() == 1);
    CHECK(beta.decks[0].cards[0].first == "beta:red");
    CHECK(beta.decks[0].cards[0].second == 1);
}

TEST_CASE("missing_setting_drops_the_mod") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("uni/mod.json", kManifest);

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "card_schema");
    CHECK(result.errors[0].artifact == "cards");
    CHECK(result.errors[0].path == "mods/uni/decks/mini.json");
    CHECK(result.errors[0].message ==
          "mods/uni/decks/mini.json#/deck/cards/1/copies: unknown property "
          "\"count_wild\" on settings");
}

TEST_CASE("deck_settings_merge_in_deck_mod_order") {
    TempModsRoot mods;
    mods.Write("alpha/mod.json",
               R"({"id":"alpha","name":"A","version":"1.0.0","api":"1",)"
               R"("settings":[{"id":"count_wild","type":"int","default":2}]})");
    mods.Write("alpha/kinds/card.json", kAlphaCardKind);
    mods.Write("alpha/decks/one.json",
               R"({"format_version":"1.0.0","deck":{"identifier":"alpha:one",)"
               R"("name":"One","mods":["alpha","beta"],"cards":[)"
               R"({"kind":"alpha:card","copies":"@settings.count_wild"},)"
               R"({"kind":"alpha:card","copies":"@settings.count_numbered"}]}})");
    mods.Write("beta/mod.json",
               R"({"id":"beta","name":"B","version":"1.0.0","api":"1",)"
               R"("settings":[{"id":"count_numbered","type":"int","default":7}]})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    REQUIRE(result.ok());
    const LoadedMod* alpha = FindMod(result, "alpha");
    REQUIRE(alpha != nullptr);
    REQUIRE(alpha->decks.size() == 1);
    CHECK(alpha->decks[0].settings ==
          nlohmann::json{{"count_numbered", 7}, {"count_wild", 2}});
    int total = 0;
    for (const auto& [kind_id, count] : alpha->decks[0].cards) total += count;
    CHECK(total == 9);
}

TEST_CASE("conflicting_setting_defaults_name_both_mods") {
    TempModsRoot mods;
    mods.Write("alpha/mod.json",
               R"({"id":"alpha","name":"A","version":"1.0.0","api":"1",)"
               R"("settings":[{"id":"count_wild","type":"int","default":2}]})");
    mods.Write("alpha/kinds/card.json", kAlphaCardKind);
    mods.Write("alpha/decks/one.json",
               R"({"format_version":"1.0.0","deck":{"identifier":"alpha:one",)"
               R"("name":"One","mods":["alpha","beta"],"cards":[)"
               R"({"kind":"alpha:card","copies":"@settings.count_wild"}]}})");
    mods.Write("beta/mod.json",
               R"({"id":"beta","name":"B","version":"1.0.0","api":"1",)"
               R"("settings":[{"id":"count_wild","type":"int","default":3}]})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(FindMod(result, "alpha") == nullptr);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "card_schema");
    CHECK(result.errors[0].path == "mods/alpha/decks/one.json");
    CHECK(result.errors[0].message ==
          "mods/alpha/decks/one.json#/deck: setting \"count_wild\" is declared by "
          "mods \"alpha\" and \"beta\" with different defaults");
}

TEST_CASE("too_many_decks_in_one_mod_is_an_error") {
    TempModsRoot mods;
    WriteEmptyDecks(mods, "uni", 65);

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].check == "card_schema");
    CHECK(result.errors[0].path == (mods.root() / "uni" / "mod.json").string());
    CHECK(result.errors[0].message ==
          "mod \"uni\" declares 65 decks; the limit is 64");
}

TEST_CASE("too_many_decks_across_mods_is_an_error") {
    TempModsRoot mods;
    for (const char* mod_id : {"alpha", "beta", "gamma", "delta", "epsilon"}) {
        WriteEmptyDecks(mods, mod_id, 52);
    }

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    REQUIRE(result.errors.size() == 5);
    CHECK(result.errors[0].message ==
          "mod \"alpha\" declares 52 decks; the mods together declare 260, "
          "the limit is 256");
}

TEST_CASE("schema_errors_are_capped_with_one_marker") {
    TempModsRoot mods;
    mods.Write("uni/mod.json", kManifest);
    fs::create_directories(mods.root() / "uni" / "decks");
    fs::copy_file(ProjectRoot() / "tests" / "fixtures" / "card_schema"
                      / "many_diagnostics" / "uni" / "decks" / "big.json",
                  mods.root() / "uni" / "decks" / "big.json");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(result.mods.empty());
    REQUIRE(result.errors.size() == 101);
    CHECK(result.errors[0].check == "card_schema");
    CHECK(result.errors[0].path == "mods/uni/decks/big.json");
    CHECK(result.errors[0].message ==
          "mods/uni/decks/big.json#/deck/cards/0: unknown kind \"uni:nope\"");
    CHECK(result.errors[99].message ==
          "mods/uni/decks/big.json#/deck/cards/99: unknown kind \"uni:nope\"");
    CHECK(result.errors[100].check == "card_schema");
    CHECK(result.errors[100].message == "too many schema errors; more not shown");
}

TEST_CASE("foreign_member_hosts_in_kind_owner") {
    TempModsRoot mods;
    mods.CopyMiniUni();
    mods.Write("magenta/mod.json",
               R"({"id":"magenta","name":"M","version":"1.0.0","api":"1"})");
    mods.Write("magenta/attributes/color/magenta.json",
               R"({"format_version":"1.0.0","attribute_member":)"
               R"({"attribute":"uni:color","identifier":"magenta:magenta",)"
               R"("tags":["colored","magenta"],"order":2}})");

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    REQUIRE(result.ok());
    const LoadedMod* uni = FindMod(result, "uni");
    const LoadedMod* magenta = FindMod(result, "magenta");
    REQUIRE(uni != nullptr);
    REQUIRE(magenta != nullptr);

    const CardDef* card = FindCard(*uni, "uni:magenta_0");
    REQUIRE(card != nullptr);
    CHECK(card->requires_mods == std::vector<std::string>{"magenta"});
    CHECK(magenta->cards.empty());

    REQUIRE(uni->decks.size() == 1);
    CHECK(uni->decks[0].mods == std::vector<std::string>{"uni"});
    bool deck_has_foreign = false;
    int total = 0;
    for (const auto& [kind_id, count] : uni->decks[0].cards) {
        total += count;
        if (kind_id.rfind("uni:magenta_", 0) == 0) deck_has_foreign = true;
    }
    CHECK_FALSE(deck_has_foreign);
    CHECK(total == 8);
}

TEST_CASE("card_over_a_failed_mods_member_is_dropped") {
    TempModsRoot mods;
    mods.Write("alpha/mod.json",
               R"({"id":"alpha","name":"A","version":"1.0.0","api":"1"})");
    mods.Write("alpha/attributes/color.json", kAlphaAttribute);
    mods.Write("alpha/attributes/color/red.json", kAlphaMember);
    mods.Write("alpha/kinds/card.json",
               R"({"format_version":"1.0.0","kind":{"identifier":"alpha:card",)"
               R"("face":{"kind":"text","label":"{colour}","art_version":1}}})");
    mods.Write("beta/mod.json",
               R"({"id":"beta","name":"B","version":"1.0.0","api":"1"})");
    mods.Write("beta/kinds/card.json", kBetaCardKind);

    const LoadResult result = match::modload::ScanModsDirectory(mods.root().string());

    CHECK_FALSE(result.ok());
    CHECK(FindMod(result, "alpha") == nullptr);
    const LoadedMod* beta = FindMod(result, "beta");
    REQUIRE(beta != nullptr);
    CHECK(beta->cards.empty());
}

TEST_CASE("catalog_diagnostic_mod_id_reads_the_file_label") {
    CHECK(match::modload::CatalogDiagnosticModId("mods/uni/kinds/numbered.json") == "uni");
    CHECK(match::modload::CatalogDiagnosticModId("mods/magenta/attributes/color.json") ==
          "magenta");
    CHECK(match::modload::CatalogDiagnosticModId("uni/kinds/numbered.json").empty());
    CHECK(match::modload::CatalogDiagnosticModId("").empty());
    CHECK(match::modload::CatalogDiagnosticModId("mods/uni").empty());
}

TEST_CASE("unprefixed_catalog_diagnostic_fails_closed") {
    const std::vector<std::string> loaded{"alpha", "beta"};
    CHECK(match::modload::CatalogDiagnosticOwners("mods/alpha/kinds/card.json", loaded) ==
          std::vector<std::string>{"alpha"});
    CHECK(match::modload::CatalogDiagnosticOwners("", loaded) ==
          std::vector<std::string>{"alpha", "beta"});
    CHECK(match::modload::CatalogDiagnosticOwners("mods/kinds/card.json", loaded) ==
          std::vector<std::string>{"alpha", "beta"});
    CHECK(match::modload::CatalogDiagnosticOwners("", {}).empty());
}

}  // TEST_SUITE("mod loader card schema")
