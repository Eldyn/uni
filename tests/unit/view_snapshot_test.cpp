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
    CHECK(window["responses"].is_array());
    CHECK(window["responses"].empty());
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
