#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
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
 * @file engine_clocks_test.cpp
 * @brief Separate turn and prompt clocks (`MatchInstance::SyncClocks`).
 *
 * The turn clock is armed once per turn and never re-armed by an action inside
 * it; a pending prompt pauses the turn clock and runs its own deadline; the
 * next turn starts from a fresh full clock.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;

namespace {

using nlohmann::json;

constexpr int64_t kTimeLimitMs = 15'000;

struct FakeClock {
    int64_t now = 0;

    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

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

std::unique_ptr<MatchInstance> MakeVanillaEngine(match::NowMs clock) {
    LoadResult load = ScanModsDirectory((ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    DeckDef classic;
    for (const LoadedMod& mod : load.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "vanilla:classic") classic = deck;
        }
    }
    REQUIRE_FALSE(classic.deck_id.empty());
    classic.mods = {"vanilla"};

    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 2; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(load.mods, classic, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly),
                                           match::WindowConfig{},
                                           std::move(clock));
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

int64_t PromptDeadline(const MatchInstance& engine) {
    const std::optional<json> pending = engine.PendingInput();
    REQUIRE(pending.has_value());
    return pending->value("deadline_ms", int64_t{0});
}

}  // namespace

TEST_CASE("engine clocks: the turn clock is armed once per turn") {
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeVanillaEngine(clock.Fn());

    engine->SyncClocks(kTimeLimitMs);
    CHECK(engine->CurrentTurnDeadlineMs() == kTimeLimitMs);

    clock.now = 4'000;
    engine->SyncClocks(kTimeLimitMs);
    CHECK(engine->CurrentTurnDeadlineMs() == kTimeLimitMs);
}

TEST_CASE("engine clocks: a prompt pauses the turn and runs its own clock") {
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeVanillaEngine(clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> wild = FindCard(*engine, "white", "jolly");
    const std::optional<ecs::Entity> filler = FindCard(*engine, "blue", "5");
    REQUIRE(wild.has_value());
    REQUIRE(filler.has_value());
    ForceHand(*engine, player0, {*wild, *filler});
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    engine->SyncClocks(kTimeLimitMs);

    clock.now = 3'000;
    REQUIRE(engine->PlayCard("player0", *wild));
    engine->SyncClocks(kTimeLimitMs);
    CHECK(engine->CurrentTurnDeadlineMs() == 0);
    CHECK(engine->Timers().Turn().RemainingMs() == kTimeLimitMs - 3'000);
    CHECK(PromptDeadline(*engine) == 3'000 + kTimeLimitMs);

    clock.now = 10'000;
    engine->SyncClocks(kTimeLimitMs);
    CHECK(PromptDeadline(*engine) == 3'000 + kTimeLimitMs);

    REQUIRE(engine->SubmitInput("player0", "red"));
    engine->SyncClocks(kTimeLimitMs);
    REQUIRE(engine->GetCurrentPlayerUsername() == "player1");
    CHECK(engine->CurrentTurnDeadlineMs() == 10'000 + kTimeLimitMs);
}
