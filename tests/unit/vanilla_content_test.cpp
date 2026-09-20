#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

/**
 * @file vanilla_content_test.cpp
 * @brief End-to-end content gate for the shipped `mods/` tree.
 *
 * Loads every mod folder through the ModLoader, asserts the scan is
 * clean, runs the SemanticValidator over each mod (checks 1-6, 8) and over
 * every declared deck as part of the full active mod set (checks 1-8). It
 * exists so `mods/` content is exercised by `ctest -R unit` and stays green
 * through the engine swap; it is additive and asserts nothing about the
 * legacy engine.
 */

namespace fs = std::filesystem;
using namespace match::modload;

namespace {

/* INFO: locate the project root (which holds `contract/schemas` and `mods`)
 *       by walking up from this file, so the test works regardless of the
 *       ctest working directory. Mirrors semantic_validator_test.cpp. */
fs::path ProjectRoot() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (fs::is_directory(p / "contract" / "schemas", ec)) return p;
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

std::string FirstError(const std::vector<LoadError>& errors) {
    if (errors.empty()) return "";
    const LoadError& e = errors.front();
    return "[" + e.check + "] " + e.artifact + " " + e.path + ": " + e.message;
}

std::string JoinErrors(const std::vector<LoadError>& errors) {
    std::string joined;
    for (const auto& e : errors) {
        if (!joined.empty()) joined += " | ";
        joined += FirstError({e});
    }
    return joined;
}

}  // namespace

TEST_CASE("vanilla content: mods load and validate clean") {
    const fs::path root = ProjectRoot();
    REQUIRE_MESSAGE(!root.empty(), "could not locate the project root");

    LoadResult load = ScanModsDirectory((root / "mods").string());
    CHECK_MESSAGE(load.ok(),
                  "mod load failed: " << JoinErrors(load.errors));
    REQUIRE(load.ok());
    REQUIRE_FALSE(load.mods.empty());

    /* INFO: Content coverage: vanilla must declare the standard
     *       draw-debt status, accumulated by magnitude. */
    const LoadedMod* vanilla = nullptr;
    for (const auto& mod : load.mods) {
        if (mod.manifest.id == "vanilla") vanilla = &mod;
    }
    REQUIRE_MESSAGE(vanilla != nullptr, "vanilla mod not loaded");
    bool has_draw_debt = false;
    for (const auto& status : vanilla->statuses) {
        if (status.status_id == "vanilla:draw_debt") {
            has_draw_debt = true;
            CHECK(status.stack_policy == "accumulate");
        }
    }
    CHECK_MESSAGE(has_draw_debt, "vanilla:draw_debt status is not declared");

    /* INFO: Content coverage: vanilla ships the three play-legality
     *       restriction entries as data. They are delivered by a
     *       rule hook that runs `add_restriction`; collect every declared
     *       entry id and assert the full set is present with the expected
     *       deny condition keyword. */
    std::map<std::string, std::string> declared_restrictions;
    for (const auto& rule : vanilla->rules) {
        for (const auto& hook : rule.hooks) {
            for (const auto& node : hook.graph.nodes) {
                if (!node.is_object() || !node.contains("op")
                    || !node["op"].is_string()
                    || node["op"].get<std::string>() != "add_restriction") {
                    continue;
                }
                const auto args = node.find("args");
                if (args == node.end() || !args->is_object()) continue;
                const auto entry = args->find("entry_def");
                if (entry == args->end() || !entry->is_object()) continue;
                const auto id = entry->find("id");
                const auto condition = entry->find("condition");
                if (id == entry->end() || !id->is_string()
                    || condition == entry->end() || !condition->is_object()
                    || condition->size() != 1) {
                    continue;
                }
                declared_restrictions[id->get<std::string>()] =
                    condition->begin().key();
            }
        }
    }
    CHECK(declared_restrictions.size() == 3);
    CHECK(declared_restrictions["vanilla:turn_order"]
          == "plays_out_of_turn");
    CHECK(declared_restrictions["vanilla:match_type_or_value"]
          == "plays_mismatch");
    CHECK(declared_restrictions["vanilla:must_own_card"]
          == "plays_unowned");

    const SemanticValidator validator((root / "contract" / "schemas").string());

    for (const auto& mod : load.mods) {
        const std::vector<LoadError> errors = validator.ValidateMod(mod);
        CHECK_MESSAGE(errors.empty(),
                      "mod '" << mod.manifest.id << "': "
                              << JoinErrors(errors));
    }

    /* INFO: checks 3/4/7/8 only run in match-set mode, so every declared deck
     *       must be validated against the whole loaded set, not just its mod. */
    std::size_t decks_checked = 0;
    for (const auto& mod : load.mods) {
        for (const auto& deck : mod.decks) {
            ++decks_checked;
            const std::vector<LoadError> errors =
                validator.ValidateMatchSet(load.mods, deck);
            CHECK_MESSAGE(errors.empty(),
                          "deck '" << deck.deck_id << "': "
                                   << JoinErrors(errors));
        }
    }
    CHECK_MESSAGE(decks_checked >= 1, "no decks were found under mods/");
}
