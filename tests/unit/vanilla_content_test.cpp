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
    CHECK(entries["draw_stacking:stackable_response"]
          == "plays_stack_response");

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
    bool stacking_response_redirects = false;
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
                if (window.value("responders", "") == "@next_player"
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
                                   == "redirect_turn") {
                            stacking_response_redirects = true;
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
                  "draw_stacking window missing @next_player/env fields");
    CHECK_MESSAGE(stacking_reopens,
                  "draw_stacking window does not declare reopen");
    CHECK_MESSAGE(stacking_response_redirects,
                  "draw_stacking response route does not hand the turn over");
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

    /* INFO: force_play is real content now: an after:draw rule
     *       branches on the drawn_card_playable predicate and, when the
     *       drawn card is playable, runs the play_card op on
     *       @drawn_card (legacy force_play.cpp /
     *       match_instance.cpp:699-727). */
    const LoadedMod* force = find_mod("force_play");
    REQUIRE(force != nullptr);
    REQUIRE(force->rules.size() == 1);
    REQUIRE(force->rules.front().hooks.size() == 1);
    {
        const auto& hook = force->rules.front().hooks.front();
        CHECK(hook.hook == "after:draw");
        std::map<std::string, const nlohmann::json*> by_id;
        for (const auto& node : hook.graph.nodes) {
            if (node.is_object() && node.contains("id")
                && node["id"].is_string()) {
                by_id[node["id"].get<std::string>()] = &node;
            }
        }
        bool force_branch = false;
        bool force_plays = false;
        for (const auto& node : hook.graph.nodes) {
            if (!node.is_object() || !node.contains("cases")) continue;
            const auto& cases = node["cases"];
            if (!cases.is_array()) continue;
            for (const auto& c : cases) {
                if (!c.is_object()) continue;
                const auto when = c.find("when");
                if (when == c.end() || !when->is_object()) continue;
                if (!when->contains("drawn_card_playable")) continue;
                force_branch = true;
                const auto next = c.find("next");
                if (next == c.end() || !next->is_string()) continue;
                const auto found = by_id.find(next->get<std::string>());
                if (found == by_id.end()) continue;
                const nlohmann::json& target = *found->second;
                if (target.value("op", "") != "play_card") continue;
                const auto args = target.find("args");
                if (args != target.end() && args->is_object()
                    && args->value("card", "") == "@drawn_card"
                    && args->value("player", "") == "@self") {
                    force_plays = true;
                }
            }
        }
        CHECK_MESSAGE(force_branch,
                      "force_play does not branch on drawn_card_playable");
        CHECK_MESSAGE(force_plays,
                      "force_play playable route does not play_card "
                      "@drawn_card");
    }

    /* INFO: Progressive is real content now: an after:draw rule
     *       stops when the drawn card is playable and otherwise runs the
     *       The engine draw_until_playable op for @self (legacy progressive.cpp
     *       / match_instance.cpp:699-723). */
    const LoadedMod* progressive = find_mod("progressive");
    REQUIRE(progressive != nullptr);
    REQUIRE(progressive->rules.size() == 1);
    REQUIRE(progressive->rules.front().hooks.size() == 1);
    {
        const auto& hook = progressive->rules.front().hooks.front();
        CHECK(hook.hook == "after:draw");
        std::map<std::string, const nlohmann::json*> by_id;
        for (const auto& node : hook.graph.nodes) {
            if (node.is_object() && node.contains("id")
                && node["id"].is_string()) {
                by_id[node["id"].get<std::string>()] = &node;
            }
        }
        bool progressive_branch = false;
        bool progressive_keeps_drawing = false;
        for (const auto& node : hook.graph.nodes) {
            if (!node.is_object() || !node.contains("cases")) continue;
            const auto& cases = node["cases"];
            if (!cases.is_array()) continue;
            for (const auto& c : cases) {
                if (!c.is_object()) continue;
                const auto when = c.find("when");
                if (when == c.end() || !when->is_object()) continue;
                if (when->contains("drawn_card_playable")) {
                    progressive_branch = true;
                }
            }
            const auto else_route = node.find("else");
            if (else_route == node.end() || !else_route->is_string()) continue;
            const auto found = by_id.find(else_route->get<std::string>());
            if (found == by_id.end()) continue;
            const nlohmann::json& target = *found->second;
            if (target.value("op", "") != "draw_until_playable") continue;
            const auto args = target.find("args");
            if (args != target.end() && args->is_object()
                && args->value("target", "") == "@self") {
                progressive_keeps_drawing = true;
            }
        }
        CHECK_MESSAGE(progressive_branch,
                      "progressive does not branch on drawn_card_playable");
        CHECK_MESSAGE(progressive_keeps_drawing,
                      "progressive else route is not draw_until_playable "
                      "@self");
    }
}

TEST_CASE("vanilla content: reverse expresses the two-player extra advance") {
    const fs::path root = ProjectRoot();
    REQUIRE_MESSAGE(!root.empty(), "could not locate the project root");

    LoadResult load = ScanModsDirectory((root / "mods").string());
    REQUIRE_MESSAGE(load.ok(), "mod load failed: " << JoinErrors(load.errors));

    const LoadedMod* vanilla = nullptr;
    for (const auto& mod : load.mods) {
        if (mod.manifest.id == "vanilla") vanilla = &mod;
    }
    REQUIRE(vanilla != nullptr);

    /* INFO: Legacy ReverseEffect flips direction and, with exactly
     *       two players, pushes an extra advance (standard.cpp:43-49). The
     *       vanilla reverse cards express that as a player_count==2 branch:
     *       reverse_direction -> advance_turn, with the classic else route a
     *       plain reverse_direction so the 3/4-player case is unchanged. */
    int reverse_cards = 0;
    for (const auto& card : vanilla->cards) {
        bool is_reverse = false;
        for (const auto& tag : card.tags) {
            if (tag == "reverse") is_reverse = true;
        }
        if (!is_reverse) continue;
        ++reverse_cards;

        const BehaviorEntry* on_play = nullptr;
        for (const auto& be : card.behaviors) {
            if (be.hook == "on_play") on_play = &be;
        }
        REQUIRE_MESSAGE(on_play != nullptr,
                        "reverse card '" << card.id << "' has no on_play");

        std::map<std::string, const nlohmann::json*> by_id;
        for (const auto& node : on_play->graph.nodes) {
            if (node.is_object() && node.contains("id")
                && node["id"].is_string()) {
                by_id[node["id"].get<std::string>()] = &node;
            }
        }

        bool branch_2p = false;
        bool else_reverses = false;
        bool two_player_extra = false;
        for (const auto& node : on_play->graph.nodes) {
            if (!node.is_object() || !node.contains("cases")) continue;
            const auto& cases = node["cases"];
            if (!cases.is_array()) continue;
            for (const auto& c : cases) {
                if (!c.is_object()) continue;
                const auto when = c.find("when");
                if (when == c.end() || !when->is_object()) continue;
                const auto count = when->find("player_count");
                if (count == when->end() || !count->is_object()) continue;
                if (count->value("cmp", "") != "eq"
                    || count->value("n", 0) != 2) {
                    continue;
                }
                branch_2p = true;
                const auto next = c.find("next");
                if (next == c.end() || !next->is_string()) continue;
                const auto first = by_id.find(next->get<std::string>());
                if (first == by_id.end()) continue;
                if (first->second->value("op", "") != "reverse_direction") {
                    continue;
                }
                const auto second_route = first->second->find("next");
                if (second_route == first->second->end()
                    || !second_route->is_string()) {
                    continue;
                }
                const auto second =
                    by_id.find(second_route->get<std::string>());
                if (second != by_id.end()
                    && second->second->value("op", "") == "advance_turn") {
                    two_player_extra = true;
                }
            }
            const auto else_route = node.find("else");
            if (else_route == node.end() || !else_route->is_string()) continue;
            const auto found = by_id.find(else_route->get<std::string>());
            if (found != by_id.end()
                && found->second->value("op", "") == "reverse_direction"
                && !found->second->contains("next")) {
                else_reverses = true;
            }
        }
        CHECK_MESSAGE(branch_2p,
                      "reverse card '" << card.id
                                       << "' has no player_count==2 branch");
        CHECK_MESSAGE(else_reverses,
                      "reverse card '" << card.id
                                       << "' else route is not plain reverse");
        CHECK_MESSAGE(two_player_extra,
                      "reverse card '" << card.id
                                       << "' has no extra advance");
    }
    CHECK_MESSAGE(reverse_cards == 4, "expected four vanilla reverse cards");
}
