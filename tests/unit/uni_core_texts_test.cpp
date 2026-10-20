#include <doctest/doctest.h>

#include <match/modload/glossary_entries.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/view/defs_builder.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

/**
 * @file uni_core_texts_test.cpp
 * @brief The shipped texts of the `uni` mod: the glossary entries and the
 *        descriptions of the base cards, in every shipped locale.
 */

namespace fs = std::filesystem;

using match::modload::LoadedMod;
using match::modload::LoadResult;
using match::modload::TextsFileDef;
using nlohmann::json;

namespace {

const std::vector<std::string> kShippedLocales = {
    "de", "en", "es", "it", "ja", "ko", "ru", "uk", "zh"};

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

const LoadResult& ShippedMods() {
    static const LoadResult loaded =
        match::modload::ScanModsDirectory((ProjectRoot() / "mods").string());
    return loaded;
}

const LoadedMod& UniMod() {
    for (const LoadedMod& mod : ShippedMods().mods) {
        if (mod.manifest.id == "uni") return mod;
    }
    FAIL("the shipped uni mod did not load");
    return ShippedMods().mods.front();
}

const TextsFileDef& TextsOf(const LoadedMod& mod, const std::string& locale) {
    for (const TextsFileDef& file : mod.texts) {
        if (file.locale == locale) return file;
    }
    FAIL("missing texts file for locale " << locale);
    return mod.texts.front();
}

std::set<std::string> KeysOf(const TextsFileDef& file) {
    std::set<std::string> keys;
    for (const auto& [key, value] : file.content.items()) keys.insert(key);
    return keys;
}

/** @brief Every key that an entry or a card of the mod points to. */
std::set<std::string> KeysTheEntriesNeed(const LoadedMod& mod) {
    std::set<std::string> needed;
    for (const auto& entry :
         match::modload::BuildGlossaryEntries(mod, {mod.manifest.id})) {
        needed.insert(entry.label_key);
        needed.insert(entry.description_key);
    }
    return needed;
}

std::set<std::string> PlaceholderNames(const std::string& text) {
    static const std::regex placeholder(R"(\{\s*([A-Za-z0-9_]+))");
    std::set<std::string> names;
    for (std::sregex_iterator it(text.begin(), text.end(), placeholder), end;
         it != end; ++it) {
        names.insert((*it)[1].str());
    }
    return names;
}

}  // namespace

TEST_CASE("uni texts: the shipped uni mod validates with no error and no warning") {
    const std::string schema_dir = (ProjectRoot() / "contract" / "schemas").string();
    match::modload::SemanticValidator validator(schema_dir);
    const LoadedMod& mod = UniMod();

    const auto errors = validator.ValidateMod(mod);
    std::string described;
    for (const auto& error : errors) described += error.check + ": " + error.message + "\n";
    CAPTURE(described);
    CHECK(errors.empty());

    const auto warnings = validator.WarningsOf(mod);
    std::string warning_text;
    for (const auto& warning : warnings) {
        warning_text += warning.check + ": " + warning.message + "\n";
    }
    CAPTURE(warning_text);
    CHECK(warnings.empty());
}

TEST_CASE("uni texts: exactly the shipped locales have a file, and pseudo has none") {
    std::set<std::string> found;
    for (const TextsFileDef& file : UniMod().texts) found.insert(file.locale);
    CHECK(found == std::set<std::string>(kShippedLocales.begin(), kShippedLocales.end()));
    CHECK(found.count("pseudo") == 0);
}

TEST_CASE("uni texts: every key of every locale is in the uni namespace") {
    for (const std::string& locale : kShippedLocales) {
        for (const std::string& key : KeysOf(TextsOf(UniMod(), locale))) {
            CAPTURE(locale);
            CAPTURE(key);
            CHECK(key.rfind("uni:", 0) == 0);
            CHECK(match::modload::IsValidTextKey(key));
        }
    }
}

TEST_CASE("uni texts: every locale has the key set of en, no missing and no extra") {
    const std::set<std::string> english = KeysOf(TextsOf(UniMod(), "en"));
    REQUIRE(!english.empty());
    for (const std::string& locale : kShippedLocales) {
        const std::set<std::string> keys = KeysOf(TextsOf(UniMod(), locale));
        std::vector<std::string> missing;
        std::vector<std::string> extra;
        std::set_difference(english.begin(), english.end(), keys.begin(), keys.end(),
                            std::back_inserter(missing));
        std::set_difference(keys.begin(), keys.end(), english.begin(), english.end(),
                            std::back_inserter(extra));
        CAPTURE(locale);
        CHECK(missing.empty());
        CHECK(extra.empty());
    }
}

TEST_CASE("uni texts: en holds the keys of the entries and the texts of the wild card only") {
    const std::set<std::string> english = KeysOf(TextsOf(UniMod(), "en"));
    const std::set<std::string> needed = KeysTheEntriesNeed(UniMod());
    std::vector<std::string> missing;
    std::vector<std::string> unused;
    std::set_difference(needed.begin(), needed.end(), english.begin(), english.end(),
                        std::back_inserter(missing));
    std::set_difference(english.begin(), english.end(), needed.begin(), needed.end(),
                        std::back_inserter(unused));
    CHECK(missing.empty());
    // INFO: the explicit entry of `uni:wild` replaces the automatic entry of the
    //       wild card, but the card popover still shows the card texts.
    // INFO: the play log and reaction texts (`uni:log.*`) belong to the play log
    //       lines, not to a glossary entry.
    unused.erase(std::remove_if(unused.begin(), unused.end(),
                                [](const std::string& key) { return key.rfind("uni:log.", 0) == 0; }),
                 unused.end());
    CHECK(unused == std::vector<std::string>{"uni:wild.description", "uni:wild.label"});
}

TEST_CASE("uni texts: a text is a non-empty string in every locale") {
    for (const std::string& locale : kShippedLocales) {
        for (const auto& [key, value] : TextsOf(UniMod(), locale).content.items()) {
            CAPTURE(locale);
            CAPTURE(key);
            REQUIRE(value.is_string());
            CHECK(!value.get<std::string>().empty());
        }
    }
}

TEST_CASE("uni texts: the placeholders of a key are the same in every locale") {
    const TextsFileDef& english = TextsOf(UniMod(), "en");
    for (const std::string& locale : kShippedLocales) {
        const TextsFileDef& file = TextsOf(UniMod(), locale);
        for (const auto& [key, value] : english.content.items()) {
            if (!file.content.contains(key)) continue;
            CAPTURE(locale);
            CAPTURE(key);
            CHECK(PlaceholderNames(file.content[key].get<std::string>())
                  == PlaceholderNames(value.get<std::string>()));
        }
    }
}

TEST_CASE("uni texts: every [k=...] link points to a glossary entry of the mod") {
    std::set<std::string> entry_ids;
    for (const auto& entry : match::modload::BuildGlossaryEntries(
             UniMod(), {UniMod().manifest.id})) {
        entry_ids.insert(entry.id);
    }
    static const std::regex link(R"(\[k=([^\]]+)\])");
    for (const std::string& locale : kShippedLocales) {
        for (const auto& [key, value] : TextsOf(UniMod(), locale).content.items()) {
            const std::string text = value.get<std::string>();
            for (std::sregex_iterator it(text.begin(), text.end(), link), end; it != end; ++it) {
                CAPTURE(locale);
                CAPTURE(key);
                const std::string target = (*it)[1].str();
                // INFO: a link that names a parameter (`[k={kind}]`) resolves at run time.
                if (!target.empty() && target.front() == '{') continue;
                CHECK(entry_ids.count(target) == 1);
            }
        }
    }
}

TEST_CASE("uni texts: the base glossary entries are the 15 explicit entries and the cards") {
    std::map<std::string, std::string> source_by_id;
    for (const auto& entry : match::modload::BuildGlossaryEntries(
             UniMod(), {UniMod().manifest.id})) {
        source_by_id[entry.id] = std::string(match::modload::GlossarySourceName(entry.source));
    }
    std::size_t explicit_count = 0;
    for (const auto& [id, source] : source_by_id) {
        if (source == "explicit") ++explicit_count;
    }
    CHECK(explicit_count == 16);
    CHECK(source_by_id.size() == 54 + 15);
    CHECK(source_by_id.at("uni:wild") == "explicit");
    CHECK(source_by_id.at("uni:red_5") == "card");
}

TEST_CASE("uni texts: the lobby text listing names the uni texts and the glossary") {
    const json listing = match::view::DefsBuilder::BuildTextListing(ShippedMods().mods);
    REQUIRE(listing.is_array());
    const json* uni = nullptr;
    for (const json& mod : listing) {
        CHECK((mod.contains("texts") || mod.contains("glossary")));
        if (mod.at("id") == "uni") uni = &mod;
    }
    REQUIRE(uni != nullptr);
    REQUIRE(uni->at("texts").size() == kShippedLocales.size());
    for (const json& file : uni->at("texts")) {
        CHECK(file.at("hash").get<std::string>().size() == 64);
        CHECK(file.at("size").get<std::size_t>() > 0);
    }
    CHECK(uni->at("glossary").size() == 54 + 15);
}

TEST_CASE("uni texts: the lobby text listing leaves out a mod with neither listing") {
    const json listing = match::view::DefsBuilder::BuildTextListing({});
    CHECK(listing.is_array());
    CHECK(listing.empty());
}
