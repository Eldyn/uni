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
    REQUIRE(seven->rules.front().hooks.size() == 2);
    std::set<std::string> seven_tags;
    bool seven_zero_rotates = false;
    bool seven_zero_swaps = false;
    for (const auto& hook : seven->rules.front().hooks) {
        if (hook.where) {
            const auto top = hook.where->find("top_of_discard");
            if (top != hook.where->end() && top->is_object()) {
                const auto tag = top->find("tag");
                if (tag != top->end() && tag->is_string()) {
                    seven_tags.insert(tag->get<std::string>());
                }
            }
        }
        bool graph_has_branch = false;
        bool graph_has_pass = false;
        bool graph_has_swap = false;
        for (const auto& node : hook.graph.nodes) {
            if (!node.is_object()) continue;
            if (node.contains("cases")) graph_has_branch = true;
            const std::string op = node.value("op", "");
            if (op == "pass_hands") graph_has_pass = true;
            if (op == "swap_hands") {
                const auto args = node.find("args");
                if (args != node.end() && args->is_object()
                    && args->value("a", "") == "@self"
                    && args->value("b", "") == "@choose_player") {
                    graph_has_swap = true;
                }
            }
        }
        if (graph_has_branch && graph_has_pass) seven_zero_rotates = true;
        if (graph_has_swap) seven_zero_swaps = true;
    }
    CHECK(seven_tags.count("zero") == 1);
    CHECK(seven_tags.count("seven") == 1);
    CHECK_MESSAGE(seven_zero_rotates,
                  "seven_zero 0-card branch does not rotate hands");
    CHECK_MESSAGE(seven_zero_swaps,
                  "seven_zero 7-card hook does not swap with @choose_player");

    /* INFO: draw_stacking is authored as a response-window rule.
     *       Assert the window fields, the `reopen` chaining declaration, the
     *       response marker route, and the default route's debt draw + clear
     *       (the engine owns the response's N append and the re-open). */
    const LoadedMod* stacking = find_mod("draw_stacking");
    REQUIRE(stacking != nullptr);
    bool stacking_window = false;
    bool stacking_reopens = false;
    bool stacking_response_signal = false;
    bool stacking_default_draws_debt = false;
    bool stacking_default_clears = false;
    for (const auto& rule : stacking->rules) {
        for (const auto& hook : rule.hooks) {
            if (hook.hook != "after:play") continue;
            std::map<std::string, const nlohmann::json*> by_id;
            for (const auto& node : hook.graph.nodes) {
                if (node.is_object() && node.contains("id")
                    && node["id"].is_string()) {
                    by_id[node["id"].get<std::string>()] = &node;
                }
            }
            for (const auto& node : hook.graph.nodes) {
                if (!node.is_object() || !node.contains("window")) continue;
                const auto& window = node["window"];
                if (!window.is_object()) continue;
                if (window.value("responders", "") == "@others"
                    && window.value("duration", "") == "env") {
                    stacking_window = true;
                }
                if (node.value("reopen", false)) stacking_reopens = true;
                const auto on_response = node.find("on_response");
                if (on_response != node.end() && on_response->is_object()) {
                    for (auto it = on_response->begin();
                         it != on_response->end(); ++it) {
                        if (!it.value().is_string()) continue;
                        const auto found =
                            by_id.find(it.value().get<std::string>());
                        if (found != by_id.end()
                            && found->second->value("op", "")
                                   == "emit_signal") {
                            stacking_response_signal = true;
                        }
                    }
                }
                std::string cursor = node.value("default", "");
                int guard = 0;
                while (!cursor.empty() && by_id.count(cursor) != 0
                       && guard++ < 8) {
                    const nlohmann::json& cur = *by_id[cursor];
                    const std::string op = cur.value("op", "");
                    const auto args = cur.find("args");
                    if (op == "draw_cards" && args != cur.end()
                        && args->is_object()
                        && args->value("n_from_debt", false)) {
                        stacking_default_draws_debt = true;
                    }
                    if (op == "remove_status" && args != cur.end()
                        && args->is_object()
                        && args->value("status_kind", "")
                               == "vanilla:draw_debt") {
                        stacking_default_clears = true;
                    }
                    cursor = cur.value("next", "");
                }
            }
        }
    }
    CHECK_MESSAGE(stacking_window,
                  "draw_stacking window missing @others/env fields");
    CHECK_MESSAGE(stacking_reopens,
                  "draw_stacking window does not declare reopen");
    CHECK_MESSAGE(stacking_response_signal,
                  "draw_stacking response route is not the stack marker");
    CHECK_MESSAGE(stacking_default_draws_debt,
                  "draw_stacking default route does not draw the debt");
    CHECK_MESSAGE(stacking_default_clears,
                  "draw_stacking default route does not clear draw_debt");

    /* INFO: jump_in keeps the allow entry and adds a
     *       response window whose on_response route redirects the turn to the
     *       jumper (legacy jump_in.cpp:22-31). */
    const LoadedMod* jump = find_mod("jump_in");
    REQUIRE(jump != nullptr);
    bool jump_redirects = false;
    for (const auto& rule : jump->rules) {
        for (const auto& hook : rule.hooks) {
            if (hook.hook != "after:play") continue;
            for (const auto& node : hook.graph.nodes) {
                if (!node.is_object() || !node.contains("window")
                    || !node.contains("on_response")) {
                    continue;
                }
                const auto& mapping = node["on_response"];
                if (!mapping.is_object()) continue;
                for (auto it = mapping.begin(); it != mapping.end(); ++it) {
                    if (!it.value().is_string()) continue;
                    for (const auto& target : hook.graph.nodes) {
                        if (!target.is_object()) continue;
                        if (target.value("id", "")
                            != it.value().get<std::string>()) {
                            continue;
                        }
                        if (target.value("op", "") != "redirect_turn") {
                            continue;
                        }
                        const auto args = target.find("args");
                        if (args != target.end() && args->is_object()
                            && args->value("target", "") == "@responder") {
                            jump_redirects = true;
                        }
                    }
                }
            }
        }
    }
    CHECK_MESSAGE(jump_redirects,
                  "jump_in response route does not redirect_turn(@responder)");

    /* INFO: KNOWN GAPS. progressive and
     *       force_play are declared for identity/activation only and ship no
     *       graph. If a future fix authors hooks for them, this assertion
     *       fails loudly so the known-gap list is updated. */
    for (const char* id : {"progressive", "force_play"}) {
        const LoadedMod* gap = find_mod(id);
        REQUIRE_MESSAGE(gap != nullptr, "known-gap mod '" << id << "' missing");
        REQUIRE(gap->rules.size() == 1);
        CHECK_MESSAGE(gap->rules.front().hooks.empty(),
                      "known gap '" << id << "' now has hooks; update the "
                      "known-gap list and add content assertions");
    }
}
