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
#include <functional>
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
 * The engine covered the three non-prompt scenarios `standard_baseline`,
 * `no_bluffing_denied` and `force_play`. The engine extends the harness to the
 * remaining scenarios (`jump_in`, `seven_zero_swap`,
 * `seven_zero_zero_rotate`, `no_bluffing_allowed`, `prompt_choose_color`,
 * `progressive`, `bot_turn`). `draw_stacking` is `may_differ:true` and is
 * skipped by design.
 *
 * Where a legacy step depends on a model the new engine does not have (the
 * legacy `pending_action` prompt trio, the legacy `TakeBotTurn`), the scenario
 * asserts the new engine's *available* outcome and records the divergence in
 * the explicit `KnownGaps()` list below; nothing is faked.
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

/** @brief Decode a legacy card integer to its `(color, label)` face. */
std::pair<std::string, std::string> DecodeCompact(uint32_t compact) {
    return {ColorName(static_cast<int>((compact >> 24) & 0xFF)).value_or(""),
            LabelForValue(static_cast<int>((compact >> 16) & 0xFF))};
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
 * @brief Decode a bare card face token (`red2`, `blue_5`, `draw4`).
 *
 * The engine widened the vocabulary: every colour prefix, numeric labels,
 * `draw2`/`+2`, `skip`, `reverse`, `jolly`, `draw4`/`jolly_draw4`.
 */
std::optional<std::pair<std::string, std::string>> DecodeFace(
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
    if (token == "draw4" || token == "jolly_draw4") {
        return std::make_pair("white", "jolly_draw4");
    }
    if (token == "jolly") return std::make_pair("white", "jolly");
    return std::nullopt;
}

/**
 * @brief Decode an action label's card token to a `(color, label)` face.
 *
 * Action labels may carry a descriptive suffix (`red5_out_of_turn`,
 * `draw4_denied`); the leading face token is decoded and the suffix ignored.
 */
std::optional<std::pair<std::string, std::string>> DecodeCardToken(
    const std::string& token) {
    if (const auto whole = DecodeFace(token); whole.has_value()) {
        return whole;
    }
    const std::size_t underscore = token.find('_');
    if (underscore != std::string::npos) {
        return DecodeFace(token.substr(0, underscore));
    }
    return std::nullopt;
}

/** @brief Map an `input:` action's value token to its engine payload. */
json InputValue(const std::string& token) {
    if (token == "swap_with_Bob") return json("Bob");
    if (token == "keep") return json(1);
    return json(token);
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

/** @brief Play a named face from `username`'s hand; REQUIREs it is held. */
bool PlayFace(MatchInstance& engine, const std::string& username,
              const std::string& token) {
    const auto face = DecodeCardToken(token);
    REQUIRE_MESSAGE(face.has_value(), "unknown card token ", token);
    const auto card =
        HandCardByFace(engine, username, face->first, face->second);
    REQUIRE_MESSAGE(card.has_value(), "no ", face->first, " ", face->second,
                    " in ", username, "'s hand");
    return engine.PlayCard(username, *card);
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
 * @struct Fixture
 * @brief A parsed golden fixture plus the usernames in seat order.
 */
struct Fixture {
    json steps = json::array();
    json load = json::object();
    std::vector<std::string> usernames;
};

/**
 * @brief Reconstruct `scenario` on the new engine, arranged to step-0 state.
 *
 * Parses the fixture, authors the synthetic union deck, assembles it with the
 * scenario's real mods, moves every card to its recorded zone and applies the
 * fixture's current player / direction / active type. Returns the started
 * engine; fills `fixture` for the caller's assertions.
 */
std::unique_ptr<MatchInstance> BuildScenario(Content& content,
                                             const std::string& scenario,
                                             Fixture& fixture) {
    const fs::path path = ProjectRoot() / "tests" / "fixtures" / "golden"
                          / (scenario + ".json");
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "cannot open ", path.string());

    json parsed;
    REQUIRE_NOTHROW(parsed = json::parse(in));
    CHECK(parsed.value("scenario", std::string()) == scenario);
    CHECK_EQ(parsed.value("engine", std::string()), "old");
    CHECK_FALSE(parsed.value("may_differ", true));

    REQUIRE(parsed.contains("steps"));
    fixture.steps = parsed["steps"];
    REQUIRE_FALSE(fixture.steps.empty());

    fixture.load = fixture.steps[0]["state"];
    const json& load = fixture.load;
    const json& players = load["players"];

    for (const json& player : players) {
        fixture.usernames.push_back(player["username"].get<std::string>());
    }

    /* INFO: union multiset = load hands + draw pile + discard pile. */
    std::vector<Placement> placements;
    std::map<std::string, int> counts;
    auto add_card = [&](uint32_t compact, Target target, int player) {
        placements.push_back(Placement{compact, target, player});
        const auto face = DecodeCompact(compact);
        const auto it = content.kind_by_face.find(face);
        REQUIRE_MESSAGE(it != content.kind_by_face.end(),
                        "no kind for face ", face.first, " ", face.second);
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
    for (std::size_t p = 0; p < fixture.usernames.size(); ++p) {
        MatchPlayerSpec spec;
        spec.username = fixture.usernames[p];
        if (players[p].contains("is_bot")) {
            spec.is_bot = players[p]["is_bot"].get<bool>();
        }
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
        const auto face = DecodeCompact(placement.compact);
        auto it = available.find(face);
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

    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

/**
 * @struct KnownGap
 * @brief A legacy golden step the new engine cannot reproduce yet.
 */
struct KnownGap {
    std::string scenario;
    std::string step;
    std::string reason;
};

/**
 * @brief The explicit known-gap list.
 *
 * Each entry names a legacy transcript step whose flow depends on a model the
 * new `match::engine` does not have. The owning scenario asserts the new
 * engine's available outcome instead of the legacy one; nothing is faked.
 */
const std::vector<KnownGap>& KnownGaps() {
    static const std::vector<KnownGap> gaps = {
        {"progressive", "draw:Alice:until_playable",
         "legacy pending_action kPlayDrawn pauses with Alice current (hand 4, "
         "discard 1); the new engine's re-entrant after:draw -> "
         "draw_until_playable path keeps drawing until the source is "
         "exhausted, keeps the discard top as the active pile, and advances "
         "to Bob, with no play-or-keep prompt"},
        {"progressive", "input:Alice:keep",
         "no new-engine analogue of the legacy kPlayDrawn keep/play input"},
        {"bot_turn", "bot:Bob",
         "legacy TakeBotTurn; the new engine exposes no bot-turn API, so the step "
         "is not replayed"},
    };
    return gaps;
}

/** @brief Run one scripted action through the new engine API. */
void RunAction(MatchInstance& engine, const std::string& action) {
    const std::vector<std::string> parts = Split(action, ':');
    REQUIRE_FALSE(parts.empty());
    if (parts[0] == "draw") {
        REQUIRE(parts.size() >= 2);
        CHECK(engine.DrawCard(parts[1]));
    } else if (parts[0] == "play") {
        REQUIRE(parts.size() >= 3);
        const bool accepted = PlayFace(engine, parts[1], parts[2]);
        const bool denied = parts[2].find("denied") != std::string::npos;
        CHECK(accepted == !denied);
    } else if (parts[0] == "input") {
        REQUIRE(parts.size() >= 3);
        CHECK(engine.SubmitInput(parts[1], InputValue(parts[2])));
    } else {
        FAIL_CHECK("unhandled action: ", action);
    }
}

/** @brief Optional per-action hook for new-engine-only assertions. */
using ExtraCheck =
    std::function<void(MatchInstance&, const std::string& action)>;

/**
 * @brief Reconstruct `scenario` on the new engine and replay its actions.
 *
 * Asserts the fixture's modelled outcome facts after every step. `extra` runs
 * after each action and before its assertions, for checks the legacy fixture
 * does not carry (e.g. an open response window).
 */
void ReplayScenario(Content& content, const std::string& scenario,
                    const ExtraCheck& extra = {}) {
    Fixture fixture;
    std::unique_ptr<MatchInstance> engine =
        BuildScenario(content, scenario, fixture);

    CheckFixtureState(*engine, fixture.load, fixture.usernames, scenario,
                      "load");

    for (std::size_t i = 1; i < fixture.steps.size(); ++i) {
        const std::string action =
            fixture.steps[i].value("action", std::string());
        RunAction(*engine, action);
        if (extra) extra(*engine, action);
        CheckFixtureState(*engine, fixture.steps[i]["state"],
                          fixture.usernames, scenario, action);
    }
}

/**
 * @brief progressive: assert the available draw outcome, record the gap.
 */
void ReplayProgressive(Content& content) {
    Fixture fixture;
    std::unique_ptr<MatchInstance> engine =
        BuildScenario(content, "progressive", fixture);
    CheckFixtureState(*engine, fixture.load, fixture.usernames, "progressive",
                      "load");

    /* INFO: step 1 - draw until playable. Known gap the engineE-GAP-003: the legacy
     *       flow pauses on kPlayDrawn with Alice current (hand 4, discard 1);
     *       the new engine's re-entrant after:draw -> draw_until_playable path
     *       keeps drawing until the draw source is exhausted, keeps the
     *       discard top as the active pile, and advances to Bob with no
     *       play-or-keep prompt. The available outcome is asserted below. */
    REQUIRE(engine->DrawCard("Alice"));
    const json after_draw = engine->ExportState();
    CHECK(after_draw["players"][0]["card_count"] == 4);
    CHECK(after_draw["draw_pile_size"] == 0);
    CHECK(after_draw["discard_pile_size"] == 1);
    CHECK_FALSE(engine->PendingInput().has_value());
    CHECK(after_draw["current_player"] == "Bob");

    /* INFO: step 2 - no keep/play prompt is parked, so the input is refused.
     *       Known gap the engineE-GAP-004. */
    CHECK_FALSE(engine->SubmitInput("Alice", json("keep")));
}

/**
 * @brief bot_turn: assert the load outcome, record the TakeBotTurn gap.
 */
void ReplayBotTurn(Content& content) {
    Fixture fixture;
    std::unique_ptr<MatchInstance> engine =
        BuildScenario(content, "bot_turn", fixture);
    CheckFixtureState(*engine, fixture.load, fixture.usernames, "bot_turn",
                      "load");

    /* INFO: the only transcript action is `bot:Bob` (legacy TakeBotTurn).
     *       Bots are the bot layer; the new engine exposes no bot-turn API, so
     *       the step is not replayed. Known gap the engineE-GAP-005. Assert the
     *       available load outcome; never fake a bot decision. */
    const json out = engine->ExportState();
    CHECK(out["current_player"] == "Bob");
    CHECK(out["discard_pile_size"] == 1);
    CHECK(out["players"][1]["card_count"] == 2);
    CHECK(out["players"][1]["is_bot"] == true);
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
 *
 * The fixture's auto-played card is `197108` (red3), not the green4 the brief's
 * scenario table names; the fixture is authoritative.
 */
TEST_CASE("golden replay: force_play matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "force_play");
}

/**
 * @brief jump_in: an out-of-turn identical card is rescued by the allow entry.
 *
 * The new engine opens the jump-in response window (legacy had none);
 * the asserted outcome facts still match. No known gap.
 */
TEST_CASE("golden replay: jump_in matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "jump_in",
                   [](MatchInstance& engine, const std::string& action) {
                       if (action == "play:Bob:red5_out_of_turn") {
                           CHECK(engine.WindowOpen());
                       }
                   });
}

/**
 * @brief seven_zero swap: the 7 prompts for a target, then the hands swap.
 *
 * `@choose_player` now expands to a real `choose_player` prompt
 * so the fixture's legacy outcome is reproduced exactly: the play parks a
 * prompt with Alice current, and answering with Bob swaps the hands and
 * settles the turn to Bob.
 */
TEST_CASE("golden replay: seven_zero_swap matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "seven_zero_swap");
}

/**
 * @brief seven_zero zero: a 0 rotates every hand one seat forward.
 */
TEST_CASE("golden replay: seven_zero_zero_rotate matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "seven_zero_zero_rotate");
}

/**
 * @brief no_bluffing: a legal +4 prompts for a colour, then draws 4 and skips.
 */
TEST_CASE("golden replay: no_bluffing_allowed matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "no_bluffing_allowed",
                   [](MatchInstance& engine, const std::string& action) {
                       if (action == "play:Alice:draw4") {
                           const std::optional<json> pending =
                               engine.PendingInput();
                           REQUIRE(pending.has_value());
                           CHECK((*pending)["kind"] == "choose_color");
                       }
                   });
}

/**
 * @brief Standard Jolly prompts for a colour; SubmitInput resolves it.
 */
TEST_CASE("golden replay: prompt_choose_color matches the legacy outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayScenario(content, "prompt_choose_color",
                   [](MatchInstance& engine, const std::string& action) {
                       if (action == "play:Alice:jolly") {
                           const std::optional<json> pending =
                               engine.PendingInput();
                           REQUIRE(pending.has_value());
                           CHECK((*pending)["kind"] == "choose_color");
                       }
                   });
}

/**
 * @brief progressive: draw-until-playable replays; the kPlayDrawn prompt is a
 *        known gap.
 */
TEST_CASE("golden replay: progressive replays the available outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayProgressive(content);
}

/**
 * @brief bot_turn: the legacy TakeBotTurn step is a known gap.
 */
TEST_CASE("golden replay: bot_turn replays the available outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    ReplayBotTurn(content);
}

/**
 * @brief The explicit known-gap list is present and well-formed.
 */
TEST_CASE("golden replay: known gaps are explicitly listed") {
    const std::vector<KnownGap>& gaps = KnownGaps();
    CHECK(gaps.size() == 3);
    for (const KnownGap& gap : gaps) {
        MESSAGE("known gap: ", gap.scenario, " / ", gap.step, " - ",
                gap.reason);
        CHECK_FALSE(gap.scenario.empty());
        CHECK_FALSE(gap.step.empty());
        CHECK_FALSE(gap.reason.empty());
    }
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
