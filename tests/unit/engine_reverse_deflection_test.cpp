#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/status.hpp>
#include <match/timers.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_reverse_deflection_test.cpp
 * @brief Reverse deflection tests for the engine MatchInstance.
 *
 * Tests the reverse_deflection mod:
 * 1. An answering player with draw debt can play a matching-color Reverse.
 * 2. The Reverse card deflects the accumulated debt back to the attacker.
 * 3. The match direction is NOT permanently reversed (Method 2 ping-pong).
 * 4. The window re-opens for the attacker.
 * 5. Playing a mismatching-color Reverse is rejected.
 * 6. Ping-pong: Attacker can play another matching Reverse to deflect it back.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;
using nlohmann::json;

namespace {

struct FakeClock {
    int64_t now = 0;

    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

match::WindowConfig FixedWindow(int64_t ms) {
    match::WindowConfig config;
    config.window_ms = ms;
    config.mode = match::WindowMode::kFixed;
    return config;
}

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

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
            if (deck.deck_id == "uni:classic") out.classic = deck;
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

std::unique_ptr<MatchInstance> Assemble(
    Content& content, const std::vector<std::string>& mods, int players,
    int cards, uint64_t seed, match::WindowConfig config,
    match::NowMs clock) {
    DeckDef deck = content.classic;
    if (!mods.empty()) deck.mods = mods;
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, deck, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly), config,
                                           std::move(clock));
}

std::unique_ptr<MatchInstance> MakeEngine(
    Content& content, int players, int cards, uint64_t seed,
    match::WindowConfig config, match::NowMs clock) {
    return Assemble(content, {"uni", "draw_stacking", "reverse_deflection"}, players, cards, seed,
                    config, std::move(clock));
}

std::size_t HandSize(const MatchInstance& engine, ecs::Entity player) {
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    return hand == nullptr ? 0 : hand->cards.size();
}

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

std::optional<ecs::Entity> FindCard(MatchInstance& engine,
                                    const std::string& color,
                                    const std::string& label) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == color && face->label == label) {
            return card;
        }
    }
    return std::nullopt;
}

std::vector<ecs::Entity> FindAllCards(MatchInstance& engine,
                                      const std::string& color,
                                      const std::string& label) {
    std::vector<ecs::Entity> out;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == color && face->label == label) {
            out.push_back(card);
        }
    }
    return out;
}

std::optional<ecs::Entity> FindCardByKind(MatchInstance& engine,
                                          const std::string& kind_id) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* id = engine.Store().Get<ecs::CardIdentity>(card);
        if (id != nullptr && id->kind_id == kind_id) {
            return card;
        }
    }
    return std::nullopt;
}

int64_t DebtOf(MatchInstance& engine, ecs::Entity player) {
    const ecs::Status* debt = match::status::Find(
        engine.Store(), player, ops::kDrawDebtStatusId);
    return debt == nullptr ? 0 : debt->magnitude;
}

}  // namespace

TEST_CASE("engine reverse_deflection: matching reverse deflects debt back to attacker") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");

    const std::optional<ecs::Entity> blue_draw2 = FindCard(*engine, "blue", "+2");
    const std::optional<ecs::Entity> blue_rev = FindCard(*engine, "blue", "reverse");
    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(*engine, "blue", "6");
    const std::optional<ecs::Entity> filler2 = FindCard(*engine, "blue", "7");

    REQUIRE(blue_draw2.has_value());
    REQUIRE(blue_rev.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());
    REQUIRE(filler2.has_value());

    ForceHand(*engine, player0, {*blue_draw2, *filler0});
    ForceHand(*engine, player1, {*blue_rev, *filler1});
    ForceHand(*engine, player2, {*filler2});

    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type = "blue";
    const std::size_t p0_initial_hand = HandSize(*engine, player0);
    const std::size_t p1_initial_hand = HandSize(*engine, player1);

    // Player 0 plays blue +2
    REQUIRE(engine->PlayCard("player0", *blue_draw2));
    REQUIRE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player0) == 0);
    CHECK(DebtOf(*engine, player1) == 2);

    // Player 1 deflects with blue reverse!
    REQUIRE(engine->PlayCard("player1", *blue_rev));

    // Debt transferred back to player0!
    CHECK(DebtOf(*engine, player1) == 0);
    CHECK(DebtOf(*engine, player0) == 2);
    CHECK(HandSize(*engine, player1) == p1_initial_hand - 1);

    // Window must be open for player0
    REQUIRE(engine->WindowOpen());
    const json window = engine->ExportWindow();
    REQUIRE(window["responders"].is_array());
    REQUIRE(window["responders"].size() == 1);
    CHECK(window["responders"][0] == "player0");

    // Direction must NOT be permanently reversed (Method 2: direction remains Forward)
    CHECK(engine->Store().Get<ecs::MatchMeta>(engine->Registries().match)->direction == ecs::Direction::kForward);

    // Player 0 passes/fails to respond -> draws 2 cards, skipped
    REQUIRE(engine->PassWindow("player0"));
    CHECK_FALSE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player0) == 0);
    // Player 0 had 1 card left, draws 2 from debt -> 3 cards
    CHECK(HandSize(*engine, player0) == p0_initial_hand - 1 + 2);
}

TEST_CASE("engine reverse_deflection: mismatching color reverse is rejected") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");

    const std::optional<ecs::Entity> blue_draw2 = FindCard(*engine, "blue", "+2");
    const std::optional<ecs::Entity> red_rev = FindCard(*engine, "red", "reverse");
    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(*engine, "blue", "6");

    REQUIRE(blue_draw2.has_value());
    REQUIRE(red_rev.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());

    ForceHand(*engine, player0, {*blue_draw2, *filler0});
    ForceHand(*engine, player1, {*red_rev, *filler1});

    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type = "blue";

    // Player 0 plays blue +2
    REQUIRE(engine->PlayCard("player0", *blue_draw2));
    REQUIRE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player1) == 2);

    // Player 1 tries to play red reverse on blue top -> rejected!
    CHECK_FALSE(engine->PlayCard("player1", *red_rev));
    CHECK(DebtOf(*engine, player1) == 2);
    CHECK(engine->WindowOpen());
}

TEST_CASE("engine reverse_deflection: ping-pong deflection between two players") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");

    const std::optional<ecs::Entity> blue_draw2 = FindCard(*engine, "blue", "+2");
    const std::vector<ecs::Entity> blue_revs = FindAllCards(*engine, "blue", "reverse");
    REQUIRE(blue_revs.size() >= 2);
    const ecs::Entity blue_rev_p1 = blue_revs[0];
    const ecs::Entity blue_rev_p0 = blue_revs[1];

    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(*engine, "blue", "6");
    const std::optional<ecs::Entity> filler2 = FindCard(*engine, "blue", "7");

    REQUIRE(blue_draw2.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());
    REQUIRE(filler2.has_value());

    ForceHand(*engine, player0, {*blue_draw2, blue_rev_p0, *filler0});
    ForceHand(*engine, player1, {blue_rev_p1, *filler1});
    ForceHand(*engine, player2, {*filler2});

    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type = "blue";
    const std::size_t p1_initial_hand = HandSize(*engine, player1);

    // P0 plays blue +2 -> debt 2 to P1
    REQUIRE(engine->PlayCard("player0", *blue_draw2));
    CHECK(DebtOf(*engine, player1) == 2);

    // P1 deflects with blue reverse -> debt 2 back to P0
    REQUIRE(engine->PlayCard("player1", blue_rev_p1));
    CHECK(DebtOf(*engine, player1) == 0);
    CHECK(DebtOf(*engine, player0) == 2);
    REQUIRE(engine->WindowOpen());

    // P0 ping-pongs back with blue reverse -> debt 2 back to P1!
    REQUIRE(engine->PlayCard("player0", blue_rev_p0));
    CHECK(DebtOf(*engine, player0) == 0);
    CHECK(DebtOf(*engine, player1) == 2);
    REQUIRE(engine->WindowOpen());

    const json window = engine->ExportWindow();
    REQUIRE(window["responders"].is_array());
    CHECK(window["responders"][0] == "player1");

    // P1 passes -> P1 draws 2 cards
    REQUIRE(engine->PassWindow("player1"));
    CHECK_FALSE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player1) == 0);
    // P1 had 2 cards, played 1 reverse (-1), drew 2 (+2) -> 3 cards
    CHECK(HandSize(*engine, player1) == p1_initial_hand - 1 + 2);
}

TEST_CASE("engine reverse_deflection: stacking +2 then deflecting accumulated debt") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");

    const std::optional<ecs::Entity> blue_draw2 = FindCard(*engine, "blue", "+2");
    const std::optional<ecs::Entity> red_draw2 = FindCard(*engine, "red", "+2");
    const std::optional<ecs::Entity> red_rev = FindCard(*engine, "red", "reverse");
    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(*engine, "blue", "6");
    const std::optional<ecs::Entity> filler2 = FindCard(*engine, "blue", "7");

    REQUIRE(blue_draw2.has_value());
    REQUIRE(red_draw2.has_value());
    REQUIRE(red_rev.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());
    REQUIRE(filler2.has_value());

    ForceHand(*engine, player0, {*blue_draw2, *filler0});
    ForceHand(*engine, player1, {*red_draw2, *filler1});
    ForceHand(*engine, player2, {*red_rev, *filler2});

    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type = "blue";
    const std::size_t p1_initial_hand = HandSize(*engine, player1);

    // P0 plays blue +2 -> debt 2 to P1
    REQUIRE(engine->PlayCard("player0", *blue_draw2));
    CHECK(DebtOf(*engine, player1) == 2);

    // P1 stacks red +2 -> debt accumulates to 4 on P2!
    REQUIRE(engine->PlayCard("player1", *red_draw2));
    CHECK(DebtOf(*engine, player1) == 0);
    CHECK(DebtOf(*engine, player2) == 4);
    REQUIRE(engine->WindowOpen());

    // P2 deflects with red reverse (matches red active) -> sends 4 debt back to P1!
    REQUIRE(engine->PlayCard("player2", *red_rev));
    CHECK(DebtOf(*engine, player2) == 0);
    CHECK(DebtOf(*engine, player1) == 4);
    REQUIRE(engine->WindowOpen());

    const json window = engine->ExportWindow();
    REQUIRE(window["responders"].is_array());
    CHECK(window["responders"][0] == "player1");

    // P1 passes -> P1 draws 4 cards
    REQUIRE(engine->PassWindow("player1"));
    CHECK_FALSE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player1) == 0);
    CHECK(HandSize(*engine, player1) == p1_initial_hand - 1 + 4);
}

TEST_CASE("engine reverse_deflection: wild draw 4 deflected with chosen color reverse") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");

    const std::optional<ecs::Entity> wild_draw4 = FindCardByKind(*engine, "uni:wild_draw4");
    const std::optional<ecs::Entity> green_rev = FindCard(*engine, "green", "reverse");
    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(*engine, "blue", "6");
    const std::optional<ecs::Entity> filler2 = FindCard(*engine, "blue", "7");

    REQUIRE(wild_draw4.has_value());
    REQUIRE(green_rev.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());
    REQUIRE(filler2.has_value());

    ForceHand(*engine, player0, {*wild_draw4, *filler0});
    ForceHand(*engine, player1, {*green_rev, *filler1});
    ForceHand(*engine, player2, {*filler2});

    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type = "blue";
    const std::size_t p0_initial_hand = HandSize(*engine, player0);

    // P0 plays wild +4
    REQUIRE(engine->PlayCard("player0", *wild_draw4));
    // P0 chooses green for the wild prompt
    REQUIRE(engine->SubmitInput("player0", "green"));

    // Debt 4 on P1, active color is green
    REQUIRE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player1) == 4);

    // P1 deflects with green reverse back to P0!
    REQUIRE(engine->PlayCard("player1", *green_rev));
    CHECK(DebtOf(*engine, player1) == 0);
    CHECK(DebtOf(*engine, player0) == 4);
    REQUIRE(engine->WindowOpen());

    // P0 passes -> P0 draws 4 cards
    REQUIRE(engine->PassWindow("player0"));
    CHECK_FALSE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player0) == 0);
    CHECK(HandSize(*engine, player0) == p0_initial_hand - 1 + 4);
}
