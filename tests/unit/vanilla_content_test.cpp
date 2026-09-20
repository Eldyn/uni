#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>

#include <filesystem>
#include <map>
#include <set>
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

TEST_CASE("vanilla content: six rule-only mods declare their behaviors") {
    const fs::path root = ProjectRoot();
    REQUIRE_MESSAGE(!root.empty(), "could not locate the project root");

    LoadResult load = ScanModsDirectory((root / "mods").string());
    REQUIRE_MESSAGE(load.ok(), "mod load failed: " << JoinErrors(load.errors));

    auto find_mod = [&](const std::string& id) -> const LoadedMod* {
        for (const auto& mod : load.mods) {
            if (mod.manifest.id == id) return &mod;
        }
        return nullptr;
    };

    const char* kRuleMods[] = {"draw_stacking", "force_play", "jump_in",
                               "no_bluffing", "progressive", "seven_zero"};
    for (const char* id : kRuleMods) {
        const LoadedMod* mod = find_mod(id);
        REQUIRE_MESSAGE(mod != nullptr, "rule mod '" << id << "' not loaded");
        CHECK_MESSAGE(mod->cards.empty(),
                      "rule mod '" << id << "' must not ship cards");
        CHECK_MESSAGE(!mod->rules.empty(),
                      "rule mod '" << id << "' declares no rules");
    }

    /* INFO: jump_in rescues the vanilla out-of-turn deny with an allow entry;
     *       no_bluffing adds the deny entry. Collect every entry declared by
     *       any rule graph across the tree. */
    std::map<std::string, std::string> entries;
    for (const auto& mod : load.mods) {
        for (const auto& rule : mod.rules) {
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
                    entries[id->get<std::string>()] = condition->begin().key();
                }
            }
        }
    }
    CHECK(entries["jump_in:identical_out_of_turn"]
          == "plays_identical_to_top");
    CHECK(entries["no_bluffing:drawn_four"] == "plays_bluffing");

    /* INFO: seven_zero keys its two after:play hooks on the zero/seven tags
     *       (the `where value=` shorthand has no store predicate). */
    const LoadedMod* seven = find_mod("seven_zero");
    REQUIRE(seven != nullptr);
    REQUIRE(seven->rules.size() == 1);
    CHECK(seven->rules.front().hooks.size() == 2);
    std::set<std::string> seven_tags;
    for (const auto& hook : seven->rules.front().hooks) {
        if (!hook.where) continue;
        const auto top = hook.where->find("top_of_discard");
        if (top == hook.where->end() || !top->is_object()) continue;
        const auto tag = top->find("tag");
        if (tag != top->end() && tag->is_string()) {
            seven_tags.insert(tag->get<std::string>());
        }
    }
    CHECK(seven_tags.count("zero") == 1);
    CHECK(seven_tags.count("seven") == 1);

    /* INFO: draw_stacking is authored as a response-window rule
     *       not as a pending_draws ValidatePlay hook. */
    const LoadedMod* stacking = find_mod("draw_stacking");
    REQUIRE(stacking != nullptr);
    bool has_window = false;
    for (const auto& rule : stacking->rules) {
        for (const auto& hook : rule.hooks) {
            for (const auto& node : hook.graph.nodes) {
                if (node.is_object() && node.contains("window")) {
                    has_window = true;
                }
            }
        }
    }
    CHECK_MESSAGE(has_window,
                  "draw_stacking does not declare a window node");
}
