#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/mod_loader.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_forced_play_test.cpp
 * @brief Engine-level forced-play test.
 *
 * A synthetic rule mod subscribes `after:draw`, branches on the new
 * `drawn_card_playable` condition and emits a `play_card` op for the drawn
 * card. The engine must route that effect through the normal play pipeline:
 * the drawn card lands on the discard (not the hand) and a `card_played` event
 * is emitted. The negative case proves the condition gates the play.
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
 * @brief A rule mod whose `after:draw` hook force-plays a playable drawn card.
 *
 * Graph: branch on `drawn_card_playable` -> `play_card` (card `@drawn_card`,
 * player `@self`); otherwise emit a marker signal.
 */
LoadedMod ForcePlayMod() {
    const json nodes = json::array({
        json{{"id", "n1"},
             {"cases", json::array({json{
                 {"when", json{{"drawn_card_playable", json::object()}}},
                 {"next", "n2"}}})},
             {"else", "n3"}},
        json{{"id", "n2"},
             {"op", "play_card"},
             {"args", json{{"card", "@drawn_card"}, {"player", "@self"}}}},
        json{{"id", "n3"},
             {"op", "emit_signal"},
             {"args", json{{"name", "wp9d1_skip"}}}},
    });
    const json graph = json{{"nodes", nodes}};

    BehaviorGraph behavior;
    behavior.raw = graph;
    behavior.nodes = nodes;

    BehaviorEntry entry;
    entry.hook = "after:draw";
    entry.graph = behavior;

    RuleDef rule;
    rule.id = "force";
    rule.namespace_id = "wp9d1_force";
    rule.rule_id = "wp9d1_force:force";
    rule.title = "test force play";
    rule.hooks.push_back(entry);

    LoadedMod mod;
    mod.folder = "wp9d1_force";
    mod.manifest.id = "wp9d1_force";
    mod.manifest.name = "test force play";
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

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          int cards, uint64_t seed) {
    DeckDef deck = content.classic;
    deck.mods = {"vanilla", "wp9d1_force"};
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, deck, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
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

std::size_t HandSize(MatchInstance& engine, const std::string& username) {
    const std::optional<ecs::Entity> player = engine.FindPlayer(username);
    if (!player.has_value()) return 0;
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(*player);
    return hand == nullptr ? 0 : hand->cards.size();
}

/** @brief Move a numeric, non-wild card to the top of the draw pile. */
std::optional<ecs::Entity> NumericTopCard(MatchInstance& engine) {
    ecs::PileContents* draw = Pile(engine, ecs::PileKind::kDraw);
    if (draw == nullptr) return std::nullopt;
    for (std::size_t i = 0; i < draw->cards.size(); ++i) {
        const ecs::FaceSpec* face =
            engine.Store().Get<ecs::FaceSpec>(draw->cards[i]);
        if (face == nullptr || face->color == "white") continue;
        if (face->label.size() != 1) continue;
        if (!std::isdigit(static_cast<unsigned char>(face->label[0]))) {
            continue;
        }
        std::swap(draw->cards[i], draw->cards.back());
        for (std::size_t j = 0; j < draw->cards.size(); ++j) {
            if (ecs::InZone* in =
                    engine.Store().Get<ecs::InZone>(draw->cards[j])) {
                in->ordinal = static_cast<uint32_t>(j);
            }
        }
        return draw->cards.back();
    }
    return std::nullopt;
}

bool HasEvent(const MatchInstance& engine, const std::string& type,
              ecs::Entity card) {
    for (const json& event : engine.Events()) {
        if (event.value("type", std::string()) != type) continue;
        if (!event.contains("payload") || !event["payload"].is_object()) {
            continue;
        }
        const json& payload = event["payload"];
        if (payload.contains("card") && payload["card"].is_object()
            && payload["card"].value("index", 0u) == card.index) {
            return true;
        }
    }
    return false;
}

/** @brief True when a named `emit_signal` packet was observed. */
bool HasSignal(const MatchInstance& engine, const std::string& name) {
    for (const json& event : engine.Events()) {
        if (event.value("type", std::string()) != "signal") continue;
        if (!event.contains("payload") || !event["payload"].is_object()) {
            continue;
        }
        if (event["payload"].value("name", std::string()) == name) return true;
    }
    return false;
}

void SetActiveType(MatchInstance& engine, const std::string& type) {
    ecs::ActiveTypeReq* req = engine.Store().Get<ecs::ActiveTypeReq>(
        engine.Assembly().registries.match);
    REQUIRE(req != nullptr);
    req->type = type;
}

/**
 * @brief Rewrite a card's identity and face together.
 *
 * The restriction pipeline reads frozen `kind_id` facts while the
 * `drawn_card_playable` condition reads the mutable `FaceSpec`; a card is only
 * genuinely unplayable when both agree.
 */
void SetCard(MatchInstance& engine, ecs::Entity card, const std::string& kind,
             const std::string& color, const std::string& label) {
    if (ecs::CardIdentity* id = engine.Store().Get<ecs::CardIdentity>(card)) {
        id->kind_id = kind;
    }
    if (ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card)) {
        face->color = color;
        face->label = label;
    }
}

}  // namespace

TEST_CASE("engine: force-play of a playable drawn card runs the pipeline") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(ForcePlayMod());

    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = NumericTopCard(*engine);
    REQUIRE(drawn.has_value());
    const ecs::FaceSpec* face = engine->Store().Get<ecs::FaceSpec>(*drawn);
    REQUIRE(face != nullptr);
    SetActiveType(*engine, face->color);

    const std::size_t before = HandSize(*engine, "player0");
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    CHECK(engine->DrawCard("player0"));

    // INFO: the drawn card was force-played: it is on the discard, not in hand
    //       (draw +1, play -1 => unchanged hand size) and a `card_played`
    //       event references it. The turn advanced to the opponent.
    CHECK(engine->Store().Get<ecs::InZone>(*drawn) != nullptr);
    CHECK(engine->Store().Get<ecs::InZone>(*drawn)->zone.kind
          == ecs::ZoneKind::kDiscardPile);
    CHECK(HandSize(*engine, "player0") == before);
    CHECK(HasEvent(*engine, "card_played", *drawn));
    CHECK_FALSE(HasSignal(*engine, "wp9d1_skip"));
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine: force-play rule leaves an unplayable drawn card in hand") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(ForcePlayMod());

    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = NumericTopCard(*engine);
    REQUIRE(drawn.has_value());

    // INFO: make the card genuinely unplayable to BOTH the restriction
    //       pipeline (which reads frozen `kind_id` facts) and the
    //       `drawn_card_playable` condition (which reads the mutable
    //       `FaceSpec`): a red 5 on a blue 9 with green active matches
    //       neither colour nor value, so the graph's condition is false and
    //       the skip branch runs (no `play_card` op is ever emitted).
    ecs::PileContents* discard = Pile(*engine, ecs::PileKind::kDiscard);
    REQUIRE(discard != nullptr);
    REQUIRE_FALSE(discard->cards.empty());
    SetCard(*engine, discard->cards.back(), "vanilla:blue_9", "blue", "9");
    SetCard(*engine, *drawn, "vanilla:red_5", "red", "5");
    SetActiveType(*engine, "green");

    const std::size_t before = HandSize(*engine, "player0");
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    CHECK(engine->DrawCard("player0"));

    // INFO: no forced play; the card stays in hand (draw +1), the graph took
    //       the skip branch (proving the `drawn_card_playable` condition --
    //       not the restriction pipeline -- gated the play) and the turn
    //       passes to the opponent.
    CHECK(engine->Store().Get<ecs::InZone>(*drawn)->zone.kind
          == ecs::ZoneKind::kHand);
    CHECK(HandSize(*engine, "player0") == before + 1);
    CHECK_FALSE(HasEvent(*engine, "card_played", *drawn));
    CHECK(HasSignal(*engine, "wp9d1_skip"));
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}
