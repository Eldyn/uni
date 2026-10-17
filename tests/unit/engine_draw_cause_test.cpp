#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/mod_loader.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
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
 * @file engine_draw_cause_test.cpp
 * @brief Draw hooks carry a `cause` so draw-reacting mods only fire on the
 *        draws they are meant for.
 *
 * Progressive must react to the player's voluntary draw only: its own
 * `draw_until_playable` draws must not re-enter it (the re-entry cap used to
 * trip and disarm the mod). Force Play must not force-play a card drawn by an
 * effect (a +2 penalty, a rule's `draw_cards`).
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;

namespace {

using nlohmann::json;

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

/** @brief A rule mod whose `turn_start` hook draws one card as an effect. */
LoadedMod EffectDrawMod() {
    const json nodes = json::array({
        json{{"id", "n1"},
             {"op", "draw_cards"},
             {"args", json{{"target", "@self"}, {"n", 1}}}},
    });

    BehaviorGraph behavior;
    behavior.raw = json{{"nodes", nodes}};
    behavior.nodes = nodes;

    BehaviorEntry entry;
    entry.hook = "turn_start";
    entry.graph = behavior;

    RuleDef rule;
    rule.id = "effect_draw";
    rule.namespace_id = "draw_cause_test";
    rule.rule_id = "draw_cause_test:effect_draw";
    rule.title = "Draw-cause test effect draw";
    rule.hooks.push_back(entry);

    LoadedMod mod;
    mod.folder = "draw_cause_test";
    mod.manifest.id = "draw_cause_test";
    mod.manifest.name = "Draw-cause test effect draw";
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

ecs::PileContents* Pile(ecs::EntityStore& store, ecs::PileKind kind) {
    for (ecs::Entity entity : store.EntitiesWith<ecs::PileContents>()) {
        ecs::PileContents* contents = store.Get<ecs::PileContents>(entity);
        if (contents != nullptr && contents->kind == kind) return contents;
    }
    return nullptr;
}

bool IsNumeric(const ecs::FaceSpec* face) {
    // INFO: 0 and 7 are skipped: the seven_zero mod gives them hand-swap effects.
    return face != nullptr && face->color != "white" && face->label.size() == 1
           && std::isdigit(static_cast<unsigned char>(face->label[0]))
           && face->label != "0" && face->label != "7";
}

/**
 * @brief Stack the draw pile so `unplayable_count` dead cards are drawn
 *        first, then one card playable on the active colour.
 *
 * The dead cards are relabelled so neither colour nor value matches; the
 * discard top is neutralised the same way.
 *
 * @return the playable card, or nullopt when the pile lacks candidates.
 */
std::optional<ecs::Entity> StackDrawPile(MatchAssembly& assembly,
                                         std::size_t unplayable_count) {
    ecs::EntityStore& store = assembly.store;
    ecs::PileContents* draw = Pile(store, ecs::PileKind::kDraw);
    ecs::PileContents* discard = Pile(store, ecs::PileKind::kDiscard);
    if (draw == nullptr || discard == nullptr || discard->cards.empty()) {
        return std::nullopt;
    }

    std::optional<ecs::Entity> playable;
    for (ecs::Entity card : draw->cards) {
        if (IsNumeric(store.Get<ecs::FaceSpec>(card))) {
            playable = card;
            break;
        }
    }
    if (!playable.has_value()) return std::nullopt;
    const std::string active = store.Get<ecs::FaceSpec>(*playable)->color;

    std::vector<ecs::Entity> dead;
    for (ecs::Entity card : draw->cards) {
        if (dead.size() == unplayable_count) break;
        ecs::FaceSpec* face = store.Get<ecs::FaceSpec>(card);
        if (!IsNumeric(face) || face->color == active) continue;
        face->label = "zzz";
        dead.push_back(card);
    }
    if (dead.size() != unplayable_count) return std::nullopt;

    std::vector<ecs::Entity> cards;
    for (ecs::Entity card : draw->cards) {
        if (card == *playable) continue;
        if (std::find(dead.begin(), dead.end(), card) != dead.end()) continue;
        cards.push_back(card);
    }
    cards.push_back(*playable);
    cards.insert(cards.end(), dead.rbegin(), dead.rend());
    draw->cards = cards;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (ecs::InZone* in = store.Get<ecs::InZone>(cards[i])) {
            in->ordinal = static_cast<uint32_t>(i);
        }
    }

    if (ecs::FaceSpec* top = store.Get<ecs::FaceSpec>(discard->cards.back())) {
        top->label = "___";
    }
    ecs::ActiveTypeReq* req =
        store.Get<ecs::ActiveTypeReq>(assembly.registries.match);
    if (req == nullptr) return std::nullopt;
    req->type = active;
    return playable;
}

std::size_t HandSize(MatchInstance& engine, const std::string& username) {
    const std::optional<ecs::Entity> player = engine.FindPlayer(username);
    if (!player.has_value()) return 0;
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(*player);
    return hand == nullptr ? 0 : hand->cards.size();
}

ecs::ZoneKind ZoneOf(MatchInstance& engine, ecs::Entity card) {
    const ecs::InZone* in = engine.Store().Get<ecs::InZone>(card);
    REQUIRE(in != nullptr);
    return in->zone.kind;
}

}  // namespace

TEST_CASE("engine: progressive keeps drawing without re-entering itself") {
    Content content;
    REQUIRE(LoadContent(content));

    DeckDef deck = content.classic;
    deck.mods = {"vanilla", "progressive"};
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, Players(2, 5, 7));
    REQUIRE(result.ok());

    const std::optional<ecs::Entity> playable =
        StackDrawPile(*result.assembly, 3);
    REQUIRE(playable.has_value());

    std::unique_ptr<MatchInstance> engine =
        std::make_unique<MatchInstance>(std::move(result.assembly));
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    CHECK(engine->DrawCard("player0"));

    // INFO: the voluntary draw took a dead card, progressive then drew the
    //       two remaining dead cards and stopped on the playable one.
    CHECK(HandSize(*engine, "player0") == 5 + 4);
    CHECK(ZoneOf(*engine, *playable) == ecs::ZoneKind::kHand);
    CHECK_FALSE(engine->Assembly().bus.IsDisarmed("progressive"));
}

namespace {

/** @brief Assemble a two-player match with `mods` and a stacked draw pile. */
struct ProgressiveFixture {
    Content content;
    std::unique_ptr<MatchInstance> engine;
    std::optional<ecs::Entity> playable;

    void Start(const std::vector<std::string>& mods,
               std::size_t unplayable_count) {
        REQUIRE(LoadContent(content));
        DeckDef deck = content.classic;
        deck.mods = mods;
        AssemblyResult result =
            MatchAssembler::Assemble(content.mods, deck, Players(2, 5, 7));
        REQUIRE(result.ok());
        playable = StackDrawPile(*result.assembly, unplayable_count);
        REQUIRE(playable.has_value());
        engine = std::make_unique<MatchInstance>(std::move(result.assembly));
        REQUIRE(engine->GetCurrentPlayerUsername() == "player0");
    }
};

}  // namespace

TEST_CASE("engine: progressive holds the turn on the playable it stopped on") {
    ProgressiveFixture f;
    f.Start({"vanilla", "progressive"}, 3);

    CHECK(f.engine->DrawCard("player0"));

    // INFO: progressive drew until a playable card turned up; the player now
    //       chooses to play or keep it, the turn does not pass.
    CHECK(HandSize(*f.engine, "player0") == 5 + 4);
    CHECK(f.engine->GetCurrentPlayerUsername() == "player0");
    const std::optional<PendingPlayDrawn>& pending =
        f.engine->PendingPlayDrawnState();
    REQUIRE(pending.has_value());
    CHECK(pending->card == *f.playable);

    REQUIRE(f.engine->PlayCard("player0", *f.playable));
    CHECK(ZoneOf(*f.engine, *f.playable) == ecs::ZoneKind::kDiscardPile);
    CHECK(f.engine->GetCurrentPlayerUsername() == "player1");
    CHECK_FALSE(f.engine->PendingPlayDrawnState().has_value());
}

TEST_CASE("engine: progressive keep leaves the playable card in hand") {
    ProgressiveFixture f;
    f.Start({"vanilla", "progressive"}, 3);

    REQUIRE(f.engine->DrawCard("player0"));
    REQUIRE(f.engine->PendingPlayDrawnState().has_value());

    REQUIRE(f.engine->KeepDrawn("player0"));
    CHECK(ZoneOf(*f.engine, *f.playable) == ecs::ZoneKind::kHand);
    CHECK(HandSize(*f.engine, "player0") == 5 + 4);
    CHECK(f.engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine: progressive with force play plays the playable it stopped on") {
    const std::vector<std::vector<std::string>> orders = {
        {"vanilla", "progressive", "force_play"},
        {"vanilla", "force_play", "progressive"},
        {"vanilla", "seven_zero", "draw_stacking", "force_play", "jump_in",
         "progressive"}};
    for (const std::vector<std::string>& mods : orders) {
        CAPTURE(mods.back());
        ProgressiveFixture f;
        f.Start(mods, 3);

        CHECK(f.engine->DrawCard("player0"));

        // INFO: the three dead cards stay in hand (5 + 4 drawn - 1 played);
        //       the playable one is forced out, and no further card is drawn.
        CHECK(ZoneOf(*f.engine, *f.playable) == ecs::ZoneKind::kDiscardPile);
        CHECK(HandSize(*f.engine, "player0") == 5 + 3);
        CHECK_FALSE(f.engine->PendingPlayDrawnState().has_value());
        // INFO: with jump_in active the play opens a jump-in window first, so
        //       the turn passes only when that window closes.
        const bool has_jump_in =
            std::find(mods.begin(), mods.end(), "jump_in") != mods.end();
        if (!has_jump_in) {
            CHECK(f.engine->GetCurrentPlayerUsername() == "player1");
        }
    }
}

TEST_CASE("engine: force play ignores a card drawn by an effect") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(EffectDrawMod());

    DeckDef deck = content.classic;
    deck.mods = {"vanilla", "force_play", "draw_cause_test"};
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, Players(2, 5, 7));
    REQUIRE(result.ok());

    const std::optional<ecs::Entity> playable =
        StackDrawPile(*result.assembly, 0);
    REQUIRE(playable.has_value());

    // INFO: construction fires `turn_start`; the rule draws the playable
    //       card as an effect, which force play must leave in hand.
    std::unique_ptr<MatchInstance> engine =
        std::make_unique<MatchInstance>(std::move(result.assembly));

    CHECK(ZoneOf(*engine, *playable) == ecs::ZoneKind::kHand);
    CHECK(HandSize(*engine, "player0") == 5 + 1);
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    // INFO: a queued forced play would surface at the next flow-safe point;
    //       the voluntary draw must not drag the effect-drawn card along.
    CHECK(engine->DrawCard("player0"));
    CHECK(ZoneOf(*engine, *playable) == ecs::ZoneKind::kHand);
}

TEST_CASE("engine: force play still plays a playable voluntary draw") {
    Content content;
    REQUIRE(LoadContent(content));

    DeckDef deck = content.classic;
    deck.mods = {"vanilla", "force_play"};
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, Players(2, 5, 7));
    REQUIRE(result.ok());

    const std::optional<ecs::Entity> playable =
        StackDrawPile(*result.assembly, 0);
    REQUIRE(playable.has_value());

    std::unique_ptr<MatchInstance> engine =
        std::make_unique<MatchInstance>(std::move(result.assembly));

    CHECK(engine->DrawCard("player0"));
    CHECK(ZoneOf(*engine, *playable) == ecs::ZoneKind::kDiscardPile);
    CHECK(HandSize(*engine, "player0") == 5);
}
