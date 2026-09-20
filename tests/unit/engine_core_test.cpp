#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/ecs/compact_card.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

/**
 * @file engine_core_test.cpp
 * @brief `match::engine::MatchInstance` acceptance tests.
 *
 * Assembles real matches from the shipped `mods/` tree and the vanilla classic
 * deck, then scripts draw / legal play / illegal play / turn advance / win
 * through the public engine surface and asserts the resulting state and event
 * stream.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

/* INFO: locate the project root from this file so the test is cwd-independent
 *       (mirrors engine_assembler_test.cpp). */
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

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

bool LoadContent(Content& out) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    LoadResult load = ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "vanilla:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

MatchAssemblyOptions Players(int count, int starting_cards, uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = starting_cards;
    options.seed = seed;
    for (int i = 0; i < count; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    return options;
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          int cards, uint64_t seed) {
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, content.classic, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

std::vector<ecs::Entity> CardsByKind(const MatchInstance& engine,
                                     const std::string& kind) {
    std::vector<ecs::Entity> out;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind) {
            out.push_back(card);
        }
    }
    return out;
}

std::size_t HandSize(const MatchInstance& engine, ecs::Entity player) {
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    return hand == nullptr ? 0 : hand->cards.size();
}

std::size_t PileSize(const MatchInstance& engine, ecs::PileKind kind) {
    for (ecs::Entity pile :
         engine.Store().EntitiesWith<ecs::PileContents>()) {
        const ecs::PileContents* contents =
            engine.Store().Get<ecs::PileContents>(pile);
        if (contents != nullptr && contents->kind == kind) {
            return contents->cards.size();
        }
    }
    return 0;
}

/** @brief Replace a player's hand with exactly `cards` (test setup). */
void ForceHand(MatchInstance& engine, ecs::Entity player,
               const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kDrawPile,
                                         ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

/** @brief Move every draw-pile card into the discard (reshuffle setup). */
void EmptyDrawIntoDiscard(MatchInstance& engine) {
    ecs::PileContents* draw = engine.Store().Get<ecs::PileContents>(
        engine.Registries().draw_pile);
    REQUIRE(draw != nullptr);
    const std::vector<ecs::Entity> cards = draw->cards;
    for (ecs::Entity card : cards) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kDiscardPile,
                                         ecs::Entity{}});
    }
}

const nlohmann::json* FindEvent(const MatchInstance& engine,
                                const std::string& type) {
    for (const nlohmann::json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) {
            return &event;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("engine core: start exposes a live headless state") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7, 42);

    CHECK_FALSE(engine->IsMatchOver());
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    CHECK(engine->GetCurrentPlayer().has_value());
    CHECK(engine->FindPlayer("player0").has_value());
    CHECK_FALSE(engine->FindPlayer("ghost").has_value());
    CHECK(engine->PendingInput() == std::nullopt);
    CHECK_FALSE(engine->SubmitInput("player0", nlohmann::json::object()));

    const nlohmann::json state = engine->ExportState();
    CHECK(state["status"] == "playing");
    CHECK(state["started"] == true);
    CHECK(state["current_player"] == "player0");
    CHECK(state["round"] == 0u);
    CHECK(state["direction"] == 1);
    CHECK(state["active_type"].is_string());
    CHECK(state["players"].size() == 4);
    CHECK(state["draw_pile_size"] == 79);
    CHECK(state["discard_pile_size"] == 1);
    CHECK(state["top_card"].is_object());
    CHECK(state["winner"] == "");
    CHECK(state["placements"].empty());
}

TEST_CASE("engine core: legal play discards and advances the turn") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7, 42);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    REQUIRE(wilds.size() >= 2);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});

    const std::size_t discard_before =
        PileSize(*engine, ecs::PileKind::kDiscard);
    REQUIRE(engine->PlayCard("player0", wilds[0]));
    // INFO: vanilla:wild's on_play behavior opens a choose_color prompt
    // the engine pauses until the actor submits a colour.
    REQUIRE(engine->PendingInput().has_value());
    REQUIRE(engine->SubmitInput("player0", "red"));
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    CHECK_FALSE(engine->IsMatchOver());

    CHECK(HandSize(*engine, player0) == 1);
    CHECK(PileSize(*engine, ecs::PileKind::kDiscard) == discard_before + 1);

    const nlohmann::json state = engine->ExportState();
    CHECK(state["active_type"] == "red");
    REQUIRE(state["last_play"].is_object());
    CHECK(state["last_play"]["player"] == "player0");
    CHECK(state["last_play"]["card"] == "vanilla:wild");

    REQUIRE(FindEvent(*engine, "card_played") != nullptr);
    REQUIRE(FindEvent(*engine, "card_left_zone") != nullptr);
    REQUIRE(FindEvent(*engine, "card_entered_zone") != nullptr);
    REQUIRE(FindEvent(*engine, "turn_advance") != nullptr);
    CHECK(FindEvent(*engine, "play_rejected") == nullptr);
}

TEST_CASE("engine core: out-of-turn play is rejected with a reason") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7, 42);

    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Hand* hand = engine->Store().Get<ecs::Hand>(player1);
    REQUIRE(hand != nullptr);
    REQUIRE_FALSE(hand->cards.empty());
    const ecs::Entity card = hand->cards.front();

    const std::size_t discard_before =
        PileSize(*engine, ecs::PileKind::kDiscard);
    const std::size_t hand_before = HandSize(*engine, player1);

    CHECK_FALSE(engine->PlayCard("player1", card));
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    CHECK(HandSize(*engine, player1) == hand_before);
    CHECK(PileSize(*engine, ecs::PileKind::kDiscard) == discard_before);

    const nlohmann::json* rejected = FindEvent(*engine, "play_rejected");
    REQUIRE(rejected != nullptr);
    CHECK((*rejected)["payload"]["player"] == "player1");
    CHECK((*rejected)["payload"]["reason_id"] == "vanilla:turn_order");
}

TEST_CASE("engine core: unowned card is rejected by must_own_card") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7, 42);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    REQUIRE_FALSE(wilds.empty());
    const ecs::Entity player0 = *engine->FindPlayer("player0");

    // INFO: empty hand -> turn_order passes (in turn), a wild passes
    //       match_type_or_value, must_own_card denies.
    ForceHand(*engine, player0, {});

    CHECK_FALSE(engine->PlayCard("player0", wilds[0]));
    const nlohmann::json* rejected = FindEvent(*engine, "play_rejected");
    REQUIRE(rejected != nullptr);
    CHECK((*rejected)["payload"]["reason_id"] == "vanilla:must_own_card");
    CHECK_FALSE(engine->IsMatchOver());
}

TEST_CASE("engine core: draw grows the hand and shrinks the draw pile") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7, 42);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::size_t hand_before = HandSize(*engine, player0);
    const std::size_t draw_before =
        PileSize(*engine, ecs::PileKind::kDraw);

    REQUIRE(engine->DrawCard("player0"));
    CHECK(HandSize(*engine, player0) == hand_before + 1);
    CHECK(PileSize(*engine, ecs::PileKind::kDraw) == draw_before - 1);
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    REQUIRE(FindEvent(*engine, "cards_drawn") != nullptr);

    // INFO: a non-current player cannot draw.
    CHECK_FALSE(engine->DrawCard("player0"));
}

TEST_CASE("engine core: draw reshuffles the discard when the pile empties") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7, 42);

    EmptyDrawIntoDiscard(*engine);
    CHECK(PileSize(*engine, ecs::PileKind::kDraw) == 0);
    const std::size_t discard_before =
        PileSize(*engine, ecs::PileKind::kDiscard);
    REQUIRE(discard_before > 1);

    REQUIRE(engine->DrawCard("player0"));
    CHECK(PileSize(*engine, ecs::PileKind::kDraw) == discard_before - 2);
    CHECK(PileSize(*engine, ecs::PileKind::kDiscard) == 1);

    const nlohmann::json* reshuffle = FindEvent(*engine, "reshuffle");
    REQUIRE(reshuffle != nullptr);
    CHECK((*reshuffle)["payload"]["discard_size"] == 1);
    REQUIRE(FindEvent(*engine, "cards_drawn") != nullptr);
}

TEST_CASE("engine core: emptying the hand wins and records placement") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7, 42);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    REQUIRE_FALSE(wilds.empty());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ForceHand(*engine, player0, {wilds[0]});

    REQUIRE(engine->PlayCard("player0", wilds[0]));
    REQUIRE(engine->SubmitInput("player0", "red"));
    CHECK(engine->IsMatchOver());
    CHECK(engine->GetWinner() == "player0");
    CHECK(engine->GetPlacements() == std::vector<std::string>{"player0"});

    const nlohmann::json state = engine->ExportState();
    CHECK(state["status"] == "finished");
    CHECK(state["winner"] == "player0");
    CHECK(state["placements"].size() == 1);

    const nlohmann::json* placement = FindEvent(*engine, "placement");
    REQUIRE(placement != nullptr);
    CHECK((*placement)["payload"]["player"] == "player0");
    CHECK((*placement)["payload"]["place"] == 1);

    // INFO: a finished match refuses further input.
    CHECK_FALSE(engine->DrawCard("player1"));
    CHECK_FALSE(engine->PlayCard("player0", wilds[0]));
}

TEST_CASE("engine core: tick advances the round on a full seat cycle") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    REQUIRE(wilds.size() >= 4);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});
    ForceHand(*engine, player1, {wilds[2], wilds[3]});

    REQUIRE(engine->PlayCard("player0", wilds[0]));
    REQUIRE(engine->SubmitInput("player0", "red"));
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    engine->Tick();
    CHECK(engine->ExportState()["round"] == 0u);

    REQUIRE(engine->PlayCard("player1", wilds[2]));
    REQUIRE(engine->SubmitInput("player1", "blue"));
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    engine->Tick();
    CHECK(engine->ExportState()["round"] == 1u);

    const nlohmann::json* round = FindEvent(*engine, "round_advance");
    REQUIRE(round != nullptr);
    CHECK((*round)["payload"]["round"] == 1);
}
