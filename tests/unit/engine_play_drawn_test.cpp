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
                                          int cards, uint64_t seed) {
    DeckDef deck = content.classic;
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
