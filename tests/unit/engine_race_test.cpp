#include <doctest/doctest.h>

#include <common/lobby.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/hooks.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/match_session.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_race_test.cpp
 * @brief Race mode: finishers leave the rotation, the match ends once the
 *        fixed target finished, the rest rank by fewest cards then seat.
 *
 * Every case assembles a real vanilla classic match and scripts it through
 * the public engine surface, like engine_core_test.cpp.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;

namespace {

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

std::unique_ptr<MatchInstance> MakeRace(Content& content, int players,
                                        uint32_t race_target) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    options.race_target = race_target;
    for (int i = 0; i < players; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE(result.ok());
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

/** @brief Replace a player's hand with exactly `cards` (test setup). */
void ForceHand(MatchInstance& engine, const std::string& username,
               const std::vector<ecs::Entity>& cards) {
    const ecs::Entity player = *engine.FindPlayer(username);
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

/** @brief Give a player `count` cards of a kind nobody plays in the test. */
void ForceHandSize(MatchInstance& engine, const std::string& username,
                   std::size_t count) {
    std::vector<ecs::Entity> pool;
    for (int value = 1; value <= 9; ++value) {
        const std::vector<ecs::Entity> kind = CardsByKind(
            engine, "vanilla:blue_" + std::to_string(value));
        pool.insert(pool.end(), kind.begin(), kind.end());
    }
    std::vector<ecs::Entity> chosen;
    for (ecs::Entity card : pool) {
        if (chosen.size() == count) break;
        const std::optional<ecs::ZoneRef> zone =
            ops::FindCardZone(engine.Store(), card);
        if (!zone.has_value() || zone->kind != ecs::ZoneKind::kDrawPile) {
            continue;
        }
        chosen.push_back(card);
    }
    REQUIRE(chosen.size() == count);
    ForceHand(engine, username, chosen);
}

std::size_t HandSize(const MatchInstance& engine,
                     const std::string& username) {
    const ecs::Hand* hand =
        engine.Store().Get<ecs::Hand>(*engine.FindPlayer(username));
    return hand == nullptr ? 0 : hand->cards.size();
}

/** @brief Make `color` the active type so a coloured card is legal. */
void SetActiveType(MatchInstance& engine, const std::string& color) {
    ecs::ActiveTypeReq* req = engine.Store().Get<ecs::ActiveTypeReq>(
        engine.Registries().match);
    REQUIRE(req != nullptr);
    req->type = color;
}

/** @brief `username` plays their only wild and picks red. */
void FinishWithWild(MatchInstance& engine, const std::string& username,
                    ecs::Entity wild) {
    ForceHand(engine, username, {wild});
    REQUIRE(engine.PlayCard(username, wild));
    REQUIRE(engine.PendingInput().has_value());
    REQUIRE(engine.SubmitInput(username, "red"));
}

/** @brief The current player draws once, passing the turn. */
void DrawAndPass(MatchInstance& engine, const std::string& expected) {
    REQUIRE(engine.GetCurrentPlayerUsername() == expected);
    REQUIRE(engine.DrawCard(expected));
}

}  // namespace

TEST_SUITE("engine race") {

TEST_CASE("race: standard assembly keeps race off and exposes the mode") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 0);

    CHECK(engine->GetMode() == "standard");
    CHECK(engine->GetRaceTarget() == 0u);
    const nlohmann::json state = engine->ExportState();
    CHECK(state["mode"] == "standard");
    CHECK(state["race_target"] == 0u);
}

TEST_CASE("race: the target is clamped to seats - 1 at assembly") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 9);
    CHECK(engine->GetMode() == "race");
    CHECK(engine->GetRaceTarget() == 3u);
}

TEST_CASE("race: the client snapshot carries mode, target and placements") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 2);
    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    FinishWithWild(*engine, "player0", wilds[0]);

    match::view::ViewBuilder builder(*engine, content.mods);
    match::view::EventSink sink;
    const nlohmann::json snapshot = builder.BuildSnapshot(
        match::view::Viewer::Player("player1"), sink);
    const nlohmann::json& state = snapshot["match_state"];
    CHECK(state["mode"] == "race");
    CHECK(state["race_target"] == 2u);
    CHECK(state["placements"] == nlohmann::json::array({"player0"}));
    CHECK(state["winner"] == "");
    CHECK(state["status"] == "playing");
}

TEST_CASE("race: two seats end on the first finisher like standard") {
    Content content;
    REQUIRE(LoadContent(content));
    LobbySettings settings;
    settings.mode = "race";
    settings.race_percent = 50;
    std::unique_ptr<MatchInstance> engine = MakeRace(
        content, 2, static_cast<uint32_t>(settings.RaceTarget(2)));
    REQUIRE(engine->GetRaceTarget() == 1u);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    FinishWithWild(*engine, "player0", wilds[0]);

    CHECK(engine->IsMatchOver());
    CHECK(engine->GetWinner() == "player0");
    CHECK(engine->GetPlacements()
          == std::vector<std::string>{"player0", "player1"});
}

TEST_CASE("race: four seats at 50% end after two finishers") {
    Content content;
    REQUIRE(LoadContent(content));
    LobbySettings settings;
    settings.mode = "race";
    settings.race_percent = 50;
    std::unique_ptr<MatchInstance> engine = MakeRace(
        content, 4, static_cast<uint32_t>(settings.RaceTarget(4)));
    REQUIRE(engine->GetRaceTarget() == 2u);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    REQUIRE(wilds.size() >= 2);

    FinishWithWild(*engine, "player0", wilds[0]);
    CHECK_FALSE(engine->IsMatchOver());
    CHECK(engine->GetWinner().empty());
    CHECK(engine->GetPlacements() == std::vector<std::string>{"player0"});
    CHECK(engine->GetCurrentPlayerUsername() == "player1");

    nlohmann::json state = engine->ExportState();
    CHECK(state["mode"] == "race");
    CHECK(state["race_target"] == 2u);
    CHECK(state["status"] == "playing");
    CHECK(state["placements"] == nlohmann::json::array({"player0"}));

    ForceHandSize(*engine, "player2", 5);
    ForceHandSize(*engine, "player3", 2);
    FinishWithWild(*engine, "player1", wilds[1]);

    CHECK(engine->IsMatchOver());
    CHECK(engine->GetWinner() == "player0");
    CHECK(engine->GetPlacements()
          == std::vector<std::string>{"player0", "player1", "player3",
                                      "player2"});
    state = engine->ExportState();
    CHECK(state["status"] == "finished");
    CHECK(state["winner"] == "player0");

    int match_end_events = 0;
    for (const nlohmann::json& event : engine->Events()) {
        if (event.value("type", "") == "match_end") ++match_end_events;
    }
    CHECK(match_end_events == 1);
}

TEST_CASE("race: losers tied on cards keep seat order") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 2);
    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");

    FinishWithWild(*engine, "player0", wilds[0]);
    ForceHandSize(*engine, "player2", 3);
    ForceHandSize(*engine, "player3", 3);
    FinishWithWild(*engine, "player1", wilds[1]);

    REQUIRE(engine->IsMatchOver());
    CHECK(engine->GetPlacements()
          == std::vector<std::string>{"player0", "player1", "player2",
                                      "player3"});
}

TEST_CASE("race: finished seats leave the turn rotation") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 3);
    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");

    FinishWithWild(*engine, "player0", wilds[0]);
    DrawAndPass(*engine, "player1");
    DrawAndPass(*engine, "player2");
    DrawAndPass(*engine, "player3");
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    CHECK(HandSize(*engine, "player0") == 0);
}

TEST_CASE("race: a finisher's last skip still skips the next live seat") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 2);
    const std::vector<ecs::Entity> skips =
        CardsByKind(*engine, "vanilla:red_skip");

    SetActiveType(*engine, "red");
    ForceHand(*engine, "player0", {skips[0]});
    REQUIRE(engine->PlayCard("player0", skips[0]));

    CHECK_FALSE(engine->IsMatchOver());
    CHECK(engine->GetPlacements() == std::vector<std::string>{"player0"});
    CHECK(engine->GetCurrentPlayerUsername() == "player2");
    DrawAndPass(*engine, "player2");
    DrawAndPass(*engine, "player3");
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("race: a finisher's last reverse turns play around them") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 2);
    const std::vector<ecs::Entity> reverses =
        CardsByKind(*engine, "vanilla:red_reverse");

    SetActiveType(*engine, "red");
    ForceHand(*engine, "player0", {reverses[0]});
    REQUIRE(engine->PlayCard("player0", reverses[0]));

    CHECK_FALSE(engine->IsMatchOver());
    CHECK(engine->GetCurrentPlayerUsername() == "player3");
    DrawAndPass(*engine, "player3");
    DrawAndPass(*engine, "player2");
    DrawAndPass(*engine, "player1");
    CHECK(engine->GetCurrentPlayerUsername() == "player3");
}

TEST_CASE("race: a finisher's last draw two hits the next live seat") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 2);
    const std::vector<ecs::Entity> draw_twos =
        CardsByKind(*engine, "vanilla:red_draw2");

    SetActiveType(*engine, "red");
    ForceHandSize(*engine, "player1", 3);
    ForceHand(*engine, "player0", {draw_twos[0]});
    REQUIRE(engine->PlayCard("player0", draw_twos[0]));

    CHECK_FALSE(engine->IsMatchOver());
    CHECK(engine->GetPlacements() == std::vector<std::string>{"player0"});
    CHECK(HandSize(*engine, "player1") == 5);
    CHECK(engine->GetCurrentPlayerUsername() == "player2");
}

TEST_CASE("race: penalties and skips step over an earlier finisher") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 3);
    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    const std::vector<ecs::Entity> draw_twos =
        CardsByKind(*engine, "vanilla:red_draw2");

    FinishWithWild(*engine, "player0", wilds[0]);
    DrawAndPass(*engine, "player1");
    DrawAndPass(*engine, "player2");
    REQUIRE(engine->GetCurrentPlayerUsername() == "player3");

    SetActiveType(*engine, "red");
    ForceHandSize(*engine, "player1", 3);
    ForceHand(*engine, "player3", {draw_twos[0], wilds[1]});
    REQUIRE(engine->PlayCard("player3", draw_twos[0]));

    CHECK(HandSize(*engine, "player0") == 0);
    CHECK(HandSize(*engine, "player1") == 5);
    CHECK(engine->GetCurrentPlayerUsername() == "player2");
}

TEST_CASE("race: reverse with two live seats left acts as a skip") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 3, 2);
    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    const std::vector<ecs::Entity> reverses =
        CardsByKind(*engine, "vanilla:red_reverse");

    FinishWithWild(*engine, "player0", wilds[0]);
    REQUIRE(engine->GetCurrentPlayerUsername() == "player1");

    SetActiveType(*engine, "red");
    ForceHand(*engine, "player1", {reverses[0], wilds[1]});
    REQUIRE(engine->PlayCard("player1", reverses[0]));
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("race: bot takeover after start does not move the target") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 2);
    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    const ecs::Entity wild0 = wilds[0];
    const ecs::Entity wild1 = wilds[1];

    match::server::MatchSession session(std::move(engine), {}, {});
    MatchInstance& live = session.Engine();
    REQUIRE(session.HandSeatToBot("player3", "bot_three"));
    REQUIRE(session.HandSeatToBot("player2", "bot_two"));
    CHECK(live.GetRaceTarget() == 2u);

    FinishWithWild(live, "player0", wild0);
    CHECK_FALSE(live.IsMatchOver());
    ForceHandSize(live, "bot_two", 4);
    ForceHandSize(live, "bot_three", 1);
    FinishWithWild(live, "player1", wild1);

    CHECK(live.IsMatchOver());
    CHECK(live.GetPlacements()
          == std::vector<std::string>{"player0", "player1", "bot_three",
                                      "bot_two"});
}

TEST_CASE("race: a turn_end veto cannot keep a finisher on the turn") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 2);
    engine->Assembly().bus.Subscribe(
        "veto", 0, {"turn_end", ecs::HookPhase::kBefore},
        [](ecs::HookPayload& payload) { payload.veto = true; });

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    FinishWithWild(*engine, "player0", wilds[0]);

    CHECK(engine->GetPlacements() == std::vector<std::string>{"player0"});
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("race: placements that reach the target end the race on turn end") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 2);

    ecs::Placements* placements = engine->Store().Get<ecs::Placements>(
        engine->Registries().match);
    REQUIRE(placements != nullptr);
    placements->order = {*engine->FindPlayer("player1"),
                         *engine->FindPlayer("player2")};

    DrawAndPass(*engine, "player0");

    CHECK(engine->IsMatchOver());
    CHECK(engine->GetWinner() == "player1");
    CHECK(engine->GetPlacements()
          == std::vector<std::string>{"player1", "player2", "player3",
                                      "player0"});
}

TEST_CASE("race: a round is one cycle of the seats still at the table") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeRace(content, 4, 3);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    FinishWithWild(*engine, "player0", wilds[0]);
    engine->Tick();
    CHECK(engine->ExportState()["round"] == 0u);

    DrawAndPass(*engine, "player1");
    DrawAndPass(*engine, "player2");
    CHECK(engine->GetCurrentPlayerUsername() == "player3");
    engine->Tick();
    CHECK(engine->ExportState()["round"] == 1u);
}

}  // TEST_SUITE
