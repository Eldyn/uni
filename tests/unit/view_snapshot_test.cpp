#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/status.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file view_snapshot_test.cpp
 * @brief Per-recipient reconnect snapshot tests.
 *
 * One positive and one negative assertion per visibility rule: the base
 * snapshot exposes only the viewer's own hand identity, the draw pile count
 * and the discard top; `VisibilityGrant` aspects reveal another hand or a
 * pile; spectators are omniscient except for players opted into
 * `privacy_from_spectators`; own-hand entries carry `can_play`.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;
using match::view::EventSink;
using match::view::SnapshotOptions;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

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

std::unique_ptr<MatchInstance> MakeEngine(Content& content,
                                          std::size_t players,
                                          uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (std::size_t i = 0; i < players; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

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

/** @brief A 3-player match with the draw_stacking window rules active. */
std::unique_ptr<MatchInstance> MakeWindowEngine(Content& content) {
    FakeClock clock;
    DeckDef deck = content.classic;
    deck.mods = {"vanilla", "draw_stacking"};
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 3; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly),
                                           FixedWindow(1000), clock.Fn());
}

std::vector<ecs::Entity> CardsByKind(const MatchInstance& engine,
                                     const std::string& kind) {
    std::vector<ecs::Entity> out;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind) {
            out.push_back(card);
        }
    }
    return out;
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

/** @brief The draw pile's contents component, or nullptr. */
ecs::PileContents* DrawPile(MatchInstance& engine) {
    for (ecs::Entity pile : engine.Store().EntitiesWith<ecs::PileContents>()) {
        ecs::PileContents* contents =
            engine.Store().Get<ecs::PileContents>(pile);
        if (contents != nullptr && contents->kind == ecs::PileKind::kDraw) {
            return contents;
        }
    }
    return nullptr;
}

/**
 * @brief Top the draw pile with a non-wild card and make it playable.
 *
 * Sets the active type to the card's colour so the next voluntary draw is
 * legal and parks a `PendingPlayDrawn` choice on the drawer.
 */
std::optional<ecs::Entity> ArmPlayableDraw(MatchInstance& engine) {
    ecs::PileContents* draw = DrawPile(engine);
    if (draw == nullptr) return std::nullopt;
    for (std::size_t i = 0; i < draw->cards.size(); ++i) {
        const ecs::FaceSpec* face =
            engine.Store().Get<ecs::FaceSpec>(draw->cards[i]);
        if (face == nullptr || face->color == "white") continue;
        std::swap(draw->cards[i], draw->cards.back());
        for (std::size_t j = 0; j < draw->cards.size(); ++j) {
            if (ecs::InZone* in =
                    engine.Store().Get<ecs::InZone>(draw->cards[j])) {
                in->ordinal = static_cast<uint32_t>(j);
            }
        }
        ecs::ActiveTypeReq* req =
            engine.Store().Get<ecs::ActiveTypeReq>(engine.Registries().match);
        REQUIRE(req != nullptr);
        req->type = face->color;
        return draw->cards.back();
    }
    return std::nullopt;
}

/** @brief True when `needle` appears as an integer anywhere in `value`. */
bool JsonHasNumber(const json& value, uint32_t needle) {
    if (value.is_number_integer()) {
        return value.get<int64_t>() == static_cast<int64_t>(needle);
    }
    if (value.is_array()) {
        for (const json& item : value) {
            if (JsonHasNumber(item, needle)) return true;
        }
        return false;
    }
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (JsonHasNumber(it.value(), needle)) return true;
        }
    }
    return false;
}

/** @brief OR `mask` into `viewer`'s grant entry on `target`. */
void AddGrant(MatchInstance& engine, ecs::Entity target, ecs::Entity viewer,
              uint32_t mask) {
    ecs::VisibilityGrant* grant =
        engine.Store().Get<ecs::VisibilityGrant>(target);
    if (grant == nullptr) {
        engine.Store().Add(target, ecs::VisibilityGrant{});
        grant = engine.Store().Get<ecs::VisibilityGrant>(target);
    }
    REQUIRE(grant != nullptr);
    ecs::VisibilityGrant::Entry entry;
    entry.viewer = viewer;
    entry.aspect_mask = mask;
    entry.expires_ms = 0;
    grant->entries.push_back(entry);
}

/** @brief Apply one status instance with an explicit hidden flag. */
void ApplyStatus(MatchInstance& engine, ecs::Entity entity,
                 const std::string& status_id, bool hidden) {
    match::status::ApplyRequest request;
    request.status_id = status_id;
    request.magnitude = 2;
    request.has_hidden = true;
    request.hidden = hidden;
    const match::status::ApplyResult result =
        match::status::Apply(engine.Store(), entity, request);
    REQUIRE(result.applied);
}

const json* FindPlayerState(const json& match_state,
                            const std::string& username) {
    for (const json& player : match_state["players"]) {
        if (player.value("username", std::string()) == username) {
            return &player;
        }
    }
    return nullptr;
}

/** @brief The hand entry whose compact id is `bits`, or nullptr. */
const json* EntryWithBits(const json& hand, uint32_t bits) {
    for (const json& entry : hand) {
        if (entry.value("card", 0u) == bits) return &entry;
    }
    return nullptr;
}

/**
 * @brief Snapshot-scoped PlayEvaluator verdict for `player`/`card`.
 *
 * Independent oracle: one evaluator built from the match, routed exactly as
 * `BuildHand` must route (`CanRespond` while a window is open, else
 * `CanPlayInTurn`).
 */
bool EvaluatorVerdict(const MatchInstance& engine, ecs::Entity player,
                      ecs::Entity card) {
    const PlayEvaluator evaluator = engine.MakePlayEvaluator();
    if (!engine.WindowOpen()) return evaluator.CanPlayInTurn(player, card);
    const ecs::WindowState* window =
        engine.Store().Get<ecs::WindowState>(engine.Registries().match);
    const auto filters = engine.WindowFilters();
    REQUIRE(window != nullptr);
    REQUIRE_FALSE(filters.empty());
    const PlayEvaluator::WindowView view{window->responders, window->responses,
                                         filters};
    return evaluator.CanRespond(view, player, card);
}

}  // namespace

TEST_CASE("view snapshot: base visibility for a seated player") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player0"), sink);
    REQUIRE(snapshot["action"] == "match_state_updated");
    const json& state = snapshot["match_state"];

    CHECK(state["status"] == "playing");
    CHECK(state["round"] == 0u);
    CHECK(state["direction"] == 1);
    CHECK(state["current_player"] == "player0");
    CHECK(state["winner"] == "");
    CHECK(state["placements"].is_array());
    CHECK(state["seq_watermark"] == 0u);
    REQUIRE(state["players"].size() == 4u);

    const json* own = FindPlayerState(state, "player0");
    REQUIRE(own != nullptr);
    REQUIRE(own->contains("hand"));
    CHECK((*own)["hand"].size() == 7u);
    CHECK((*own)["hand"][0].contains("card"));
    CHECK((*own)["hand"][0].contains("kind"));
    CHECK((*own)["hand"][0].contains("can_play"));
    CHECK((*own)["hand"][0]["can_play"].is_boolean());

    const json* other = FindPlayerState(state, "player1");
    REQUIRE(other != nullptr);
    CHECK((*other)["card_count"] == 7u);
    CHECK_FALSE(other->contains("hand"));

    CHECK(state["draw_pile"]["count"].get<std::size_t>() > 0u);
    CHECK_FALSE(state["draw_pile"].contains("cards"));
    CHECK(state["discard_pile"]["count"].get<std::size_t>() >= 1u);
    REQUIRE(state["discard_pile"].contains("top"));
    CHECK(state["discard_pile"]["top"].contains("card"));
    CHECK(state["discard_pile"]["top"].contains("kind"));
    CHECK(state["window"].is_null());
    CHECK(state["prompts"].is_array());
    CHECK(state["prompts"].empty());
}

TEST_CASE("view snapshot: pending_draws mirrors the draw-stacking debt") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);
    EventSink sink;

    CHECK(builder.BuildSnapshot(Viewer::Player("player0"), sink)
              ["match_state"]["pending_draws"]
          == 0);

    // INFO: a hidden debt still counts; the "+N" is public table state.
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ApplyStatus(*engine, player1, "vanilla:draw_debt", true);
    CHECK(builder.BuildSnapshot(Viewer::Player("player0"), sink)
              ["match_state"]["pending_draws"]
          == 2);
}

TEST_CASE("view snapshot: pending_play_drawn reveals the drawn card to the owner only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);
    EventSink sink;

    // INFO: no parked choice -> explicit null, same shape every viewer.
    CHECK(builder.BuildSnapshot(Viewer::Player("player0"), sink)
              ["match_state"]["pending_play_drawn"]
              .is_null());

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());
    const uint32_t drawn_bits = engine->Registries().CardId(*drawn)->bits;

    // OWNER: names the player and carries the compact card bits.
    const json owner =
        builder.BuildSnapshot(Viewer::Player("player0"), sink);
    const json& owner_choice =
        owner["match_state"]["pending_play_drawn"];
    REQUIRE(owner_choice.is_object());
    CHECK(owner_choice["player"] == "player0");
    REQUIRE(owner_choice.contains("card"));
    CHECK(owner_choice["card"] == drawn_bits);

    // OPPONENT: names the choosing player, never the card; the drawn identity
    // appears nowhere else in the opponent's JSON.
    const json opponent =
        builder.BuildSnapshot(Viewer::Player("player1"), sink);
    const json& opp_choice =
        opponent["match_state"]["pending_play_drawn"];
    REQUIRE(opp_choice.is_object());
    CHECK(opp_choice["player"] == "player0");
    CHECK_FALSE(opp_choice.contains("card"));
    CHECK_FALSE(JsonHasNumber(opponent["match_state"], drawn_bits));

    // SPECTATOR (default, omniscient): the field names the choosing player
    // but never carries the card. A default spectator can still see the drawn
    // identity through the pre-existing omniscient hand rule, so the field must
    // not add a second copy of it.
    const json spectator =
        builder.BuildSnapshot(Viewer::Spectator(), sink);
    const json& spec_choice =
        spectator["match_state"]["pending_play_drawn"];
    REQUIRE(spec_choice.is_object());
    CHECK(spec_choice["player"] == "player0");
    CHECK_FALSE(spec_choice.contains("card"));
    CHECK_FALSE(JsonHasNumber(spec_choice, drawn_bits));

    // SPECTATOR with the drawer's hand private: with the hand hidden, the drawn
    // identity appears NOWHERE in the JSON - the new field leaks nothing.
    SnapshotOptions private_options;
    private_options.privacy_from_spectators = {"player0"};
    const json private_spec = builder.BuildSnapshot(
        Viewer::Spectator(), sink, private_options);
    const json& private_choice =
        private_spec["match_state"]["pending_play_drawn"];
    REQUIRE(private_choice.is_object());
    CHECK(private_choice["player"] == "player0");
    CHECK_FALSE(private_choice.contains("card"));
    CHECK_FALSE(JsonHasNumber(private_spec["match_state"], drawn_bits));
}

TEST_CASE("view snapshot: no other hand identity without a grant") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player0"), sink);
    const json& state = snapshot["match_state"];

    // INFO: every non-owner hand is count-only and the draw pile leaks no
    //       identity.
    for (const json& player : state["players"]) {
        if (player["username"] == "player0") continue;
        CHECK_FALSE(player.contains("hand"));
        CHECK_FALSE(player.contains("card"));
    }
    CHECK_FALSE(state["draw_pile"].contains("cards"));
}

TEST_CASE("view snapshot: a hand grant reveals the granted aspects") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");
    AddGrant(*engine, player1, player0,
             static_cast<uint32_t>(ecs::Aspect::kIdentity));

    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player0"), sink);
    const json& state = snapshot["match_state"];

    const json* granted = FindPlayerState(state, "player1");
    REQUIRE(granted != nullptr);
    REQUIRE(granted->contains("hand"));
    CHECK((*granted)["hand"].size() == 7u);
    CHECK((*granted)["hand"][0].contains("card"));
    CHECK((*granted)["hand"][0].contains("kind"));
    // INFO: colour/value/position were not granted.
    CHECK_FALSE((*granted)["hand"][0].contains("color"));
    CHECK_FALSE((*granted)["hand"][0].contains("value"));
    CHECK_FALSE((*granted)["hand"][0].contains("slot"));
    CHECK_FALSE((*granted)["hand"][0].contains("can_play"));

    // INFO: an unrelated viewer still sees no identity.
    const json* ungranted = FindPlayerState(state, "player2");
    REQUIRE(ungranted != nullptr);
    CHECK_FALSE(ungranted->contains("hand"));

    EventSink other_sink;
    const json other =
        builder.BuildSnapshot(Viewer::Player("player2"), other_sink);
    const json* still_hidden =
        FindPlayerState(other["match_state"], "player1");
    REQUIRE(still_hidden != nullptr);
    CHECK_FALSE(still_hidden->contains("hand"));
}

TEST_CASE("view snapshot: a per-card grant reveals only that card") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Hand* hand = engine->Store().Get<ecs::Hand>(player1);
    REQUIRE(hand != nullptr);
    REQUIRE(hand->cards.size() == 7u);
    const ecs::Entity revealed = hand->cards[3];
    const uint32_t bits = engine->Registries().CardId(revealed)->bits;
    AddGrant(*engine, revealed, player0,
             static_cast<uint32_t>(ecs::Aspect::kIdentity));

    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player0"), sink);
    const json* granted =
        FindPlayerState(snapshot["match_state"], "player1");
    REQUIRE(granted != nullptr);
    REQUIRE(granted->contains("hand"));
    REQUIRE((*granted)["hand"].size() == 1u);
    CHECK((*granted)["hand"][0]["card"] == bits);
}

TEST_CASE("view snapshot: spectator is omniscient") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ApplyStatus(*engine, player1, "vanilla:draw_debt", true);

    EventSink sink;
    const json snapshot = builder.BuildSnapshot(Viewer::Spectator(), sink);
    const json& state = snapshot["match_state"];

    const json* seen = FindPlayerState(state, "player1");
    REQUIRE(seen != nullptr);
    REQUIRE(seen->contains("hand"));
    CHECK((*seen)["hand"].size() == 7u);
    CHECK((*seen)["hand"][0].contains("card"));

    bool hidden_seen = false;
    for (const json& status : (*seen)["statuses"]) {
        if (status["status_kind"] == "vanilla:draw_debt") hidden_seen = true;
    }
    CHECK(hidden_seen);
    CHECK(state["draw_pile"].contains("cards"));
}

TEST_CASE("view snapshot: privacy_from_spectators strips that player only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ApplyStatus(*engine, player0, "vanilla:draw_debt", true);
    ApplyStatus(*engine, player1, "vanilla:draw_debt", true);

    SnapshotOptions options;
    options.privacy_from_spectators = {"player1"};
    CHECK(options.PrivacyOn("player1"));
    CHECK_FALSE(options.PrivacyOn("player0"));

    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Spectator(), sink, options);
    const json& state = snapshot["match_state"];

    const json* private_player = FindPlayerState(state, "player1");
    REQUIRE(private_player != nullptr);
    CHECK_FALSE(private_player->contains("hand"));
    for (const json& status : (*private_player)["statuses"]) {
        CHECK(status["status_kind"] != "vanilla:draw_debt");
    }

    const json* public_player = FindPlayerState(state, "player0");
    REQUIRE(public_player != nullptr);
    REQUIRE(public_player->contains("hand"));
    CHECK((*public_player)["hand"].size() == 7u);
    bool hidden_seen = false;
    for (const json& status : (*public_player)["statuses"]) {
        if (status["status_kind"] == "vanilla:draw_debt") hidden_seen = true;
    }
    CHECK(hidden_seen);
}

TEST_CASE("view snapshot: hidden statuses are owner-only for players") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ApplyStatus(*engine, player1, "testmod:secret", true);
    ApplyStatus(*engine, player1, "vanilla:draw_debt", false);

    EventSink other_sink;
    const json other =
        builder.BuildSnapshot(Viewer::Player("player0"), other_sink);
    const json* seen_other =
        FindPlayerState(other["match_state"], "player1");
    REQUIRE(seen_other != nullptr);
    // INFO: the hidden instance is dropped; the public one remains.
    REQUIRE((*seen_other)["statuses"].size() == 1u);
    CHECK((*seen_other)["statuses"][0]["status_kind"] == "vanilla:draw_debt");

    EventSink own_sink;
    const json own =
        builder.BuildSnapshot(Viewer::Player("player1"), own_sink);
    const json* seen_own = FindPlayerState(own["match_state"], "player1");
    REQUIRE(seen_own != nullptr);
    CHECK((*seen_own)["statuses"].size() == 2u);
}

TEST_CASE("view snapshot: open prompt reaches only its target") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    REQUIRE_FALSE(wilds.empty());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ForceHand(*engine, player0, {wilds[0]});
    REQUIRE(engine->PlayCard("player0", wilds[0]));
    REQUIRE(engine->PendingInput().has_value());

    EventSink target_sink;
    const json target =
        builder.BuildSnapshot(Viewer::Player("player0"), target_sink);
    REQUIRE(target["match_state"]["prompts"].size() == 1u);
    CHECK(target["match_state"]["prompts"][0]["kind"] == "choose_color");

    EventSink other_sink;
    const json other =
        builder.BuildSnapshot(Viewer::Player("player1"), other_sink);
    CHECK(other["match_state"]["prompts"].empty());

    EventSink spec_sink;
    const json spectator = builder.BuildSnapshot(Viewer::Spectator(),
                                                 spec_sink);
    CHECK(spectator["match_state"]["prompts"].empty());
}

TEST_CASE("view snapshot: window state is included when open") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeWindowEngine(content);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    REQUIRE(draw2.has_value());
    ForceHand(*engine, player0, {*draw2});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)
        ->type = "red";
    REQUIRE(engine->PlayCard("player0", *draw2));
    REQUIRE(engine->WindowOpen());

    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player2"), sink);
    const json& window = snapshot["match_state"]["window"];
    REQUIRE_FALSE(window.is_null());
    CHECK(window["window_id"]
          == std::to_string(engine->ExportWindow().value("id", 0)));
    CHECK(window["responders"].size() == 1u);
    CHECK(window["eligible_filter_digest"].is_string());
    CHECK(window["kind"] == "generic");
    CHECK(window["responses"].is_array());
    CHECK(window["responses"].empty());
}

TEST_CASE("view snapshot: own-hand can_play equals PlayEvaluator verdicts") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> red2 = FindCard(*engine, "red", "+2");
    const std::optional<ecs::Entity> blue5 = FindCard(*engine, "blue", "5");
    REQUIRE(red2.has_value());
    REQUIRE(blue5.has_value());
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";
    ForceHand(*engine, player0, {*red2, *blue5});
    REQUIRE(*engine->GetCurrentPlayer() == player0);

    // INFO: in turn, no window - every own-hand entry carries the evaluator's
    //       CanPlayInTurn verdict; the red +2 matches the active type, the
    //       blue 5 does not.
    EventSink in_turn_sink;
    const json in_turn =
        builder.BuildSnapshot(Viewer::Player("player0"), in_turn_sink);
    const json* own = FindPlayerState(in_turn["match_state"], "player0");
    REQUIRE(own != nullptr);
    REQUIRE(own->contains("hand"));
    REQUIRE((*own)["hand"].size() == 2u);
    const ecs::Hand* hand0 = engine->Store().Get<ecs::Hand>(player0);
    REQUIRE(hand0->cards.size() == 2u);
    for (const json& entry : (*own)["hand"]) {
        const ecs::Entity card = entry["card"] ==
                                         engine->Registries().CardId(*red2)->bits
                                     ? *red2
                                     : *blue5;
        CHECK(entry["can_play"].get<bool>()
              == EvaluatorVerdict(*engine, player0, card));
    }
    CHECK(EntryWithBits((*own)["hand"], engine->Registries().CardId(*red2)->bits)
              ->at("can_play")
          == true);
    CHECK(EntryWithBits((*own)["hand"], engine->Registries().CardId(*blue5)->bits)
              ->at("can_play")
          == false);

    // INFO: out of turn, no window - the verdict is false and still equals the
    //       evaluator.
    ForceHand(*engine, player1, {*red2});
    EventSink out_sink;
    const json out =
        builder.BuildSnapshot(Viewer::Player("player1"), out_sink);
    const json* other = FindPlayerState(out["match_state"], "player1");
    REQUIRE(other != nullptr);
    REQUIRE(other->contains("hand"));
    REQUIRE((*other)["hand"].size() == 1u);
    CHECK((*other)["hand"][0]["can_play"] == false);
    CHECK_FALSE(EvaluatorVerdict(*engine, player1, *red2));
}

TEST_CASE("view snapshot: own-hand can_play uses CanRespond in a window") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeWindowEngine(content);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<ecs::Entity> opener = FindCard(*engine, "red", "+2");
    const std::optional<ecs::Entity> filler0 = FindCard(*engine, "blue", "5");
    const std::optional<ecs::Entity> stack = FindCard(*engine, "green", "+2");
    const std::optional<ecs::Entity> filler1 = FindCard(*engine, "blue", "6");
    REQUIRE(opener.has_value());
    REQUIRE(filler0.has_value());
    REQUIRE(stack.has_value());
    REQUIRE(filler1.has_value());
    ForceHand(*engine, player0, {*opener, *filler0});
    ForceHand(*engine, player1, {*stack, *filler1});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";
    REQUIRE(engine->PlayCard("player0", *opener));
    REQUIRE(engine->WindowOpen());

    // INFO: the responder's own hand is lit only for the +2 the window
    //       accepts; the verdict comes from the evaluator's CanRespond.
    EventSink responder_sink;
    const json responder =
        builder.BuildSnapshot(Viewer::Player("player1"), responder_sink);
    const json* row1 = FindPlayerState(responder["match_state"], "player1");
    REQUIRE(row1 != nullptr);
    REQUIRE(row1->contains("hand"));
    REQUIRE((*row1)["hand"].size() == 2u);
    CHECK(EntryWithBits((*row1)["hand"], engine->Registries().CardId(*stack)->bits)
              ->at("can_play")
          == true);
    CHECK(EntryWithBits((*row1)["hand"],
                        engine->Registries().CardId(*filler1)->bits)
              ->at("can_play")
          == false);
    CHECK(EvaluatorVerdict(*engine, player1, *stack));
    CHECK_FALSE(EvaluatorVerdict(*engine, player1, *filler1));

    // INFO: the actor opened the window but is not a responder, so its own hand
    //       is dimmed during the window.
    EventSink actor_sink;
    const json actor =
        builder.BuildSnapshot(Viewer::Player("player0"), actor_sink);
    const json* row0 = FindPlayerState(actor["match_state"], "player0");
    REQUIRE(row0 != nullptr);
    REQUIRE(row0->contains("hand"));
    for (const json& entry : (*row0)["hand"]) {
        CHECK(entry["can_play"] == false);
        const ecs::Entity card =
            entry["card"] == engine->Registries().CardId(*opener)->bits
                ? *opener
                : *filler0;
        CHECK_FALSE(EvaluatorVerdict(*engine, player0, card));
    }

    // INFO: spectators and other seats' hands never carry can_play.
    EventSink spec_sink;
    const json spectator =
        builder.BuildSnapshot(Viewer::Spectator(), spec_sink);
    for (const json& player : spectator["match_state"]["players"]) {
        REQUIRE(player.contains("hand"));
        for (const json& entry : player["hand"]) {
            CHECK_FALSE(entry.contains("can_play"));
        }
    }
}

TEST_CASE("view snapshot: seq watermark tracks the viewer stream") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    EventSink sink;
    const json first =
        json{{"type", "round_advance"}, {"payload", json{{"round", 1}}}};
    const json second =
        json{{"type", "placement"}, {"payload", json{{"player", "player0"},
                                                     {"place", 1}}}};
    REQUIRE(builder.Wrap(first, Viewer::Player("player0"), sink).has_value());
    REQUIRE(builder.Wrap(second, Viewer::Player("player0"), sink)
                .has_value());

    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player0"), sink);
    CHECK(snapshot["match_state"]["seq_watermark"] == sink.NextSeq());
    CHECK(sink.NextSeq() == 2u);
}

namespace {

/** @brief A card matching `color` + `label` other than `excluded`. */
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

constexpr int64_t kGroupWindowMs = 7000;
constexpr int64_t kGroupHoldMs = 800;

/** @brief Three seats with a jump_in + draw_stacking group just opened. */
std::unique_ptr<MatchInstance> OpenMergedGroup(
    Content& content, const std::vector<std::string>& mods,
    FakeClock& clock) {
    DeckDef deck = content.classic;
    deck.mods = mods;
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 3; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    std::unique_ptr<MatchInstance> engine = std::make_unique<MatchInstance>(
        std::move(result.assembly), FixedWindow(kGroupWindowMs), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    REQUIRE(draw2.has_value());
    const std::optional<ecs::Entity> twin =
        FindOtherCard(*engine, "red", "+2", *draw2);
    REQUIRE(twin.has_value());
    ForceHand(*engine, player0, {*draw2});
    ForceHand(*engine, *engine->FindPlayer("player2"), {*twin});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";
    REQUIRE(engine->PlayCard("player0", *draw2));
    REQUIRE(engine->WindowOpen());
    return engine;
}

/** @brief The first packet of `type` a viewer's stream carries, or null. */
json PacketOfType(const ViewBuilder& builder, const Viewer& viewer,
                  const std::string& type) {
    for (const json& packet : builder.BuildPackets(viewer)) {
        if (packet.value("type", std::string()) == type) return packet;
    }
    return json(nullptr);
}

const std::vector<std::string> kGroupMods = {"vanilla", "jump_in",
                                             "draw_stacking"};

}  // namespace

TEST_CASE("view window: a merged group exposes kinds and hold live") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        OpenMergedGroup(content, kGroupMods, clock);
    ViewBuilder builder(*engine, content.mods);

    const json open =
        PacketOfType(builder, Viewer::Player("player0"), "window_open");
    REQUIRE_FALSE(open.is_null());
    const json& payload = open["payload"];
    REQUIRE(payload["kinds"].is_array());
    REQUIRE(payload["kinds"].size() == 2u);
    CHECK(payload["kind"] == payload["kinds"][0]);
    CHECK(payload["kinds"] == json::array({"jump_in", "generic"}));
    CHECK(payload["hold_ms"] == kGroupHoldMs);
    CHECK(payload["duration_ms"] == kGroupWindowMs);
    CHECK(payload["deadline_ms"] == kGroupWindowMs);
}

TEST_CASE("view window: kinds follow member order, kind stays the first") {
    Content content;
    REQUIRE(LoadContent(content));
    struct Order {
        std::vector<std::string> mods;
        json kinds;
    };
    const std::vector<Order> orders = {
        {{"vanilla", "jump_in", "draw_stacking"},
         json::array({"jump_in", "generic"})},
        {{"vanilla", "draw_stacking", "jump_in"},
         json::array({"generic", "jump_in"})}};
    for (const Order& order : orders) {
        FakeClock clock;
        std::unique_ptr<MatchInstance> engine =
            OpenMergedGroup(content, order.mods, clock);
        ViewBuilder builder(*engine, content.mods);
        const json open =
            PacketOfType(builder, Viewer::Player("player0"), "window_open");
        REQUIRE_FALSE(open.is_null());
        CHECK(open["payload"]["kinds"] == order.kinds);
        CHECK(open["payload"]["kind"] == order.kinds[0]);
        EventSink sink;
        CHECK(builder.BuildSnapshot(Viewer::Spectator(), sink)["match_state"]
                                  ["window"]["kinds"] == order.kinds);
    }
}

TEST_CASE("view window: viewers and spectators see the same group") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        OpenMergedGroup(content, kGroupMods, clock);
    ViewBuilder builder(*engine, content.mods);

    const json base =
        PacketOfType(builder, Viewer::Player("player0"), "window_open");
    REQUIRE_FALSE(base.is_null());
    for (const Viewer& viewer :
         {Viewer::Player("player1"), Viewer::Player("player2"),
          Viewer::Spectator()}) {
        CHECK(PacketOfType(builder, viewer, "window_open")["payload"]
              == base["payload"]);
        EventSink sink;
        const json window =
            builder.BuildSnapshot(viewer, sink)["match_state"]["window"];
        CHECK(window["kinds"] == base["payload"]["kinds"]);
        CHECK(window["hold_ms"] == base["payload"]["hold_ms"]);
    }
}

TEST_CASE("view window: the snapshot mid-group equals the live payload") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        OpenMergedGroup(content, kGroupMods, clock);
    ViewBuilder builder(*engine, content.mods);

    const json live =
        PacketOfType(builder, Viewer::Player("player1"), "window_open")
            ["payload"];
    clock.now = 300;
    EventSink sink;
    const json window = builder.BuildSnapshot(Viewer::Player("player1"), sink)
                            ["match_state"]["window"];
    REQUIRE_FALSE(window.is_null());
    for (const char* field :
         {"window_id", "kind", "kinds", "hold_ms", "duration_ms", "responders",
          "eligible_filter_digest"}) {
        CHECK_MESSAGE(window[field] == live[field], field);
    }
    // INFO: the snapshot deadline is absolute; the live one is remaining.
    CHECK(window["deadline_ms"] == kGroupWindowMs);
    CHECK(live["deadline_ms"] == kGroupWindowMs);
}

TEST_CASE("view window: a single generic window has no hold") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeWindowEngine(content);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    REQUIRE(draw2.has_value());
    ForceHand(*engine, player0, {*draw2});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";
    REQUIRE(engine->PlayCard("player0", *draw2));
    REQUIRE(engine->WindowOpen());

    const json open =
        PacketOfType(builder, Viewer::Spectator(), "window_open");
    REQUIRE_FALSE(open.is_null());
    CHECK(open["payload"]["kinds"] == json::array({"generic"}));
    CHECK_FALSE(open["payload"].contains("hold_ms"));
    EventSink sink;
    const json window = builder.BuildSnapshot(Viewer::Spectator(), sink)
                            ["match_state"]["window"];
    CHECK(window["kinds"] == json::array({"generic"}));
    CHECK_FALSE(window.contains("hold_ms"));
    CHECK(window["duration_ms"] == 1000);
}

TEST_CASE("view snapshot: server_now_ms carries the engine clock") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    clock.now = 4242;
    std::unique_ptr<MatchInstance> engine =
        OpenMergedGroup(content, kGroupMods, clock);
    ViewBuilder builder(*engine, content.mods);
    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player0"), sink);
    CHECK(snapshot["match_state"]["server_now_ms"] == 4242);
}

TEST_CASE("view snapshot: an armed prompt reports its clock length") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    clock.now = 1000;
    DeckDef deck = content.classic;
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 4; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    MatchInstance engine(std::move(result.assembly), FixedWindow(1000),
                         clock.Fn());
    ViewBuilder builder(engine, content.mods);

    const std::vector<ecs::Entity> wilds = CardsByKind(engine, "vanilla:wild");
    REQUIRE_FALSE(wilds.empty());
    ForceHand(engine, *engine.FindPlayer("player0"), {wilds[0]});
    REQUIRE(engine.PlayCard("player0", wilds[0]));
    REQUIRE(engine.PendingInput().has_value());
    engine.SyncClocks(15000);

    EventSink sink;
    const json prompt =
        builder.BuildSnapshot(Viewer::Player("player0"), sink)["match_state"]
                                                              ["prompts"][0];
    CHECK(prompt["deadline_ms"] == 16000);
    CHECK(prompt["duration_ms"] == 15000);
}

TEST_CASE("view prompt: prompt_open carries the enforced clock length") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    clock.now = 1000;
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 4; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    MatchInstance engine(std::move(result.assembly), FixedWindow(1000),
                         clock.Fn());
    ViewBuilder builder(engine, content.mods);

    // INFO: the controller arms the clocks at turn start, before any play.
    engine.SyncClocks(15000);
    const std::vector<ecs::Entity> wilds = CardsByKind(engine, "vanilla:wild");
    REQUIRE_FALSE(wilds.empty());
    ForceHand(engine, *engine.FindPlayer("player0"), {wilds[0]});
    REQUIRE(engine.PlayCard("player0", wilds[0]));
    REQUIRE(engine.PendingInput().has_value());

    EventSink sink;
    const std::optional<json> open =
        builder.BuildPendingPrompt(Viewer::Player("player0"), sink);
    REQUIRE(open.has_value());
    CHECK((*open)["payload"]["duration_ms"] == 15000);
    CHECK((*open)["payload"]["deadline_ms"] == 0);
}

TEST_CASE("view snapshot: prompt_wait exposes only the pending prompt clock") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    clock.now = 1000;
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int i = 0; i < 4; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    MatchInstance engine(std::move(result.assembly), FixedWindow(1000),
                         clock.Fn());
    ViewBuilder builder(engine, content.mods);

    EventSink idle_sink;
    const json idle =
        builder.BuildSnapshot(Viewer::Player("player1"), idle_sink);
    CHECK_FALSE(idle["match_state"].contains("prompt_wait"));

    const std::vector<ecs::Entity> wilds = CardsByKind(engine, "vanilla:wild");
    REQUIRE_FALSE(wilds.empty());
    ForceHand(engine, *engine.FindPlayer("player0"), {wilds[0]});
    REQUIRE(engine.PlayCard("player0", wilds[0]));
    REQUIRE(engine.PendingInput().has_value());
    engine.SyncClocks(15000);

    const json expected = {{"deadline_ms", 16000}, {"duration_ms", 15000}};
    const Viewer viewers[] = {Viewer::Player("player0"),
                              Viewer::Player("player1"),
                              Viewer::Spectator()};
    for (const Viewer& viewer : viewers) {
        EventSink sink;
        const json state =
            builder.BuildSnapshot(viewer, sink)["match_state"];
        REQUIRE(state.contains("prompt_wait"));
        CHECK(state["prompt_wait"] == expected);
    }

    EventSink other_sink;
    const json other =
        builder.BuildSnapshot(Viewer::Player("player1"), other_sink);
    CHECK(other["match_state"]["prompts"].empty());
}
