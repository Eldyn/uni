#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/mod_loader.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_progressive_test.cpp
 * @brief Engine-level progressive draw-until-playable test.
 *
 * A synthetic rule subscribes `turn_start` and runs the new
 * `draw_until_playable` op for the current player. The engine must draw from
 * the draw pile until a playable card turns up and stop there: the hand holds
 * the unplayable cards plus the playable one, a buried card is left on the
 * pile, and a `cards_drawn` event reports the count. The op only draws - no
 * play and no turn advance.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

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

/**
 * @brief A rule mod whose `turn_start` hook draws until the player can play.
 */
LoadedMod ProgressiveMod() {
    const json nodes = json::array({
        json{{"id", "n1"},
             {"op", "draw_until_playable"},
             {"args", json{{"target", "@self"}}}},
    });
    const json graph = json{{"nodes", nodes}};

    BehaviorGraph behavior;
    behavior.raw = graph;
    behavior.nodes = nodes;

    BehaviorEntry entry;
    entry.hook = "turn_start";
    entry.graph = behavior;

    RuleDef rule;
    rule.id = "progressive";
    rule.namespace_id = "wp9d1b_progressive";
    rule.rule_id = "wp9d1b_progressive:progressive";
    rule.title = "test progressive draw";
    rule.hooks.push_back(entry);

    LoadedMod mod;
    mod.folder = "wp9d1b_progressive";
    mod.manifest.id = "wp9d1b_progressive";
    mod.manifest.name = "test progressive draw";
    mod.manifest.version = "1.0.0";
    mod.manifest.api = "1";
    mod.rules.push_back(rule);
    return mod;
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

ecs::PileContents* Pile(MatchInstance& engine, ecs::PileKind kind) {
    const std::vector<ecs::Entity> piles =
        engine.Store().EntitiesWith<ecs::PileContents>();
    for (ecs::Entity entity : piles) {
        ecs::PileContents* contents =
            engine.Store().Get<ecs::PileContents>(entity);
        if (contents != nullptr && contents->kind == kind) return contents;
    }
    return nullptr;
}

ecs::PileContents* AssemblyPile(MatchAssembly& assembly,
                                ecs::PileKind kind) {
    const std::vector<ecs::Entity> piles =
        assembly.store.EntitiesWith<ecs::PileContents>();
    for (ecs::Entity entity : piles) {
        ecs::PileContents* contents =
            assembly.store.Get<ecs::PileContents>(entity);
        if (contents != nullptr && contents->kind == kind) return contents;
    }
    return nullptr;
}

/** @brief Reorder a pile so `top` is drawn first, then `below`. */
void PlaceTop(ecs::EntityStore& store, ecs::PileContents* pile,
              ecs::Entity below, ecs::Entity top) {
    std::vector<ecs::Entity> cards = pile->cards;
    cards.erase(std::remove(cards.begin(), cards.end(), below), cards.end());
    cards.erase(std::remove(cards.begin(), cards.end(), top), cards.end());
    cards.push_back(below);
    cards.push_back(top);
    pile->cards = cards;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (ecs::InZone* in = store.Get<ecs::InZone>(cards[i])) {
            in->ordinal = static_cast<uint32_t>(i);
        }
    }
}

}  // namespace

TEST_CASE("engine: progressive draw-until-playable runs via the op") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(ProgressiveMod());

    DeckDef deck = content.classic;
    deck.mods = {"vanilla", "wp9d1b_progressive"};
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, Players(2, 5, 7));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));

    MatchAssembly& assembly = *result.assembly;
    ecs::PileContents* draw = AssemblyPile(assembly, ecs::PileKind::kDraw);
    REQUIRE(draw != nullptr);
    REQUIRE(draw->cards.size() >= 3);

    // INFO: pick a playable card from the draw pile and make its colour the
    //       active type, then pick a different-coloured card to bury on top.
    const std::vector<ecs::Entity> cards = draw->cards;
    std::optional<ecs::Entity> playable;
    std::string active;
    for (ecs::Entity card : cards) {
        const ecs::FaceSpec* face = assembly.store.Get<ecs::FaceSpec>(card);
        if (face != nullptr && !face->color.empty() && face->color != "white") {
            playable = card;
            active = face->color;
            break;
        }
    }
    REQUIRE(playable.has_value());

    std::optional<ecs::Entity> unplayable;
    for (ecs::Entity card : cards) {
        if (card == *playable) continue;
        ecs::FaceSpec* face = assembly.store.Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color != active
            && face->color != "white") {
            face->label = "zzz";
            unplayable = card;
            break;
        }
    }
    REQUIRE(unplayable.has_value());

    ecs::ActiveTypeReq* req =
        assembly.store.Get<ecs::ActiveTypeReq>(assembly.registries.match);
    REQUIRE(req != nullptr);
    req->type = active;

    // INFO: neutralise the discard top's value so only colour drives play.
    ecs::PileContents* discard =
        AssemblyPile(assembly, ecs::PileKind::kDiscard);
    REQUIRE(discard != nullptr);
    REQUIRE_FALSE(discard->cards.empty());
    if (ecs::FaceSpec* top =
            assembly.store.Get<ecs::FaceSpec>(discard->cards.back())) {
        top->label = "___";
    }

    PlaceTop(assembly.store, draw, *playable, *unplayable);

    // INFO: construction fires `turn_start`; the rule draws until playable.
    std::unique_ptr<MatchInstance> engine =
        std::make_unique<MatchInstance>(std::move(result.assembly));

    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    const std::optional<ecs::Entity> player = engine->FindPlayer("player0");
    REQUIRE(player.has_value());
    const ecs::Hand* hand = engine->Store().Get<ecs::Hand>(*player);
    REQUIRE(hand != nullptr);

    // INFO: the unplayable card was drawn first, then the playable one; both
    //       are in hand (5 starting + 2) and neither is on the pile.
    CHECK(hand->cards.size() == 7);
    CHECK(std::find(hand->cards.begin(), hand->cards.end(), *unplayable)
          != hand->cards.end());
    CHECK(std::find(hand->cards.begin(), hand->cards.end(), *playable)
          != hand->cards.end());
    CHECK(engine->Store().Get<ecs::InZone>(*playable)->zone.kind
          == ecs::ZoneKind::kHand);
    CHECK(engine->Store().Get<ecs::InZone>(*unplayable)->zone.kind
          == ecs::ZoneKind::kHand);

    bool drew_two = false;
    for (const json& event : engine->Events()) {
        if (event.value("type", std::string()) != "cards_drawn") continue;
        if (!event.contains("payload") || !event["payload"].is_object()) {
            continue;
        }
        if (event["payload"].value("count", 0) == 2) drew_two = true;
    }
    CHECK(drew_two);
    CHECK_FALSE(engine->IsMatchOver());
}
