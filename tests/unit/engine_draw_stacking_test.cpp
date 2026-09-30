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
 * draw-penalty play opens the window and records its N as `vanilla:draw_debt`
 * on the seat after the player, a stackable response carries the debt on to
 * the seat after the responder and re-opens the window on one budget ledger,
 * the default route makes the victim draw the accumulated debt with
 * `n_from_debt`, clears it and skips them, a `wild_draw4` prompt and a
 * draw-stacking window in the same dispatch are sequenced (the window is
 * deferred, never dropped), and a second window over the same play records
 * the debt only once.
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

constexpr int64_t kStackingWindowMs = 7000;
constexpr int64_t kJumpInHoldMs = 800;

/** @brief Three seats set up for a jump_in + draw_stacking +2 play. */
struct MergedTable {
    std::unique_ptr<MatchInstance> engine;
    ecs::Entity player0{};
    ecs::Entity player1{};
    ecs::Entity player2{};
    ecs::Entity draw2{};       /**< player0's red +2 (the opening play). */
    ecs::Entity identical2{};  /**< player2's second red +2 (jump-in). */
    ecs::Entity stack2{};      /**< player1's green +2 (stack response). */
    ecs::Entity filler0{};
    ecs::Entity filler1{};
    ecs::Entity filler2{};
    std::size_t victim_hand = 0;
};

/** @brief A card with `color` + `label` other than `excluded`. */
std::optional<ecs::Entity> FindOtherCard(MatchInstance& engine,
                                         const std::string& color,
                                         const std::string& label,
                                         ecs::Entity excluded) {
    for (ecs::Entity card : engine.Registries().cards) {
        if (card == excluded) continue;
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == color && face->label == label) {
            return card;
        }
    }
    return std::nullopt;
}

MergedTable SetUpMergedTable(Content& content,
                             const std::vector<std::string>& mods,
                             FakeClock& clock) {
    MergedTable table;
    table.engine = Assemble(content, mods, 3, 7, 42,
                            FixedWindow(kStackingWindowMs), clock.Fn());
    MatchInstance& engine = *table.engine;
    table.player0 = *engine.FindPlayer("player0");
    table.player1 = *engine.FindPlayer("player1");
    table.player2 = *engine.FindPlayer("player2");

    const std::optional<ecs::Entity> draw2 = FindCard(engine, "red", "+2");
    REQUIRE(draw2.has_value());
    const std::optional<ecs::Entity> identical2 =
        FindOtherCard(engine, "red", "+2", *draw2);
    const std::optional<ecs::Entity> stack2 = FindCard(engine, "green", "+2");
    const std::optional<ecs::Entity> filler0 = FindCard(engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(engine, "blue", "6");
    const std::optional<ecs::Entity> filler2 = FindCard(engine, "blue", "7");
    REQUIRE(identical2.has_value());
    REQUIRE(stack2.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());
    REQUIRE(filler2.has_value());
    table.draw2 = *draw2;
    table.identical2 = *identical2;
    table.stack2 = *stack2;
    table.filler0 = *filler0;
    table.filler1 = *filler1;
    table.filler2 = *filler2;

    ForceHand(engine, table.player0, {table.draw2, table.filler0});
    ForceHand(engine, table.player1, {table.stack2, table.filler1});
    ForceHand(engine, table.player2, {table.identical2, table.filler2});
    engine.Store().Get<ecs::ActiveTypeReq>(engine.Registries().match)->type =
        "red";
    table.victim_hand = HandSize(engine, table.player1);
    return table;
}

/** @brief Number of `jump_in:no_jump` signals (jump_in's default route). */
std::size_t CountNoJumpSignals(const MatchInstance& engine) {
    std::size_t count = 0;
    for (const json& event : engine.Events()) {
        if (!event.is_object() || event.value("type", "") != "signal") {
            continue;
        }
        if (event["payload"].value("name", "") == "jump_in:no_jump") ++count;
    }
    return count;
}

/** @brief Mod load orders of the merged pair; outcomes must not differ. */
const std::vector<std::vector<std::string>> kMergedLoadOrders = {
    {"vanilla", "jump_in", "draw_stacking"},
    {"vanilla", "draw_stacking", "jump_in"}};

/** @brief Three seats after player0 played a wild +4 into a merged group. */
struct Plus4Table {
    std::unique_ptr<MatchInstance> engine;
    ecs::Entity player0{};
    ecs::Entity player1{};
    ecs::Entity player2{};
    std::size_t victim_hand = 0;
    std::size_t bystander_hand = 0;
    ecs::Entity wild4{};
};

Plus4Table OpenPlus4Group(Content& content,
                          const std::vector<std::string>& mods,
                          FakeClock& clock) {
    Plus4Table table;
    table.engine = Assemble(content, mods, 3, 7, 42,
                            FixedWindow(kStackingWindowMs), clock.Fn());
    MatchInstance& engine = *table.engine;
    table.player0 = *engine.FindPlayer("player0");
    table.player1 = *engine.FindPlayer("player1");
    table.player2 = *engine.FindPlayer("player2");

    const std::optional<ecs::Entity> wild4 =
        FindCard(engine, "white", "jolly_draw4");
    const std::optional<ecs::Entity> filler0 = FindCard(engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(engine, "blue", "6");
    const std::optional<ecs::Entity> filler2 = FindCard(engine, "blue", "7");
    REQUIRE(wild4.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());
    REQUIRE(filler2.has_value());
    table.wild4 = *wild4;
    ForceHand(engine, table.player0, {*wild4, *filler0});
    ForceHand(engine, table.player1, {*filler1});
    ForceHand(engine, table.player2, {*filler2});
    table.victim_hand = HandSize(engine, table.player1);
    table.bystander_hand = HandSize(engine, table.player2);

    REQUIRE(engine.PlayCard("player0", *wild4));
    REQUIRE(engine.SubmitInput("player0", "red"));
    REQUIRE(engine.WindowOpen());
    REQUIRE(DebtOf(engine, table.player1) == 4);
    return table;
}

}  // namespace

TEST_CASE("engine draw_stacking: +N play opens the window and records debt") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    const std::optional<ecs::Entity> filler = FindCard(*engine, "blue", "5");
    REQUIRE(draw2.has_value());
    REQUIRE(filler.has_value());
    ForceHand(*engine, player0, {*draw2, *filler});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";
    const std::size_t victim_hand = HandSize(*engine, player1);

    REQUIRE(engine->PlayCard("player0", *draw2));
    CHECK(engine->WindowOpen());
    CHECK(engine->PendingWindow().has_value());
    CHECK(FindEvent(*engine, "window_open") != nullptr);

    // INFO: The +2 play records its N as debt on the victim (the
    //       seat after the player), who draws nothing until the chain ends.
    CHECK(DebtOf(*engine, player0) == 0);
    CHECK(DebtOf(*engine, player1) == 2);
    CHECK(HandSize(*engine, player1) == victim_hand);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");

    const json window = engine->ExportWindow();
    REQUIRE(window["responders"].is_array());
    // INFO: only the victim may stack; the other seats are not responders.
    REQUIRE(window["responders"].size() == 1);
    CHECK(window["responders"][0] == "player1");
    CHECK(window["default_route"] == "n2");
    // INFO: a window that declares no kind stays generic on the env duration.
    CHECK(window["kind"] == "generic");
    const json* opened = FindEvent(*engine, "window_open");
    REQUIRE(opened != nullptr);
    CHECK((*opened)["payload"]["kind"] == "generic");
    CHECK((*opened)["payload"]["duration_ms"] == 1000);
}

TEST_CASE("engine draw_stacking: unanswered +N makes the victim draw and skip") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    const std::optional<ecs::Entity> filler = FindCard(*engine, "blue", "5");
    REQUIRE(draw2.has_value());
    REQUIRE(filler.has_value());
    ForceHand(*engine, player0, {*draw2, *filler});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";
    const std::size_t victim_hand = HandSize(*engine, player1);

    REQUIRE(engine->PlayCard("player0", *draw2));
    CHECK_FALSE(engine->PassWindow("player2"));
    REQUIRE(engine->PassWindow("player1"));

    CHECK_FALSE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player1) == 0);
    CHECK(HandSize(*engine, player1) == victim_hand + 2);
    CHECK(engine->GetCurrentPlayerUsername() == "player2");
}

TEST_CASE("engine draw_stacking: stack response stacks and re-opens") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    const std::optional<ecs::Entity> stack2 = FindCard(*engine, "green", "+2");
    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(*engine, "blue", "6");
    REQUIRE(draw2.has_value());
    REQUIRE(stack2.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());
    ForceHand(*engine, player0, {*draw2, *filler0});
    ForceHand(*engine, player1, {*stack2, *filler1});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";

    REQUIRE(engine->PlayCard("player0", *draw2));
    REQUIRE(engine->WindowOpen());
    CHECK(DebtOf(*engine, player1) == 2);
    const uint64_t steps_after_open = engine->Assembly().budget.chain_steps;
    CHECK(CountEvents(*engine, "window_open") == 1);

    // INFO: the victim's out-of-turn stack is rescued by the mod's allow and,
    //       as the only responder, closes the window at once.
    REQUIRE(engine->RespondWindow("player1", *stack2));

    // INFO: route n3 hands player1 the turn, the whole debt moves on to
    //       player2 -> engine re-opens.
    CHECK(engine->WindowOpen());
    CHECK(DebtOf(*engine, player1) == 0);
    CHECK(DebtOf(*engine, player2) == 4);
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    CHECK(CountEvents(*engine, "window_open") == 2);
    CHECK(CountEvents(*engine, "window_close") == 1);
    const uint64_t steps_after_reopen = engine->Assembly().budget.chain_steps;
    // INFO: the chain shares one budget ledger (no reset across re-open).
    CHECK(steps_after_reopen > steps_after_open);

    // INFO: the re-opened window's only responder is the new victim.
    const std::size_t victim_hand = HandSize(*engine, player2);
    CHECK_FALSE(engine->PassWindow("player0"));
    REQUIRE(engine->PassWindow("player2"));

    CHECK_FALSE(engine->WindowOpen());
    CHECK(CountEvents(*engine, "window_close") == 2);
    // INFO: default route draws the accumulated debt on the victim, clears the
    //       status and skips them.
    CHECK(DebtOf(*engine, player2) == 0);
    CHECK(HandSize(*engine, player2) == victim_hand + 4);
    CHECK(HandSize(*engine, player1) == 1);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
}

TEST_CASE("engine draw_stacking: wild_draw4 prompt then window is sequenced") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> wild4 =
        FindCard(*engine, "white", "jolly_draw4");
    const std::optional<ecs::Entity> filler = FindCard(*engine, "blue", "5");
    REQUIRE(wild4.has_value());
    REQUIRE(filler.has_value());
    ForceHand(*engine, player0, {*wild4, *filler});
    const std::size_t victim_hand = HandSize(*engine, player1);

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
    // INFO: the deferred window records the +4 play's N as the initial debt
    //       on the victim; the mutated card no longer draws it immediately.
    CHECK(DebtOf(*engine, player1) == 4);
    CHECK(HandSize(*engine, player1) == victim_hand);
    CHECK(engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)
              ->type
          == "red");
}

TEST_CASE("engine draw_stacking: a second window over the play records once") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    MergedTable table = SetUpMergedTable(
        content, {"vanilla", "jump_in", "draw_stacking"}, clock);
    MatchInstance& engine = *table.engine;

    // INFO: both windows open over the same play as one group; the stacking
    //       member records the +2 as debt exactly once, at open.
    REQUIRE(engine.PlayCard("player0", table.draw2));
    REQUIRE(engine.WindowOpen());
    CHECK(DebtOf(engine, table.player1) == 2);
    clock.now = kStackingWindowMs;
    engine.Tick();

    CHECK_FALSE(engine.WindowOpen());
    CHECK(DebtOf(engine, table.player1) == 0);
    CHECK(HandSize(engine, table.player1) == table.victim_hand + 2);
}

TEST_CASE("engine merge: jump_in and draw_stacking open one longest group") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    MergedTable table = SetUpMergedTable(
        content, {"vanilla", "jump_in", "draw_stacking"}, clock);
    MatchInstance& engine = *table.engine;

    REQUIRE(engine.PlayCard("player0", table.draw2));
    REQUIRE(engine.WindowOpen());

    // INFO: one open event for the group, on the longest member duration.
    CHECK(CountEvents(engine, "window_open") == 1);
    const json* opened = FindEvent(engine, "window_open");
    REQUIRE(opened != nullptr);
    CHECK((*opened)["payload"]["duration_ms"] == kStackingWindowMs);
    CHECK((*opened)["payload"]["kind"] == "jump_in");

    // INFO: responders are the union: jump_in's @others plus the victim.
    const json window = engine.ExportWindow();
    std::vector<std::string> responders;
    for (const json& name : window["responders"]) {
        responders.push_back(name.get<std::string>());
    }
    CHECK(responders == std::vector<std::string>{"player1", "player2"});
    CHECK(window["deadline_ms"] == kStackingWindowMs);

    // INFO: the gate does not end at the 800 ms jump_in duration.
    clock.now = 800;
    engine.Tick();
    CHECK(engine.WindowOpen());
    CHECK(CountEvents(engine, "window_open") == 1);
    CHECK(CountEvents(engine, "window_close") == 0);
}

TEST_CASE("engine merge: CanRespondWindow accepts any member's filter") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    MergedTable table = SetUpMergedTable(
        content, {"vanilla", "jump_in", "draw_stacking"}, clock);
    MatchInstance& engine = *table.engine;

    REQUIRE(engine.PlayCard("player0", table.draw2));
    REQUIRE(engine.WindowOpen());

    CHECK(engine.CanRespondWindow(table.player2, table.identical2));
    CHECK(engine.CanRespondWindow(table.player1, table.stack2));
    CHECK_FALSE(engine.CanRespondWindow(table.player1, table.filler1));
    CHECK_FALSE(engine.CanRespondWindow(table.player2, table.filler2));
    CHECK_FALSE(engine.CanRespondWindow(table.player0, table.filler0));
}

namespace {

/** @brief Seat state the jump-in / stack chains are compared on. */
struct ChainOutcome {
    std::vector<int64_t> debts;
    std::vector<std::size_t> hands;
    std::string current;
    std::size_t opens = 0;
    std::size_t closes = 0;
    bool window_open = false;

    bool operator==(const ChainOutcome& other) const = default;
};

ChainOutcome SnapshotChain(MatchInstance& engine,
                           const std::vector<ecs::Entity>& seats) {
    ChainOutcome outcome;
    for (ecs::Entity seat : seats) {
        outcome.debts.push_back(DebtOf(engine, seat));
        outcome.hands.push_back(HandSize(engine, seat));
    }
    outcome.current = engine.GetCurrentPlayerUsername();
    outcome.opens = CountEvents(engine, "window_open");
    outcome.closes = CountEvents(engine, "window_close");
    outcome.window_open = engine.WindowOpen();
    return outcome;
}

/** @brief A +4 group whose bystander (player2) also holds a wild +4. */
Plus4Table OpenPlus4JumpTable(Content& content,
                              const std::vector<std::string>& mods,
                              FakeClock& clock, ecs::Entity& jump4) {
    Plus4Table table = OpenPlus4Group(content, mods, clock);
    MatchInstance& engine = *table.engine;
    const std::optional<ecs::Entity> twin =
        FindOtherCard(engine, "white", "jolly_draw4", table.wild4);
    const std::optional<ecs::Entity> filler2 = FindCard(engine, "blue", "7");
    REQUIRE(twin.has_value());
    REQUIRE(filler2.has_value());
    jump4 = *twin;
    ForceHand(engine, table.player2, {jump4, *filler2});
    table.bystander_hand = HandSize(engine, table.player2);
    return table;
}

}  // namespace

TEST_CASE("engine jump_in debt: a jump-in on a +4 accumulates to 8") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        ecs::Entity jump4{};
        Plus4Table table = OpenPlus4JumpTable(content, mods, clock, jump4);
        MatchInstance& engine = *table.engine;

        REQUIRE(engine.CanRespondWindow(table.player2, jump4));
        REQUIRE(engine.RespondWindow("player2", jump4));
        clock.now = kJumpInHoldMs;
        engine.Tick();

        // INFO: the jump-in resolved as a stack response by the jumper: the
        //       debt moved on to the jumper's next opponent (player0), the
        //       turn is the jumper's and a new window is open over their card.
        CHECK(CountEvents(engine, "window_close") == 1);
        CHECK(CountEvents(engine, "window_open") == 2);
        CHECK(engine.WindowOpen());
        CHECK(DebtOf(engine, table.player0) == 8);
        CHECK(DebtOf(engine, table.player1) == 0);
        CHECK(DebtOf(engine, table.player2) == 0);
        CHECK(HandSize(engine, table.player1) == table.victim_hand);
        CHECK(HandSize(engine, table.player2) == table.bystander_hand - 1);
        CHECK(engine.GetCurrentPlayerUsername() == "player2");
        CHECK(CountNoJumpSignals(engine) == 0);
        const json window = engine.ExportWindow();
        CHECK(window["responders"] == json::array({"player0"}));

        // INFO: the new victim draws the whole 8 when the window lapses and
        //       the turn resumes after them.
        const std::size_t player0_hand = HandSize(engine, table.player0);
        clock.now = 2 * kStackingWindowMs;
        engine.Tick();
        CHECK_FALSE(engine.WindowOpen());
        CHECK(CountEvents(engine, "window_close") == 2);
        CHECK(DebtOf(engine, table.player0) == 0);
        CHECK(HandSize(engine, table.player0) == player0_hand + 8);
        CHECK(engine.GetCurrentPlayerUsername() == "player1");
    }
}

TEST_CASE("engine jump_in debt: a victim pass with a winner keeps the debt") {
    Content content;
    REQUIRE(LoadContent(content));
    std::vector<ChainOutcome> outcomes;
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        ecs::Entity jump4{};
        Plus4Table table = OpenPlus4JumpTable(content, mods, clock, jump4);
        MatchInstance& engine = *table.engine;
        const std::vector<ecs::Entity> seats = {table.player0, table.player1,
                                                table.player2};

        REQUIRE(engine.RespondWindow("player2", jump4));
        clock.now = kJumpInHoldMs;
        REQUIRE(engine.PassWindow("player1"));

        // INFO: the pass closes on the winner's route, not the draw default.
        CHECK(DebtOf(engine, table.player0) == 8);
        CHECK(DebtOf(engine, table.player1) == 0);
        CHECK(HandSize(engine, table.player1) == table.victim_hand);
        outcomes.push_back(SnapshotChain(engine, seats));
    }
    REQUIRE(outcomes.size() == 2);
    CHECK(outcomes[0] == outcomes[1]);
}

TEST_CASE("engine jump_in debt: +2 jump-in then a victim stack is 2/4/6") {
    Content content;
    REQUIRE(LoadContent(content));
    std::vector<ChainOutcome> outcomes;
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        MergedTable table = SetUpMergedTable(content, mods, clock);
        MatchInstance& engine = *table.engine;
        const std::vector<ecs::Entity> seats = {table.player0, table.player1,
                                                table.player2};
        const std::optional<ecs::Entity> yellow2 =
            FindCard(engine, "yellow", "+2");
        REQUIRE(yellow2.has_value());

        REQUIRE(engine.PlayCard("player0", table.draw2));
        CHECK(DebtOf(engine, table.player1) == 2);

        REQUIRE(engine.RespondWindow("player2", table.identical2));
        clock.now = kStackingWindowMs;
        engine.Tick();
        CHECK(DebtOf(engine, table.player0) == 4);
        CHECK(DebtOf(engine, table.player1) == 0);
        CHECK(DebtOf(engine, table.player2) == 0);
        REQUIRE(engine.WindowOpen());

        ForceHand(engine, table.player0, {*yellow2, table.filler0});
        // INFO: player0 is the lone responder, so the response closes it.
        REQUIRE(engine.RespondWindow("player0", *yellow2));
        CHECK(DebtOf(engine, table.player0) == 0);
        CHECK(DebtOf(engine, table.player1) == 6);
        CHECK(DebtOf(engine, table.player2) == 0);
        CHECK(engine.WindowOpen());
        CHECK(engine.GetCurrentPlayerUsername() == "player0");

        outcomes.push_back(SnapshotChain(engine, seats));
    }
    REQUIRE(outcomes.size() == 2);
    CHECK(outcomes[0] == outcomes[1]);
}

TEST_CASE("engine jump_in debt: a victim's identical card is load-order safe") {
    Content content;
    REQUIRE(LoadContent(content));
    std::vector<ChainOutcome> outcomes;
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        MergedTable table = SetUpMergedTable(content, mods, clock);
        MatchInstance& engine = *table.engine;
        const std::vector<ecs::Entity> seats = {table.player0, table.player1,
                                                table.player2};

        // INFO: the victim holds an identical red +2, acceptable to both the
        //       jump-in and the stacking member; whichever loads first owns
        //       it, and the result must be the same stack response.
        ForceHand(engine, table.player1, {table.identical2, table.filler1});
        REQUIRE(engine.PlayCard("player0", table.draw2));
        REQUIRE(engine.RespondWindow("player1", table.identical2));
        clock.now = kStackingWindowMs;
        engine.Tick();

        CHECK(DebtOf(engine, table.player0) == 0);
        CHECK(DebtOf(engine, table.player1) == 0);
        CHECK(DebtOf(engine, table.player2) == 4);
        CHECK(engine.GetCurrentPlayerUsername() == "player1");
        CHECK(engine.WindowOpen());
        outcomes.push_back(SnapshotChain(engine, seats));
    }
    REQUIRE(outcomes.size() == 2);
    CHECK(outcomes[0] == outcomes[1]);
}

TEST_CASE("engine jump_in debt: a jump-in with no debt only redirects") {
    Content content;
    REQUIRE(LoadContent(content));
    std::vector<ChainOutcome> outcomes;
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        MergedTable table = SetUpMergedTable(content, mods, clock);
        MatchInstance& engine = *table.engine;
        const std::vector<ecs::Entity> seats = {table.player0, table.player1,
                                                table.player2};
        const std::optional<ecs::Entity> first5 = FindCard(engine, "red", "5");
        REQUIRE(first5.has_value());
        const std::optional<ecs::Entity> second5 =
            FindOtherCard(engine, "red", "5", *first5);
        REQUIRE(second5.has_value());
        ForceHand(engine, table.player0, {*first5, table.filler0});
        ForceHand(engine, table.player2, {*second5, table.filler2});

        REQUIRE(engine.PlayCard("player0", *first5));
        REQUIRE(engine.WindowOpen());
        REQUIRE(engine.RespondWindow("player2", *second5));
        clock.now = kStackingWindowMs;
        engine.Tick();

        CHECK_FALSE(engine.WindowOpen());
        CHECK(CountEvents(engine, "window_open") == 1);
        CHECK(CountEvents(engine, "window_close") == 1);
        for (ecs::Entity seat : seats) CHECK(DebtOf(engine, seat) == 0);
        CHECK(HandSize(engine, table.player1) == table.victim_hand);
        CHECK(CountNoJumpSignals(engine) == 0);
        CHECK(engine.GetCurrentPlayerUsername() == "player0");
        outcomes.push_back(SnapshotChain(engine, seats));
    }
    REQUIRE(outcomes.size() == 2);
    CHECK(outcomes[0] == outcomes[1]);
}

TEST_CASE("engine jump_in debt: a later play after a chain owns its debt") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        MergedTable table = SetUpMergedTable(content, mods, clock);
        MatchInstance& engine = *table.engine;

        REQUIRE(engine.PlayCard("player0", table.draw2));
        REQUIRE(engine.RespondWindow("player2", table.identical2));
        clock.now = kStackingWindowMs;
        engine.Tick();
        clock.now = 2 * kStackingWindowMs;
        engine.Tick();
        REQUIRE_FALSE(engine.WindowOpen());
        REQUIRE(engine.GetCurrentPlayerUsername() == "player1");
        for (ecs::Entity seat : {table.player0, table.player1, table.player2}) {
            REQUIRE(DebtOf(engine, seat) == 0);
        }

        const std::optional<ecs::Entity> next2 =
            FindCard(engine, "yellow", "+2");
        REQUIRE(next2.has_value());
        engine.Store().Get<ecs::ActiveTypeReq>(engine.Registries().match)
            ->type = "yellow";
        ForceHand(engine, table.player1, {*next2, table.filler1});
        REQUIRE(engine.PlayCard("player1", *next2));

        // INFO: only this play's own N is owed, and it opens its own group.
        CHECK(DebtOf(engine, table.player2) == 2);
        CHECK(DebtOf(engine, table.player0) == 0);
        CHECK(engine.WindowOpen());
    }
}

TEST_CASE("engine jump_in debt: a timeout with a winner resolves once") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        ecs::Entity jump4{};
        Plus4Table table = OpenPlus4JumpTable(content, mods, clock, jump4);
        MatchInstance& engine = *table.engine;
        const std::vector<ecs::Entity> seats = {table.player0, table.player1,
                                                table.player2};

        REQUIRE(engine.RespondWindow("player2", jump4));
        clock.now = kStackingWindowMs;
        engine.Tick();
        const ChainOutcome resolved = SnapshotChain(engine, seats);
        engine.Tick();
        engine.Tick();

        CHECK(SnapshotChain(engine, seats) == resolved);
        CHECK(resolved.closes == 1);
        CHECK(resolved.debts == std::vector<int64_t>{8, 0, 0});
    }
}

TEST_CASE("engine merge: a jump-in winner skips the stacking default draw") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    MergedTable table = SetUpMergedTable(
        content, {"vanilla", "jump_in", "draw_stacking"}, clock);
    MatchInstance& engine = *table.engine;

    REQUIRE(engine.PlayCard("player0", table.draw2));
    REQUIRE(engine.RespondWindow("player2", table.identical2));
    clock.now = kStackingWindowMs;
    engine.Tick();

    // INFO: nobody drew: the carried debt now sits on player0 and the
    //       victim's default draw did not run.
    CHECK(HandSize(engine, table.player1) == table.victim_hand);
    CHECK(CountNoJumpSignals(engine) == 0);
    CHECK(DebtOf(engine, table.player0) == 4);
}

TEST_CASE("engine merge: a member's responders scope its accepted cards") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    MergedTable table = SetUpMergedTable(
        content, {"vanilla", "jump_in", "draw_stacking"}, clock);
    MatchInstance& engine = *table.engine;
    const std::optional<ecs::Entity> stack_card =
        FindCard(engine, "yellow", "+2");
    REQUIRE(stack_card.has_value());
    ForceHand(engine, table.player2, {*stack_card, table.identical2});

    REQUIRE(engine.PlayCard("player0", table.draw2));
    REQUIRE(engine.WindowOpen());

    // INFO: player2 is a responder of jump_in only, so draw_stacking's stack
    //       filter must not admit a non-identical +2 from them.
    CHECK_FALSE(engine.CanRespondWindow(table.player2, *stack_card));
    CHECK_FALSE(engine.RespondWindow("player2", *stack_card));
    CHECK(engine.ExportWindow()["responses"].empty());
    CHECK(engine.CanRespondWindow(table.player2, table.identical2));
    CHECK(engine.RespondWindow("player2", table.identical2));
    // INFO: the victim, a stacking responder, may still stack.
    CHECK(engine.CanRespondWindow(table.player1, table.stack2));
}

TEST_CASE("engine merge: a wild +4 prompt then one merged group") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        Assemble(content, {"vanilla", "jump_in", "draw_stacking"}, 3, 7, 42,
                 FixedWindow(kStackingWindowMs), clock.Fn());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> wild4 =
        FindCard(*engine, "white", "jolly_draw4");
    const std::optional<ecs::Entity> filler = FindCard(*engine, "blue", "5");
    REQUIRE(wild4.has_value());
    REQUIRE(filler.has_value());
    ForceHand(*engine, player0, {*wild4, *filler});

    REQUIRE(engine->PlayCard("player0", *wild4));
    REQUIRE(engine->PendingInput().has_value());
    CHECK_FALSE(engine->WindowOpen());
    REQUIRE(engine->SubmitInput("player0", "red"));

    // INFO: both windows deferred behind the prompt open as one group.
    REQUIRE(engine->WindowOpen());
    CHECK(CountEvents(*engine, "window_open") == 1);
    const json* opened = FindEvent(*engine, "window_open");
    REQUIRE(opened != nullptr);
    CHECK((*opened)["payload"]["duration_ms"] == kStackingWindowMs);
    CHECK((*opened)["payload"]["kind"] == "jump_in");
    CHECK(DebtOf(*engine, player1) == 4);
}

TEST_CASE("engine merge: a stack response still stacks and re-opens") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    MergedTable table = SetUpMergedTable(
        content, {"vanilla", "jump_in", "draw_stacking"}, clock);
    MatchInstance& engine = *table.engine;

    REQUIRE(engine.PlayCard("player0", table.draw2));
    REQUIRE(engine.WindowOpen());
    REQUIRE(engine.RespondWindow("player1", table.stack2));

    clock.now = kJumpInHoldMs;
    engine.Tick();

    // INFO: draw_stacking's on_response owns the reply: the debt moves on to
    //       player2, player1 holds the turn and the stacking window re-opens;
    //       jump_in's default did not run.
    CHECK(CountEvents(engine, "window_close") == 1);
    CHECK(CountEvents(engine, "window_open") == 2);
    CHECK(engine.WindowOpen());
    CHECK(DebtOf(engine, table.player1) == 0);
    CHECK(DebtOf(engine, table.player2) == 4);
    CHECK(engine.GetCurrentPlayerUsername() == "player1");
    CHECK(CountNoJumpSignals(engine) == 0);
}

TEST_CASE("engine merge: no responder runs every default once in order") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    MergedTable table = SetUpMergedTable(
        content, {"vanilla", "jump_in", "draw_stacking"}, clock);
    MatchInstance& engine = *table.engine;

    REQUIRE(engine.PlayCard("player0", table.draw2));
    REQUIRE(engine.WindowOpen());
    clock.now = kStackingWindowMs;
    engine.Tick();

    CHECK_FALSE(engine.WindowOpen());
    CHECK(CountEvents(engine, "window_open") == 1);
    CHECK(CountEvents(engine, "window_close") == 1);
    CHECK(CountNoJumpSignals(engine) == 1);
    CHECK(DebtOf(engine, table.player1) == 0);
    CHECK(HandSize(engine, table.player1) == table.victim_hand + 2);
    CHECK(engine.GetCurrentPlayerUsername() == "player2");

    // INFO: member order is mod load order: jump_in's signal, then the draw.
    std::size_t signal_index = 0;
    std::size_t draw_index = 0;
    std::size_t index = 0;
    for (const json& event : engine.Events()) {
        ++index;
        if (!event.is_object()) continue;
        const std::string type = event.value("type", "");
        if (type == "signal" && signal_index == 0) signal_index = index;
        if (type == "status_removed" && draw_index == 0) draw_index = index;
    }
    REQUIRE(signal_index != 0);
    REQUIRE(draw_index != 0);
    CHECK(signal_index < draw_index);
}

TEST_CASE("engine merge: the group kind follows the first member") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    MergedTable table = SetUpMergedTable(
        content, {"vanilla", "draw_stacking", "jump_in"}, clock);
    MatchInstance& engine = *table.engine;

    REQUIRE(engine.PlayCard("player0", table.draw2));
    REQUIRE(engine.WindowOpen());
    CHECK(CountEvents(engine, "window_open") == 1);
    CHECK(engine.ExportWindow()["kind"] == "generic");

    // INFO: passability keys on the hold, not the kind: the victim passes
    //       once it has elapsed and the pass draws the debt and closes.
    clock.now = kJumpInHoldMs;
    CHECK_FALSE(engine.PassWindow("player2"));
    REQUIRE(engine.PassWindow("player1"));
    CHECK_FALSE(engine.WindowOpen());
    CHECK(CountEvents(engine, "window_close") == 1);
    CHECK(CountNoJumpSignals(engine) == 1);
    CHECK(HandSize(engine, table.player1) == table.victim_hand + 2);
}

TEST_CASE("engine hold: the victim passes at the hold boundary, not before") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        Plus4Table table = OpenPlus4Group(content, mods, clock);
        MatchInstance& engine = *table.engine;

        CHECK(engine.WindowHoldMs() == kJumpInHoldMs);
        clock.now = kJumpInHoldMs - 1;
        CHECK_FALSE(engine.WindowHoldElapsed());
        CHECK_FALSE(engine.PassWindow("player1"));
        CHECK(engine.WindowOpen());
        CHECK(engine.ExportWindow()["responses"].empty());
        CHECK(DebtOf(engine, table.player1) == 4);
        CHECK(HandSize(engine, table.player1) == table.victim_hand);
        CHECK(CountEvents(engine, "window_close") == 0);

        clock.now = kJumpInHoldMs;
        CHECK(engine.WindowHoldElapsed());
        REQUIRE(engine.PassWindow("player1"));
        CHECK_FALSE(engine.WindowOpen());
        CHECK(CountEvents(engine, "window_close") == 1);
        CHECK(DebtOf(engine, table.player1) == 0);
        CHECK(HandSize(engine, table.player1) == table.victim_hand + 4);
        CHECK(HandSize(engine, table.player2) == table.bystander_hand);
        CHECK(engine.GetCurrentPlayerUsername() == "player2");
    }
}

TEST_CASE("engine hold: a non-victim can never pass a debt group") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        Plus4Table table = OpenPlus4Group(content, mods, clock);
        MatchInstance& engine = *table.engine;

        for (int64_t now : {int64_t{0}, kJumpInHoldMs - 1, kJumpInHoldMs,
                            kStackingWindowMs - 1}) {
            clock.now = now;
            CHECK_FALSE(engine.PassWindow("player2"));
            CHECK_FALSE(engine.PassWindow("player0"));
        }
        CHECK(engine.WindowOpen());
        CHECK(engine.ExportWindow()["responses"].empty());
        CHECK(HandSize(engine, table.player2) == table.bystander_hand);
        CHECK(DebtOf(engine, table.player1) == 4);
    }
}

TEST_CASE("engine hold: timer expiry draws the same cards as a pass") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        Plus4Table table = OpenPlus4Group(content, mods, clock);
        MatchInstance& engine = *table.engine;

        clock.now = kStackingWindowMs;
        engine.Tick();

        CHECK_FALSE(engine.WindowOpen());
        CHECK(CountEvents(engine, "window_close") == 1);
        CHECK(DebtOf(engine, table.player1) == 0);
        CHECK(HandSize(engine, table.player1) == table.victim_hand + 4);
        CHECK(engine.GetCurrentPlayerUsername() == "player2");
    }
}

TEST_CASE("engine hold: a jump-in closes the group at the hold end") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        ecs::Entity jump4{};
        Plus4Table table = OpenPlus4JumpTable(content, mods, clock, jump4);
        MatchInstance& engine = *table.engine;

        clock.now = 200;
        REQUIRE(engine.RespondWindow("player2", jump4));
        clock.now = kJumpInHoldMs - 1;
        engine.Tick();
        CHECK(CountEvents(engine, "window_close") == 0);
        CHECK(DebtOf(engine, table.player1) == 4);

        clock.now = kJumpInHoldMs;
        engine.Tick();
        CHECK(CountEvents(engine, "window_close") == 1);
        CHECK(DebtOf(engine, table.player0) == 8);
        CHECK(DebtOf(engine, table.player1) == 0);
        CHECK(HandSize(engine, table.player1) == table.victim_hand);
        engine.Tick();
        CHECK(CountEvents(engine, "window_close") == 1);
    }
}

TEST_CASE("engine hold: a jump-in after the hold closes immediately") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        ecs::Entity jump4{};
        Plus4Table table = OpenPlus4JumpTable(content, mods, clock, jump4);
        MatchInstance& engine = *table.engine;

        clock.now = 1500;
        REQUIRE(engine.RespondWindow("player2", jump4));
        CHECK(CountEvents(engine, "window_close") == 1);
        CHECK(DebtOf(engine, table.player0) == 8);
        CHECK(engine.GetCurrentPlayerUsername() == "player2");
    }
}

TEST_CASE("engine hold: a victim stack closes at the hold end or at once") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        {
            FakeClock clock;
            MergedTable table = SetUpMergedTable(content, mods, clock);
            MatchInstance& engine = *table.engine;
            REQUIRE(engine.PlayCard("player0", table.draw2));
            clock.now = 200;
            REQUIRE(engine.RespondWindow("player1", table.stack2));
            clock.now = kJumpInHoldMs - 1;
            engine.Tick();
            CHECK(CountEvents(engine, "window_close") == 0);
            clock.now = kJumpInHoldMs;
            engine.Tick();
            CHECK(CountEvents(engine, "window_close") == 1);
            CHECK(DebtOf(engine, table.player2) == 4);
        }
        {
            FakeClock clock;
            MergedTable table = SetUpMergedTable(content, mods, clock);
            MatchInstance& engine = *table.engine;
            REQUIRE(engine.PlayCard("player0", table.draw2));
            clock.now = 1500;
            REQUIRE(engine.RespondWindow("player1", table.stack2));
            CHECK(CountEvents(engine, "window_close") == 1);
            CHECK(DebtOf(engine, table.player2) == 4);
        }
    }
}

TEST_CASE("engine hold: no winner still runs to the timeout and draws") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        Plus4Table table = OpenPlus4Group(content, mods, clock);
        MatchInstance& engine = *table.engine;

        clock.now = kJumpInHoldMs;
        engine.Tick();
        CHECK(engine.WindowOpen());
        clock.now = kStackingWindowMs - 1;
        engine.Tick();
        CHECK(engine.WindowOpen());
        clock.now = kStackingWindowMs;
        engine.Tick();
        CHECK_FALSE(engine.WindowOpen());
        CHECK(HandSize(engine, table.player1) == table.victim_hand + 4);
        CHECK(CountEvents(engine, "window_close") == 1);
    }
}

TEST_CASE("engine hold: a pass then a tick never draws twice") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        Plus4Table table = OpenPlus4Group(content, mods, clock);
        MatchInstance& engine = *table.engine;

        clock.now = kJumpInHoldMs;
        REQUIRE(engine.PassWindow("player1"));
        const std::size_t drawn_hand = HandSize(engine, table.player1);
        clock.now = kStackingWindowMs;
        engine.Tick();
        engine.Tick();

        CHECK_FALSE(engine.PassWindow("player1"));
        CHECK(HandSize(engine, table.player1) == drawn_hand);
        CHECK(drawn_hand == table.victim_hand + 4);
        CHECK(CountEvents(engine, "window_close") == 1);
        CHECK(CountEvents(engine, "window_open") == 1);
    }
}

TEST_CASE("engine hold: a play after a pass opens a jumpable group") {
    Content content;
    REQUIRE(LoadContent(content));
    for (const std::vector<std::string>& mods : kMergedLoadOrders) {
        CAPTURE(mods);
        FakeClock clock;
        MergedTable table = SetUpMergedTable(content, mods, clock);
        MatchInstance& engine = *table.engine;
        const std::optional<ecs::Entity> first5 = FindCard(engine, "red", "5");
        REQUIRE(first5.has_value());
        const std::optional<ecs::Entity> second5 =
            FindOtherCard(engine, "red", "5", *first5);
        REQUIRE(second5.has_value());

        REQUIRE(engine.PlayCard("player0", table.draw2));
        clock.now = kJumpInHoldMs;
        REQUIRE(engine.PassWindow("player1"));
        REQUIRE_FALSE(engine.WindowOpen());
        REQUIRE(engine.GetCurrentPlayerUsername() == "player2");

        ForceHand(engine, table.player2, {*first5, table.filler2});
        ForceHand(engine, table.player0, {*second5, table.filler0});
        REQUIRE(engine.PlayCard("player2", *first5));
        REQUIRE(engine.WindowOpen());
        CHECK(CountEvents(engine, "window_open") == 2);
        CHECK(engine.CanRespondWindow(table.player0, *second5));
        CHECK(engine.RespondWindow("player0", *second5));
    }
}

TEST_CASE("engine hold: a jump_in-only window has no pass at all") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        Assemble(content, {"vanilla", "jump_in"}, 3, 7, 42,
                 FixedWindow(kStackingWindowMs), clock.Fn());
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> top = [&]() {
        const ecs::PileContents* discard =
            engine->Store().Get<ecs::PileContents>(
                engine->Registries().discard_pile);
        return discard == nullptr || discard->cards.empty()
                   ? std::optional<ecs::Entity>()
                   : std::optional<ecs::Entity>(discard->cards.back());
    }();
    REQUIRE(top.has_value());
    const ecs::FaceSpec* top_face = engine->Store().Get<ecs::FaceSpec>(*top);
    REQUIRE(top_face != nullptr);
    const std::optional<ecs::Entity> twin =
        FindOtherCard(*engine, top_face->color, top_face->label, *top);
    REQUIRE(twin.has_value());
    ForceHand(*engine, player1, {*twin});
    REQUIRE(engine->PlayCard("player1", *twin));
    REQUIRE(engine->WindowOpen());

    for (int64_t now : {int64_t{0}, kJumpInHoldMs - 1}) {
        clock.now = now;
        for (const char* seat : {"player0", "player1", "player2"}) {
            CHECK_FALSE(engine->PassWindow(seat));
        }
    }
    CHECK(engine->WindowOpen());
    CHECK(engine->ExportWindow()["responses"].empty());

    clock.now = kJumpInHoldMs;
    for (const char* seat : {"player0", "player1", "player2"}) {
        CHECK_FALSE(engine->PassWindow(seat));
    }
    engine->Tick();
    CHECK_FALSE(engine->WindowOpen());
    CHECK(CountEvents(*engine, "window_close") == 1);
}

TEST_CASE("engine draw_stacking: CanRespondWindow mirrors RespondWindow") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    const std::optional<ecs::Entity> stack2 = FindCard(*engine, "green", "+2");
    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> filler1 = FindCard(*engine, "blue", "6");
    REQUIRE(draw2.has_value());
    REQUIRE(stack2.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(filler1.has_value());
    const std::optional<ecs::Entity> wild4 =
        FindCard(*engine, "white", "jolly_draw4");
    REQUIRE(wild4.has_value());
    ForceHand(*engine, player0, {*draw2, *filler0});
    ForceHand(*engine, player1, {*stack2, *filler1, *wild4});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";

    CHECK_FALSE(engine->CanRespondWindow(player1, *stack2));
    REQUIRE(engine->PlayCard("player0", *draw2));
    REQUIRE(engine->WindowOpen());

    // INFO: the out-of-turn responder may stack, but only with a +N; the
    //       player who opened the window is not a responder.
    CHECK(engine->CanRespondWindow(player1, *stack2));
    CHECK_FALSE(engine->CanRespondWindow(player1, *filler1));
    CHECK_FALSE(engine->CanRespondWindow(player0, *filler0));
    // INFO: legacy stacking only matches the same draw value (+4 on +2 no).
    CHECK_FALSE(engine->CanRespondWindow(player1, *wild4));

    REQUIRE(engine->RespondWindow("player1", *stack2));
    CHECK_FALSE(engine->CanRespondWindow(player1, *filler1));
}

TEST_CASE("engine draw_stacking: a +N is not playable on an unrelated card") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeStackingEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");
    const std::optional<ecs::Entity> opener = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "6");
    const std::optional<ecs::Entity> green2 = FindCard(*engine, "green", "+2");
    const std::optional<ecs::Entity> yellow2 =
        FindCard(*engine, "yellow", "+2");
    REQUIRE(opener.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(green2.has_value());
    REQUIRE(yellow2.has_value());
    ForceHand(*engine, player0, {*opener, *filler0});
    ForceHand(*engine, player1, {*green2});
    ForceHand(*engine, player2, {*yellow2});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "blue";

    REQUIRE(engine->PlayCard("player0", *opener));
    REQUIRE_FALSE(engine->WindowOpen());
    REQUIRE(engine->GetCurrentPlayerUsername() == "player1");

    // INFO: neither in turn (colour/value mismatch) nor out of turn (no
    //       stacking window) may a +2 land on a blue 5.
    CHECK_FALSE(engine->PlayCard("player1", *green2));
    CHECK_FALSE(engine->PlayCard("player2", *yellow2));
    CHECK(HandSize(*engine, player1) == 1);
    CHECK(HandSize(*engine, player2) == 1);
}
