#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_golden_replay_test.cpp
 * @brief Headless golden replay of the legacy transcripts.
 *
 * Reconstructs each `may_differ:false` golden fixture on the new
 * `match::engine` and replays its scripted actions, asserting the fixture's
 * *outcome facts* (current player, direction, active type, per-player hand
 * counts, draw/discard pile sizes, winner, placements). Byte-equality with the
 * legacy `ExportState` is explicitly NOT the target: the new engine exports a
 * deliberately leaner, different shape.
 *
 * Reconstruction recipe: author a synthetic `DeckDef` whose card
 * multiset is the fixture's `load` hands + draw + discard union; assemble it
 * with the scenario's real mods at `starting_cards = 0`; then overwrite the
 * shuffle/deal by arranging hands / draw / discard / current player / direction
 * / active type to the fixture's step-0 state with `ops::MoveCardToZone`.
 *
 * The engine covers the three non-prompt scenarios `standard_baseline`,
 * `no_bluffing_denied` and `force_play`. `draw_stacking` is `may_differ:true`
 * and is skipped by design. The remaining scenarios are the engine.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

/** @brief Human-readable assembly failure. */
std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

/**
 * @brief Walk up from this test file to the repository root.
 */
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

/** @brief Map a legacy `Type` index to the engine colour name. */
std::optional<std::string> ColorName(int index) {
    switch (index) {
        case 0: return "red";
        case 1: return "blue";
        case 2: return "green";
        case 3: return "yellow";
        case 4: return "white";
        default: return std::nullopt;
    }
}

/** @brief Map a legacy `Value` index to the card face label. */
std::string LabelForValue(int value) {
    if (value >= 0 && value <= 9) return std::to_string(value);
    switch (value) {
        case 10: return "skip";
        case 11: return "reverse";
        case 12: return "+2";
        case 13: return "jolly";
        case 14: return "jolly_draw4";
        default: return std::string();
    }
}

/**
 * @struct Content
 * @brief Loaded mods plus the `(color, label) -> kind id` face index.
 */
struct Content {
    std::vector<LoadedMod> mods;
    std::map<std::pair<std::string, std::string>, std::string> kind_by_face;
};

/**
 * @brief Scan the shipped `mods/` tree and index every face.
 */
bool LoadContent(Content& out) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    LoadResult load = ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const CardDef& card : mod.cards) {
            const std::pair<std::string, std::string> face{
                card.face.color.value_or(""), card.face.label.value_or("")};
            out.kind_by_face[face] = card.kind_id;
        }
    }
    return !out.mods.empty();
}

/** @brief Split `text` on `sep`, keeping empty parts. */
std::vector<std::string> Split(const std::string& text, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t pos = text.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

/**
 * @brief Decode an action label's card token to a `(color, label)` face.
 *
 * Tokens seen in the engine fixtures: `red2`, `red3`, `red7`, `draw4_denied`.
 * Kept deliberately small; the engine widens it alongside the remaining
 * scenarios.
 */
std::optional<std::pair<std::string, std::string>> DecodeCardToken(
    const std::string& token) {
    static const char* kColors[] = {"red", "blue", "green", "yellow"};
    for (const char* color : kColors) {
        const std::string prefix(color);
        if (token.rfind(prefix, 0) != 0) continue;
        const std::string rest = token.substr(prefix.size());
        if (!rest.empty()
            && rest.find_first_not_of("0123456789") == std::string::npos) {
            return std::make_pair(prefix, rest);
        }
        if (rest == "draw2") return std::make_pair(prefix, "+2");
        if (rest == "skip") return std::make_pair(prefix, "skip");
        if (rest == "reverse") return std::make_pair(prefix, "reverse");
    }
    if (token.find("draw4") != std::string::npos) {
        return std::make_pair("white", "jolly_draw4");
    }
    if (token == "jolly") return std::make_pair("white", "jolly");
    return std::nullopt;
}

/** @brief Find a face in `username`'s current hand, or nullopt. */
std::optional<ecs::Entity> HandCardByFace(MatchInstance& engine,
                                          const std::string& username,
                                          const std::string& color,
                                          const std::string& label) {
    const std::optional<ecs::Entity> player = engine.FindPlayer(username);
    if (!player.has_value()) return std::nullopt;
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(*player);
    if (hand == nullptr) return std::nullopt;
    for (ecs::Entity card : hand->cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == color && face->label == label) {
            return card;
        }
    }
    return std::nullopt;
}

/**
 * @brief Compare the engine's modelled facts to a fixture step state.
 *
 * Only the facts the new engine models are asserted; legacy-only
 * keys such as `pending_action` / `effect_queue` are intentionally ignored.
 */
void CheckFixtureState(MatchInstance& engine, const json& state,
                       const std::vector<std::string>& usernames,
                       const std::string& scenario,
                       const std::string& action) {
    INFO("scenario=", scenario, " action=", action);
    const json out = engine.ExportState();

    if (state.contains("current_player_index")) {
        const std::size_t index =
            state["current_player_index"].get<std::size_t>();
        REQUIRE(index < usernames.size());
        CHECK(out["current_player"] == usernames[index]);
    }
    if (state.contains("play_direction")) {
        CHECK(out["direction"] == state["play_direction"].get<int>());
    }
    if (state.contains("active_type") && state["active_type"].is_number()) {
        const std::optional<std::string> color =
            ColorName(state["active_type"].get<int>());
        if (color.has_value()) {
            CHECK(out["active_type"] == *color);
        } else {
            CHECK(out["active_type"].is_null());
        }
    }
    if (state.contains("players") && state["players"].is_array()) {
        const json& players = state["players"];
        REQUIRE(out["players"].size() == players.size());
        for (std::size_t p = 0; p < players.size(); ++p) {
            CHECK(out["players"][p]["card_count"]
                  == players[p]["hand"].size());
        }
    }
    if (state.contains("draw_pile")) {
        CHECK(out["draw_pile_size"] == state["draw_pile"].size());
    }
    if (state.contains("discard_pile")) {
        CHECK(out["discard_pile_size"] == state["discard_pile"].size());
    }
    if (state.contains("winner")) {
        CHECK(out["winner"] == state["winner"].get<std::string>());
    }
    if (state.contains("placements")) {
        std::vector<std::string> want;
        for (const json& name : state["placements"]) {
            want.push_back(name.get<std::string>());
        }
        CHECK(engine.GetPlacements() == want);
    }
}

/** @brief Destination bucket of one reconstructed card (setup only). */
enum class Target { kHand, kDraw, kDiscard };

/** @brief One fixture card plus its reconstructed destination. */
struct Placement {
    uint32_t compact = 0;
    Target target = Target::kHand;
    int player = -1;  /**< seat index for `kHand`, else -1. */
};

/**
 * @brief Reconstruct `scenario` on the new engine and replay its actions.
 */
void ReplayScenario(Content& content, const std::string& scenario) {
    const fs::path path = ProjectRoot() / "tests" / "fixtures" / "golden"
                          / (scenario + ".json");
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "cannot open ", path.string());

    json fixture;
    REQUIRE_NOTHROW(fixture = json::parse(in));
    CHECK(fixture.value("scenario", std::string()) == scenario);
    CHECK_EQ(fixture.value("engine", std::string()), "old");
    CHECK_FALSE(fixture.value("may_differ", true));

    REQUIRE(fixture.contains("steps"));
    const json& steps = fixture["steps"];
    REQUIRE_FALSE(steps.empty());

    const json& load = steps[0]["state"];
    const json& players = load["players"];

    std::vector<std::string> usernames;
    for (const json& player : players) {
        usernames.push_back(player["username"].get<std::string>());
    }

    /* INFO: union multiset = load hands + draw pile + discard pile. */
    std::vector<Placement> placements;
    std::map<std::string, int> counts;
    auto add_card = [&](uint32_t compact, Target target, int player) {
        placements.push_back(Placement{compact, target, player});
        const std::string color =
            ColorName(static_cast<int>((compact >> 24) & 0xFF))
                .value_or("");
        const std::string label =
            LabelForValue(static_cast<int>((compact >> 16) & 0xFF));
        const auto it = content.kind_by_face.find({color, label});
        REQUIRE_MESSAGE(it != content.kind_by_face.end(),
                        "no kind for face ", color, " ", label);
        counts[it->second] += 1;
    };
    for (std::size_t p = 0; p < players.size(); ++p) {
        for (const json& card : players[p]["hand"]) {
            add_card(card.get<uint32_t>(), Target::kHand,
                     static_cast<int>(p));
        }
    }
    for (const json& card : load["draw_pile"]) {
        add_card(card.get<uint32_t>(), Target::kDraw, -1);
    }
    for (const json& card : load["discard_pile"]) {
        add_card(card.get<uint32_t>(), Target::kDiscard, -1);
    }

    DeckDef deck;
    deck.id = "golden_" + scenario;
    deck.namespace_id = "golden";
    deck.deck_id = "golden:" + scenario;
    deck.name = "Golden replay " + scenario;
    deck.mods.push_back("vanilla");
    if (load.contains("rules") && load["rules"].is_array()) {
        for (const json& rule : load["rules"]) {
            deck.mods.push_back(rule.get<std::string>());
        }
    }
    for (const auto& entry : counts) {
        deck.cards.push_back({entry.first, entry.second});
    }

    MatchAssemblyOptions options;
    options.starting_cards = 0;
    options.seed = 20260919;
    for (const std::string& username : usernames) {
        MatchPlayerSpec spec;
        spec.username = username;
        options.players.push_back(spec);
    }

    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    MatchAssembly& assembly = *result.assembly;

    /* INFO: index assembled entities by face so each fixture card can be
     *       moved to its recorded zone in fixture order. */
    std::map<std::pair<std::string, std::string>, std::vector<ecs::Entity>>
        available;
    for (ecs::Entity card : assembly.registries.cards) {
        const ecs::FaceSpec* face = assembly.store.Get<ecs::FaceSpec>(card);
        if (face == nullptr) continue;
        available[{face->color, face->label}].push_back(card);
    }

    const std::vector<ecs::Entity>& seats = assembly.registries.players;
    for (const Placement& placement : placements) {
        const std::string color =
            ColorName(static_cast<int>((placement.compact >> 24) & 0xFF))
                .value_or("");
        const std::string label = LabelForValue(
            static_cast<int>((placement.compact >> 16) & 0xFF));
        auto it = available.find({color, label});
        REQUIRE(it != available.end());
        REQUIRE_FALSE(it->second.empty());
        const ecs::Entity card = it->second.back();
        it->second.pop_back();

        ecs::ZoneRef zone;
        if (placement.target == Target::kHand) {
            zone = ecs::ZoneRef{ecs::ZoneKind::kHand,
                                seats[static_cast<std::size_t>(
                                    placement.player)]};
        } else if (placement.target == Target::kDraw) {
            zone = ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}};
        } else {
            zone = ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}};
        }
        REQUIRE(ops::MoveCardToZone(assembly.store, card, zone));
    }
    /* INFO: the synthetic deck is exactly the fixture multiset, so every
     *       assembled entity must have been placed. */
    for (const auto& entry : available) {
        CHECK(entry.second.empty());
    }

    const int current_index = load["current_player_index"].get<int>();
    for (std::size_t i = 0; i < seats.size(); ++i) {
        if (ecs::TurnState* turn = assembly.store.Get<ecs::TurnState>(
                seats[i])) {
            turn->is_current = (static_cast<int>(i) == current_index);
        }
    }
    if (ecs::MatchMeta* meta = assembly.store.Get<ecs::MatchMeta>(
            assembly.registries.match)) {
        meta->direction = load["play_direction"].get<int>() == 1
                              ? ecs::Direction::kForward
                              : ecs::Direction::kReverse;
    }
    if (ecs::ActiveTypeReq* active = assembly.store.Get<ecs::ActiveTypeReq>(
            assembly.registries.match)) {
        active->type = ColorName(load["active_type"].get<int>());
    }

    auto engine =
        std::make_unique<MatchInstance>(std::move(result.assembly));

    CheckFixtureState(*engine, load, usernames, scenario, "load");

    for (std::size_t i = 1; i < steps.size(); ++i) {
        const std::string action = steps[i].value("action", std::string());
        const std::vector<std::string> parts = Split(action, ':');
        REQUIRE_FALSE(parts.empty());
        if (parts[0] == "draw") {
            REQUIRE(parts.size() >= 2);
            CHECK(engine->DrawCard(parts[1]));
        } else if (parts[0] == "play") {
            REQUIRE(parts.size() >= 3);
            const std::string& who = parts[1];
            const std::string& token = parts[2];
            const auto face = DecodeCardToken(token);
            REQUIRE_MESSAGE(face.has_value(), "unknown card token ", token);
            const auto card = HandCardByFace(*engine, who, face->first,
                                             face->second);
            REQUIRE_MESSAGE(card.has_value(), "no ", face->first, " ",
                            face->second, " in ", who, "'s hand");
            const bool accepted = engine->PlayCard(who, *card);
            const bool denied = token.find("denied") != std::string::npos;
            CHECK(accepted == !denied);
        } else {
            FAIL_CHECK("unhandled action: ", action);
        }
        CheckFixtureState(*engine, steps[i]["state"], usernames, scenario,
                          action);
    }
}

}  // namespace

/**
 * @brief Standard two-player baseline: draw, legal play, turn advance.
 */
TEST_CASE("golden replay: standard_baseline matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "standard_baseline");
}

/**
 * @brief no_bluffing: the +4 denial leaves state untouched, then red7 plays.
 */
TEST_CASE("golden replay: no_bluffing_denied matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "no_bluffing_denied");
}

/**
 * @brief force_play: a playable drawn card auto-plays through the pipeline.
 */
TEST_CASE("golden replay: force_play matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "force_play");
}

/**
 * @brief draw_stacking is `may_differ:true` and explicitly not replayed.
 *
 *  rebuilds draw stacking on response windows, so its transcript is
 * expected to diverge by design. The legacy capture
 * suite still verifies its shape.
 */
TEST_CASE("golden replay: draw_stacking is explicitly skipped") {
    const fs::path path = ProjectRoot() / "tests" / "fixtures" / "golden"
                          / "draw_stacking.json";
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "cannot open ", path.string());
    json fixture;
    REQUIRE_NOTHROW(fixture = json::parse(in));
    CHECK_EQ(fixture.value("scenario", std::string()), "draw_stacking");
    CHECK(fixture.value("may_differ", false));
    MESSAGE("skipped draw_stacking: may_differ=true by design");
}
