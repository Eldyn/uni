#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/timers.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_jump_in_window_test.cpp
 * @brief Jump-in window characterisation tests for the engine MatchInstance.
 *
 * Assembles real `mods/` content (`vanilla` + `jump_in`) and pins the current
 * behaviour of an out-of-turn `PlayCard` made with no window open: the jump_in
 * `allow` rescues vanilla's out-of-turn deny, so the engine accepts it, while
 * nothing redirects the turn (the redirect lives only in the window's
 * `on_response`). This is the latent hole recorded earlier; it is
 * pinned, not fixed, in this task.
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

/** @brief `{"vanilla","jump_in"}` match (the out-of-turn play subject). */
std::unique_ptr<MatchInstance> MakeJumpInEngine(
    Content& content, int players, int cards, uint64_t seed,
    match::WindowConfig config, match::NowMs clock) {
    return Assemble(content, {"vanilla", "jump_in"}, players, cards, seed,
                    config, std::move(clock));
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

/** @brief The discard pile's top card entity, or nullopt. */
std::optional<ecs::Entity> TopDiscardEntity(MatchInstance& engine) {
    const ecs::PileContents* discard = engine.Store().Get<ecs::PileContents>(
        engine.Registries().discard_pile);
    if (discard == nullptr || discard->cards.empty()) return std::nullopt;
    return discard->cards.back();
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

/**
 * @brief A second copy of the discard top's face (same colour + label).
 *
 * The starter card is numeric and numbered cards ship two copies, so an
 * identical out-of-turn play is always craftable.
 */
std::optional<ecs::Entity> FindDuplicateOfTop(MatchInstance& engine) {
    const std::optional<ecs::Entity> top = TopDiscardEntity(engine);
    if (!top.has_value()) return std::nullopt;
    const ecs::FaceSpec* top_face = engine.Store().Get<ecs::FaceSpec>(*top);
    if (top_face == nullptr) return std::nullopt;
    for (ecs::Entity card : engine.Registries().cards) {
        if (card == *top) continue;
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == top_face->color
            && face->label == top_face->label) {
            return card;
        }
    }
    return std::nullopt;
}

/** @brief A card that matches neither the discard top nor the active type. */
std::optional<ecs::Entity> FindNonMatchingCard(MatchInstance& engine) {
    const std::optional<ecs::Entity> top = TopDiscardEntity(engine);
    if (!top.has_value()) return std::nullopt;
    const ecs::FaceSpec* top_face = engine.Store().Get<ecs::FaceSpec>(*top);
    if (top_face == nullptr) return std::nullopt;
    for (ecs::Entity card : engine.Registries().cards) {
        if (card == *top) continue;
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face == nullptr) continue;
        if (face->color == "white") continue;
        if (face->color == top_face->color) continue;
        if (face->label == top_face->label) continue;
        return card;
    }
    return std::nullopt;
}

std::size_t CountEvents(const MatchInstance& engine,
                        const std::string& type) {
    std::size_t count = 0;
    for (const json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) ++count;
    }
    return count;
}

const json* FindEvent(const MatchInstance& engine, const std::string& type) {
    for (const json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) {
            return &event;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("engine jump_in: out-of-turn identical PlayCard wins (no window)") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeJumpInEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");
    const std::optional<ecs::Entity> identical = FindDuplicateOfTop(*engine);
    REQUIRE(identical.has_value());
    ForceHand(*engine, player1, {*identical});

    // INFO: pins latent hole. The precondition is a play made with
    //       no window open, i.e. a direct `PlayCard` rather than a response.
    REQUIRE_FALSE(engine->WindowOpen());
    const std::size_t rejected_before = CountEvents(*engine, "play_rejected");

    const bool accepted = engine->PlayCard("player1", *identical);

    // INFO: pins latent hole. The jump_in `allow` rescues vanilla's
    //       out-of-turn deny, so the engine accepts the play today.
    CHECK(accepted);
    CHECK(CountEvents(*engine, "play_rejected") == rejected_before);
    // INFO: pins latent hole. Nothing redirects the turn to the
    //       out-of-turn player; the redirect lives in the window on_response.
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    CHECK(player0 != player1);
    // INFO: the accepted play's `after:play` opens the jump_in window.
    CHECK(engine->WindowOpen());
}

TEST_CASE("engine jump_in: non-identical out-of-turn PlayCard is refused") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeJumpInEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player1 = *engine->FindPlayer("player1");
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");
    const std::optional<ecs::Entity> mismatch = FindNonMatchingCard(*engine);
    REQUIRE(mismatch.has_value());
    ForceHand(*engine, player1, {*mismatch});

    REQUIRE_FALSE(engine->WindowOpen());
    const std::size_t rejected_before = CountEvents(*engine, "play_rejected");

    // INFO: pins latent hole. Only the identical-card allow rescues
    //       an out-of-turn play; every other out-of-turn play stays denied.
    CHECK_FALSE(engine->PlayCard("player1", *mismatch));
    CHECK(CountEvents(*engine, "play_rejected") == rejected_before + 1);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    CHECK_FALSE(engine->WindowOpen());
}

TEST_CASE("engine jump_in: gate window is kind jump_in and 800 ms") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeJumpInEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> identical = FindDuplicateOfTop(*engine);
    REQUIRE(identical.has_value());
    ForceHand(*engine, player1, {*identical});
    REQUIRE(engine->PlayCard("player1", *identical));
    REQUIRE(engine->WindowOpen());

    // INFO: the mod's integer `duration` overrides the 1000 ms env window.
    const json* opened = FindEvent(*engine, "window_open");
    REQUIRE(opened != nullptr);
    CHECK((*opened)["payload"]["duration_ms"] == 800);
    CHECK((*opened)["payload"]["kind"] == "jump_in");
    CHECK(engine->ExportWindow()["kind"] == "jump_in");
    CHECK(engine->ExportWindow()["deadline_ms"] == 800);
    for (const json& name : engine->ExportWindow()["responders"]) {
        CHECK(name != "player1");
    }
}

TEST_CASE("engine jump_in: a timeout with a winner resolves once") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeJumpInEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");

    const std::optional<ecs::Entity> twin = FindDuplicateOfTop(*engine);
    REQUIRE(twin.has_value());
    ForceHand(*engine, player1, {*twin});
    REQUIRE(engine->PlayCard("player1", *twin));
    REQUIRE(engine->WindowOpen());
    const std::optional<ecs::Entity> jumper_card = FindDuplicateOfTop(*engine);
    REQUIRE(jumper_card.has_value());
    ForceHand(*engine, player2, {*jumper_card});

    // INFO: player0 has not answered, so the solo window stays open with the
    //       winner recorded until its 800 ms duration elapses.
    REQUIRE(engine->RespondWindow("player2", *jumper_card));
    CHECK(engine->WindowOpen());
    CHECK(CountEvents(*engine, "window_close") == 0);

    clock.now = 800;
    engine->Tick();
    CHECK_FALSE(engine->WindowOpen());
    CHECK(CountEvents(*engine, "window_close") == 1);
    CHECK(engine->GetCurrentPlayerUsername() == "player2");

    engine->Tick();
    engine->Tick();
    CHECK(CountEvents(*engine, "window_close") == 1);
    CHECK(engine->GetCurrentPlayerUsername() == "player2");
}
