#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/mod_loader.hpp>

#include <nlohmann/json.hpp>

#include <match/timers.hpp>

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
 * @file engine_play_drawn_test.cpp
 * @brief  engine-level play-drawn choice test (task 1: state + keep).
 *
 * A voluntary draw whose card is legal opens a play/keep choice: the turn is
 * held on the drawing player and `PendingPlayDrawnState()` reports the drawn
 * card. An unplayable draw advances immediately; `KeepDrawn` (owner only)
 * clears the choice and passes the turn, leaving the card in the hand. The
 * fixture helpers mirror `engine_forced_play_test.cpp`.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

constexpr int64_t kTimeLimitMs = 15'000;

/** @brief A settable engine clock so the turn deadline is deterministic. */
struct FakeClock {
    int64_t now = 0;

    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

/** @brief A rule mod whose `after:draw` hook force-plays a playable draw. */
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
    rule.title = " test force play";
    rule.hooks.push_back(entry);

    LoadedMod mod;
    mod.folder = "wp9d1_force";
    mod.manifest.id = "wp9d1_force";
    mod.manifest.name = " test force play";
    mod.manifest.version = "1.0.0";
    mod.manifest.api = "1";
    mod.rules.push_back(rule);
    return mod;
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

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          int cards, uint64_t seed,
                                          match::NowMs clock = nullptr) {
    DeckDef deck = content.classic;
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, deck, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    if (clock) {
        return std::make_unique<MatchInstance>(std::move(result.assembly),
                                               std::move(clock));
    }
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

/** @brief Top the draw pile with the plain wild (`jolly`) card. */
std::optional<ecs::Entity> WildTopCard(MatchInstance& engine) {
    ecs::PileContents* draw = Pile(engine, ecs::PileKind::kDraw);
    if (draw == nullptr) return std::nullopt;
    for (std::size_t i = 0; i < draw->cards.size(); ++i) {
        const ecs::FaceSpec* face =
            engine.Store().Get<ecs::FaceSpec>(draw->cards[i]);
        if (face == nullptr || face->color != "white") continue;
        if (face->label != "jolly") continue;
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

ecs::ZoneKind ZoneOf(MatchInstance& engine, ecs::Entity card) {
    const ecs::InZone* in = engine.Store().Get<ecs::InZone>(card);
    REQUIRE(in != nullptr);
    return in->zone.kind;
}

std::size_t HandSize(MatchInstance& engine, const std::string& username) {
    const std::optional<ecs::Entity> player = engine.FindPlayer(username);
    if (!player.has_value()) return 0;
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(*player);
    return hand == nullptr ? 0 : hand->cards.size();
}

bool HandContains(MatchInstance& engine, const std::string& username,
                  ecs::Entity card) {
    const std::optional<ecs::Entity> player = engine.FindPlayer(username);
    if (!player.has_value()) return false;
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(*player);
    if (hand == nullptr) return false;
    return std::find(hand->cards.begin(), hand->cards.end(), card)
        != hand->cards.end();
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

void SetActiveType(MatchInstance& engine, const std::string& type) {
    ecs::ActiveTypeReq* req = engine.Store().Get<ecs::ActiveTypeReq>(
        engine.Assembly().registries.match);
    REQUIRE(req != nullptr);
    req->type = type;
}

/** @brief Rewrite a card's identity/face so its facts change. */
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

/**
 * @brief Force `drawn` illegal: the restrictions read card facts (kind ids),
 * not face labels, so both colour and value must miss.
 */
void MakeUnplayable(MatchInstance& engine, ecs::Entity drawn) {
    ecs::PileContents* discard = Pile(engine, ecs::PileKind::kDiscard);
    REQUIRE(discard != nullptr);
    REQUIRE_FALSE(discard->cards.empty());
    // INFO: discard top blue 9, drawn card red 5, active colour green: the
    //       drawn card matches neither the active type nor the top value.
    SetCard(engine, discard->cards.back(), "vanilla:blue_9", "blue", "9");
    SetCard(engine, drawn, "vanilla:red_5", "red", "5");
    SetActiveType(engine, "green");
}

/** @brief Top the draw pile with a playable-by-colour numeric card. */
std::optional<ecs::Entity> ArmPlayableDraw(MatchInstance& engine) {
    const std::optional<ecs::Entity> drawn = NumericTopCard(engine);
    if (!drawn.has_value()) return std::nullopt;
    const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(*drawn);
    if (face == nullptr) return std::nullopt;
    SetActiveType(engine, face->color);
    return drawn;
}

}  // namespace

TEST_CASE("engine: playable voluntary draw holds the turn and reports the card") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    CHECK(engine->DrawCard("player0"));

    // INFO: the drawn card is playable, so the turn stays on player0 and the
    //       pending choice names that player and card.
    const std::optional<PendingPlayDrawn>& pending =
        engine->PendingPlayDrawnState();
    REQUIRE(pending.has_value());
    CHECK(pending->player == *engine->FindPlayer("player0"));
    CHECK(pending->card == *drawn);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
}

TEST_CASE("engine: KeepDrawn clears the choice, advances and keeps the card") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());
    const std::size_t drawn_hand = HandSize(*engine, "player0");

    CHECK(engine->KeepDrawn("player0"));

    CHECK_FALSE(engine->PendingPlayDrawnState().has_value());
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    CHECK(HandContains(*engine, "player0", *drawn));
    CHECK(HandSize(*engine, "player0") == drawn_hand);
}

TEST_CASE("engine: unplayable voluntary draw advances with no pending choice") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = NumericTopCard(*engine);
    REQUIRE(drawn.has_value());

    // INFO: the drawn card matches neither the active type nor the top value,
    //       so it is illegal to play.
    MakeUnplayable(*engine, *drawn);
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    CHECK(engine->DrawCard("player0"));

    CHECK_FALSE(engine->PendingPlayDrawnState().has_value());
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    CHECK(HandContains(*engine, "player0", *drawn));
}

TEST_CASE("engine: KeepDrawn by a non-owner is refused and leaves the choice") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    CHECK_FALSE(engine->KeepDrawn("player1"));
    CHECK(engine->PendingPlayDrawnState().has_value());
    CHECK(engine->GetCurrentPlayerUsername() == "player0");

    // INFO: the owner can still resolve the choice afterwards.
    CHECK(engine->KeepDrawn("player0"));
    CHECK_FALSE(engine->PendingPlayDrawnState().has_value());
}

TEST_CASE("engine: playing the held drawn card clears the choice and advances") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    CHECK(engine->PlayCard("player0", *drawn));

    CHECK_FALSE(engine->PendingPlayDrawnState().has_value());
    CHECK(ZoneOf(*engine, *drawn) == ecs::ZoneKind::kDiscardPile);
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine: playing another card while waiting is refused, state stays") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    // INFO: player0's first hand card is not the held draw.
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Hand* hand = engine->Store().Get<ecs::Hand>(player0);
    REQUIRE(hand != nullptr);
    REQUIRE_FALSE(hand->cards.empty());
    const ecs::Entity other = hand->cards.front();
    REQUIRE_FALSE(other == *drawn);

    CHECK_FALSE(engine->PlayCard("player0", other));

    CHECK(engine->PendingPlayDrawnState().has_value());
    CHECK(engine->PendingPlayDrawnState()->card == *drawn);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    CHECK(HandContains(*engine, "player0", other));
}

TEST_CASE("engine: drawing again while waiting is refused") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());
    const std::size_t hand = HandSize(*engine, "player0");

    CHECK_FALSE(engine->DrawCard("player0"));

    CHECK(engine->PendingPlayDrawnState().has_value());
    CHECK(HandSize(*engine, "player0") == hand);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
}

TEST_CASE("engine: a wild held draw opens the colour prompt when played") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    // INFO: a wild is always legal, so the voluntary draw holds the turn.
    const std::optional<ecs::Entity> drawn = WildTopCard(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    CHECK(engine->PlayCard("player0", *drawn));

    CHECK_FALSE(engine->PendingPlayDrawnState().has_value());
    const std::optional<json> pending = engine->PendingInput();
    REQUIRE(pending.has_value());
    CHECK((*pending)["kind"] == "choose_color");

    REQUIRE(engine->SubmitInput("player0", "red"));
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine: force_play resolves the drawn card and never parks the state") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(ForcePlayMod());
    content.classic.mods.push_back("wp9d1_force");

    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    CHECK(engine->DrawCard("player0"));

    // INFO: the after:draw graph force-played the drawn card before the hold
    //       path could run: no choice is parked and the turn advanced.
    CHECK_FALSE(engine->PendingPlayDrawnState().has_value());
    CHECK(ZoneOf(*engine, *drawn) == ecs::ZoneKind::kDiscardPile);
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine: a lapsed turn clock drops the held draw and advances") {
    FakeClock clock;
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7,
                                                       clock.Fn());

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    engine->SyncClocks(kTimeLimitMs);
    clock.now = kTimeLimitMs + 1;
    engine->Tick();

    CHECK_FALSE(engine->PendingPlayDrawnState().has_value());
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    CHECK(HandContains(*engine, "player0", *drawn));
    CHECK(ZoneOf(*engine, *drawn) == ecs::ZoneKind::kHand);
}

TEST_CASE("engine: a held draw neither suspends nor resets the turn clock") {
    FakeClock clock;
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7,
                                                       clock.Fn());

    // INFO: arm the turn clock BEFORE the draw so the recorded deadline is
    //       the one the hold must leave alone (a suspend would zero it).
    engine->SyncClocks(kTimeLimitMs);
    const int64_t armed = engine->CurrentTurnDeadlineMs();
    REQUIRE(armed > 0);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    // INFO: parking the hold must not suspend or re-arm the clock.
    CHECK(engine->CurrentTurnDeadlineMs() == armed);

    REQUIRE(engine->KeepDrawn("player0"));
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
    // INFO: the keep advances the turn; the incoming seat is left unarmed
    //       for the controller to arm, not handed the outgoing seat's clock.
    CHECK(engine->CurrentTurnDeadlineMs() == 0);
}

TEST_CASE("engine: other players route as today while a held draw waits") {
    Content content;
    REQUIRE(LoadContent(content));

    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 5, 7);

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    // INFO: the hold only constrains the holder; a non-holder's attempt while
    //       the holder still waits is refused (turn order), leaving the
    //       choice and the current player untouched. Assert the refusals
    //       explicitly so the ordinary pipeline cannot silently accept them.
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Hand* hand = engine->Store().Get<ecs::Hand>(player1);
    REQUIRE(hand != nullptr);
    REQUIRE_FALSE(hand->cards.empty());

    CHECK_FALSE(engine->PlayCard("player1", *drawn));
    CHECK(engine->PendingPlayDrawnState().has_value());
    CHECK(engine->PendingPlayDrawnState()->card == *drawn);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");

    CHECK_FALSE(engine->PlayCard("player1", hand->cards.front()));
    CHECK(engine->PendingPlayDrawnState().has_value());
    CHECK(engine->PendingPlayDrawnState()->card == *drawn);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
}
