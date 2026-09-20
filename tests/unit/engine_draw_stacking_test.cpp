#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/status.hpp>
#include <match/timers.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_draw_stacking_test.cpp
 * @brief Draw-stacking tests for the engine MatchInstance.
 *
 * Assembles real `mods/` content (`vanilla` + `draw_stacking`) and asserts the
 * engine-owned debt mechanics that the DAG content cannot express: a
 * draw-penalty play opens the window and records its N as `vanilla:draw_debt`,
 * a stackable response appends its own N and re-opens the window on one budget
 * ledger, the default route draws the accumulated debt with `n_from_debt` and
 * clears it, and a `wild_draw4` prompt and a draw-stacking window in the same
 * dispatch are sequenced (the window is deferred, never dropped).
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

/** @brief Deterministic wall clock for the window/turn timers. */
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

/** @brief `{"vanilla","draw_stacking"}` match (0.subject). */
std::unique_ptr<MatchInstance> MakeStackingEngine(
    Content& content, int players, int cards, uint64_t seed,
    match::WindowConfig config, match::NowMs clock) {
    return Assemble(content, {"vanilla", "draw_stacking"}, players, cards, seed,
                    config, std::move(clock));
}

std::size_t HandSize(const MatchInstance& engine, ecs::Entity player) {
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    return hand == nullptr ? 0 : hand->cards.size();
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

/** @brief First card whose face matches `color` + `label`, or nullopt. */
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

/** @brief Accumulated `vanilla:draw_debt` magnitude on `player` (0 = none). */
int64_t DebtOf(MatchInstance& engine, ecs::Entity player) {
    const ecs::Status* debt = match::status::Find(
        engine.Store(), player, ops::kDrawDebtStatusId);
    return debt == nullptr ? 0 : debt->magnitude;
}

const json* FindEvent(const MatchInstance& engine, const std::string& type) {
    for (const json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) {
            return &event;
        }
    }
    return nullptr;
}

std::size_t CountEvents(const MatchInstance& engine,
                        const std::string& type) {
    std::size_t count = 0;
    for (const json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("engine draw_stacking: +N play opens the window and records debt") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    REQUIRE(draw2.has_value());
    ForceHand(*engine, player0, {*draw2});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";

    REQUIRE(engine->PlayCard("player0", *draw2));
    CHECK(engine->WindowOpen());
    CHECK(engine->PendingWindow().has_value());
    CHECK(FindEvent(*engine, "window_open") != nullptr);

    // INFO: The +2 play records its N as debt on the target.
    CHECK(DebtOf(*engine, player0) == 2);

    const json window = engine->ExportWindow();
    REQUIRE(window["responders"].is_array());
    CHECK(window["responders"].size() == 2);
    CHECK(window["default_route"] == "n2");
}

TEST_CASE("engine draw_stacking: stack response stacks and re-opens") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    const std::optional<ecs::Entity> stack2 = FindCard(*engine, "green", "+2");
    REQUIRE(draw2.has_value());
    REQUIRE(stack2.has_value());
    ForceHand(*engine, player0, {*draw2});
    ForceHand(*engine, player1, {*stack2});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";

    REQUIRE(engine->PlayCard("player0", *draw2));
    REQUIRE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player0) == 2);
    const uint64_t steps_after_open = engine->Assembly().budget.chain_steps;
    CHECK(CountEvents(*engine, "window_open") == 1);

    // INFO: an out-of-turn stackable response is rescued by the mod's allow;
    //       the first accepted response wins when the window closes.
    REQUIRE(engine->RespondWindow("player1", *stack2));
    CHECK(engine->WindowOpen());  // INFO: player2 has not replied yet.
    REQUIRE(engine->PassWindow("player2"));

    // INFO: close collected player1's response -> route n3 -> engine re-opens.
    CHECK(engine->WindowOpen());
    CHECK(DebtOf(*engine, player0) == 4);
    CHECK(CountEvents(*engine, "window_open") == 2);
    CHECK(CountEvents(*engine, "window_close") == 1);
    const uint64_t steps_after_reopen = engine->Assembly().budget.chain_steps;
    // INFO: the chain shares one budget ledger (no reset across re-open).
    CHECK(steps_after_reopen > steps_after_open);

    // INFO: the re-opened window has responders @others of player1 = p0, p2.
    const std::size_t hand_before = HandSize(*engine, player0);
    REQUIRE(engine->PassWindow("player0"));
    CHECK(engine->WindowOpen());
    REQUIRE(engine->PassWindow("player2"));

    CHECK_FALSE(engine->WindowOpen());
    CHECK(CountEvents(*engine, "window_close") == 2);
    // INFO: default route draws the accumulated debt on @current_player and
    //       clears the status.
    CHECK(DebtOf(*engine, player0) == 0);
    CHECK(HandSize(*engine, player0) == hand_before + 4);
}

TEST_CASE("engine draw_stacking: wild_draw4 prompt then window is sequenced") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> wild4 =
        FindCard(*engine, "white", "jolly_draw4");
    REQUIRE(wild4.has_value());
    ForceHand(*engine, player0, {*wild4});

    // INFO: the +4's own on_play prompts for a colour before the draw-stacking
    //       rule resolves; the window must be deferred, not dropped.
    REQUIRE(engine->PlayCard("player0", *wild4));
    CHECK_FALSE(engine->WindowOpen());
    REQUIRE(engine->PendingInput().has_value());
    CHECK(FindEvent(*engine, "window_open") == nullptr);

    REQUIRE(engine->SubmitInput("player0", "red"));
    CHECK_FALSE(engine->PendingInput().has_value());
    CHECK(engine->WindowOpen());
    CHECK(FindEvent(*engine, "window_open") != nullptr);
    // INFO: the deferred window records the +4 play's N as the initial debt.
    CHECK(DebtOf(*engine, player0) == 4);
}
