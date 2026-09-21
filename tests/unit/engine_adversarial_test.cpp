#include <doctest/doctest.h>

#include <common/env.hpp>

#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/ecs/compact_card.hpp>
#include <match/modload/mod_loader.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_adversarial_test.cpp
 * @brief adversarial engine tests.
 *
 * Stress cases over the public `MatchInstance` surface: a 1000-card hand
 * match, a deck built from every vanilla kind, and the engine-level
 * defense-in-depth assertion that a chain-budget abort leaves the match
 * playable. Every generated match is deterministic (fixed seed).
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

/* INFO: locate the project root from this file so the test is cwd-independent
 *       (mirrors engine_core_test.cpp). */
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

std::unique_ptr<MatchInstance> AssembleEngine(Content& content,
                                              const DeckDef& deck, int players,
                                              int cards, uint64_t seed) {
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, deck, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
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

/** @brief Every kind id the vanilla mod declares (kind table coverage). */
std::vector<std::string> VanillaKinds(const Content& content) {
    std::vector<std::string> kinds;
    for (const LoadedMod& mod : content.mods) {
        if (mod.manifest.id != "vanilla") continue;
        for (const CardDef& card : mod.cards) kinds.push_back(card.kind_id);
    }
    return kinds;
}

/** INFO: scoped env override; the resolver captures guards at construction. */
struct EnvGuard {
    std::string key;
    std::string old;
    bool had = false;

    EnvGuard(std::string k, std::string value) : key(std::move(k)) {
        const char* current = std::getenv(key.c_str());
        if (current != nullptr) {
            had = true;
            old = current;
        }
        Env::SetEnv(key, value);
    }

    ~EnvGuard() { Env::SetEnv(key, had ? old : std::string()); }
};

}  // namespace

TEST_CASE("engine adversarial: thousand-card hands assemble and play") {
    Content content;
    REQUIRE(LoadContent(content));

    // INFO: two 1000-card hands plus a draw pile need > 2000 copies, still
    //       inside kMaxInstancesPerKind (4096).
    DeckDef deck = content.classic;
    deck.cards = {{"vanilla:red_5", 2200}};
    std::unique_ptr<MatchInstance> engine =
        AssembleEngine(content, deck, 2, 1000, 42);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    CHECK(HandSize(*engine, player0) == 1000);
    CHECK(HandSize(*engine, player1) == 1000);
    // INFO: the numeric starter is taken from the draw pile after dealing.
    CHECK(PileSize(*engine, ecs::PileKind::kDraw) == 199);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    CHECK(engine->GetCurrentPlayer().has_value());

    const ecs::Entity card =
        engine->Store().Get<ecs::Hand>(player0)->cards.front();
    REQUIRE(engine->PlayCard("player0", card));
    CHECK(HandSize(*engine, player0) == 999);
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    CHECK_FALSE(engine->IsMatchOver());

    REQUIRE(engine->DrawCard("player1"));
    CHECK(HandSize(*engine, player1) == 1001);
    CHECK(PileSize(*engine, ecs::PileKind::kDraw) == 198);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");

    engine->Tick();
    CHECK(engine->GetCurrentPlayer().has_value());
    CHECK_FALSE(engine->IsMatchOver());
}

TEST_CASE("engine adversarial: many-kind deck assembles and advances a turn") {
    Content content;
    REQUIRE(LoadContent(content));

    const std::vector<std::string> kinds = VanillaKinds(content);
    // INFO: cover every kind vanilla ships.
    MESSAGE("vanilla distinct kinds: " << kinds.size());
    REQUIRE(kinds.size() >= 40);

    DeckDef deck = content.classic;
    deck.cards.clear();
    for (const std::string& kind : kinds) deck.cards.push_back({kind, 4});
    std::unique_ptr<MatchInstance> engine =
        AssembleEngine(content, deck, 4, 7, 42);

    const std::string before = engine->GetCurrentPlayerUsername();
    REQUIRE(engine->DrawCard(before));
    CHECK(engine->GetCurrentPlayerUsername() != before);
    CHECK(engine->GetCurrentPlayer().has_value());
    CHECK_FALSE(engine->IsMatchOver());
}

TEST_CASE("engine adversarial: budget abort leaves the match playable") {
    EnvGuard budget("UNI_CHAIN_BUDGET", "0");
    EnvGuard disarm("UNI_MOD_DISARM_THRESHOLD", "1");
    Content content;
    REQUIRE(LoadContent(content));

    // INFO: red_draw2 has a multi-node, prompt-free on_play graph, so a zero
    //       chain budget aborts at its first node.
    DeckDef deck = content.classic;
    deck.cards = {{"vanilla:red_draw2", 20}};
    std::unique_ptr<MatchInstance> engine =
        AssembleEngine(content, deck, 2, 7, 42);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity card =
        engine->Store().Get<ecs::Hand>(player0)->cards.front();
    CHECK(engine->PlayCard("player0", card));
    // INFO: the abort must not wedge the match; the play settles and the
    //       next seat can still act.
    CHECK(engine->GetCurrentPlayer().has_value());
    CHECK_FALSE(engine->IsMatchOver());

    const std::string current = engine->GetCurrentPlayerUsername();
    REQUIRE(engine->DrawCard(current));
    CHECK(engine->GetCurrentPlayer().has_value());
    CHECK_FALSE(engine->IsMatchOver());
}
