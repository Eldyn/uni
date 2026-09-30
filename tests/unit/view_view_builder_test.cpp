#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file view_view_builder_test.cpp
 * @brief Per-recipient filtering tests.
 *
 * One positive and one negative assertion per visibility rule:
 * a viewer that is not the target / owner / grantee / audience must never
 * receive the packet, and the owner-identity / hidden-status rules must not
 * leak another player's data.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;
using match::view::EventSink;
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

json EntityRef(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
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

/** @brief Wrap one synthetic descriptor with a throwaway sink. */
std::optional<json> WrapOne(const ViewBuilder& builder, const Viewer& viewer,
                            const json& event) {
    EventSink sink;
    return builder.Wrap(event, viewer, sink);
}

/** @brief The card entity whose compact id is `bits`, or nullopt. */
std::optional<ecs::Entity> FindByBits(const MatchInstance& engine,
                                      uint32_t bits) {
    for (ecs::Entity card : engine.Registries().cards) {
        const std::optional<ecs::CompactCardV2> id =
            engine.Registries().CardId(card);
        if (id.has_value() && id->bits == bits) return card;
    }
    return std::nullopt;
}

/** @brief A loaded-mod stub declaring a hidden status. */
LoadedMod HiddenStatusMod() {
    LoadedMod mod;
    mod.manifest.id = "testmod";
    mod.manifest.version = "1.0.0";
    StatusDef secret;
    secret.id = "secret";
    secret.namespace_id = "testmod";
    secret.status_id = "testmod:secret";
    secret.hidden = true;
    mod.statuses.push_back(secret);
    StatusDef open;
    open.id = "open";
    open.namespace_id = "testmod";
    open.status_id = "testmod:open";
    open.hidden = false;
    mod.statuses.push_back(open);
    return mod;
}

/** @brief A loaded-mod stub declaring signal audiences. */
LoadedMod SignalMod() {
    LoadedMod mod;
    mod.manifest.id = "signals";
    mod.manifest.version = "1.0.0";
    mod.manifest.signals = json::array(
        {json{{"name", "anim_all"}},
         json{{"name", "anim_players"}, {"audience", "players"}},
         json{{"name", "anim_spec"}, {"audience", "spectators"}}});
    return mod;
}

}  // namespace

TEST_CASE("view filter: play_rejected is target only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const json event =
        json{{"type", "play_rejected"},
             {"payload", json{{"player", "player0"},
                              {"reason_id", "not_your_turn"}}}};

    const std::optional<json> target =
        WrapOne(builder, Viewer::Player("player0"), event);
    REQUIRE(target.has_value());
    CHECK((*target)["payload"]["player"] == "player0");
    CHECK((*target)["payload"]["reason_id"] == "not_your_turn");

    CHECK_FALSE(
        WrapOne(builder, Viewer::Player("player1"), event).has_value());
    CHECK_FALSE(WrapOne(builder, Viewer::Spectator(), event).has_value());
}

TEST_CASE("view filter: cards_drawn identities are owner only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    REQUIRE(engine->Registries().cards.size() >= 2);
    const ecs::Entity card0 = engine->Registries().cards[0];
    const ecs::Entity card1 = engine->Registries().cards[1];
    const uint32_t bits0 = engine->Registries().CardId(card0)->bits;
    const uint32_t bits1 = engine->Registries().CardId(card1)->bits;

    const json event = json{
        {"type", "cards_drawn"},
        {"payload",
         json{{"player", EntityRef(player0)},
              {"count", 2},
              {"source", "draw_pile"},
              {"cards", json::array({EntityRef(card0), EntityRef(card1)})}}}};

    const std::optional<json> owner =
        WrapOne(builder, Viewer::Player("player0"), event);
    REQUIRE(owner.has_value());
    REQUIRE((*owner)["payload"]["cards"].is_array());
    CHECK((*owner)["payload"]["cards"].size() == 2);
    CHECK((*owner)["payload"]["cards"][0] == bits0);
    CHECK((*owner)["payload"]["cards"][1] == bits1);

    const std::optional<json> other =
        WrapOne(builder, Viewer::Player("player1"), event);
    REQUIRE(other.has_value());
    CHECK((*other)["payload"]["count"] == 2);
    CHECK((*other)["payload"]["source_pile"] == "draw_pile");
    CHECK_FALSE((*other)["payload"].contains("cards"));

    const std::optional<json> spectator =
        WrapOne(builder, Viewer::Spectator(), event);
    REQUIRE(spectator.has_value());
    CHECK_FALSE((*spectator)["payload"].contains("cards"));
}

TEST_CASE("view filter: hidden statuses are owner only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    std::vector<LoadedMod> mods = content.mods;
    mods.push_back(HiddenStatusMod());
    ViewBuilder builder(*engine, mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const json secret =
        json{{"type", "status_applied"},
             {"payload", json{{"target", EntityRef(player0)},
                              {"status_kind", "testmod:secret"},
                              {"magnitude", 3},
                              {"duration_unit", "turns"},
                              {"instance", 1}}}};

    const std::optional<json> owner =
        WrapOne(builder, Viewer::Player("player0"), secret);
    REQUIRE(owner.has_value());
    CHECK((*owner)["payload"]["status_kind"] == "testmod:secret");
    CHECK((*owner)["payload"]["instance_id"] == 1);
    CHECK((*owner)["payload"]["magnitude"] == 3);
    CHECK((*owner)["payload"]["duration_unit"] == "turns");

    CHECK_FALSE(
        WrapOne(builder, Viewer::Player("player1"), secret).has_value());
    CHECK_FALSE(WrapOne(builder, Viewer::Spectator(), secret).has_value());

    const json open =
        json{{"type", "status_applied"},
             {"payload", json{{"target", EntityRef(player0)},
                              {"status_kind", "testmod:open"},
                              {"magnitude", 1},
                              {"duration_unit", "turns"},
                              {"instance", 2}}}};
    CHECK(WrapOne(builder, Viewer::Player("player1"), open).has_value());

    const json removed =
        json{{"type", "status_removed"},
             {"payload", json{{"target", EntityRef(player0)},
                              {"status_kind", "testmod:secret"},
                              {"instance", 1}}}};
    CHECK(WrapOne(builder, Viewer::Player("player0"), removed).has_value());
    CHECK_FALSE(
        WrapOne(builder, Viewer::Player("player1"), removed).has_value());

    // INFO: the vanilla draw-debt status is declared public, so its removal
    //       is visible to everyone.
    const json public_removed =
        json{{"type", "status_removed"},
             {"payload", json{{"target", EntityRef(player0)},
                              {"status_kind", "vanilla:draw_debt"},
                              {"instance", 9}}}};
    CHECK(WrapOne(builder, Viewer::Player("player1"), public_removed)
              .has_value());
}

TEST_CASE("view filter: window_open projects kinds and hold_ms") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const json merged =
        json{{"type", "window_open"},
             {"payload", json{{"id", 3},
                              {"duration_ms", 7000},
                              {"hold_ms", 800},
                              {"kind", "jump_in"},
                              {"kinds", json::array({"jump_in", "generic"})}}}};
    const json out =
        (*WrapOne(builder, Viewer::Spectator(), merged))["payload"];
    CHECK(out["kinds"] == json::array({"jump_in", "generic"}));
    CHECK(out["kind"] == "jump_in");
    CHECK(out["hold_ms"] == 800);
    CHECK(out["duration_ms"] == 7000);

    const json bare = json{{"type", "window_open"},
                           {"payload", json{{"id", 4}, {"deadline_ms", 5}}}};
    const json plain =
        (*WrapOne(builder, Viewer::Spectator(), bare))["payload"];
    CHECK(plain["kinds"] == json::array({"generic"}));
    CHECK_FALSE(plain.contains("hold_ms"));
}

TEST_CASE("view filter: window packets are uniform and public") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const json open =
        json{{"type", "window_open"},
             {"payload", json{{"id", 7},
                              {"responders",
                               json::array({"player1", "player2"})},
                              {"filter_digest", "digest-abc"},
                              {"deadline_ms", 999999},
                              {"duration_ms", 7000},
                              {"kind", "jump_in"}}}};

    const std::optional<json> p0 =
        WrapOne(builder, Viewer::Player("player0"), open);
    const std::optional<json> p1 =
        WrapOne(builder, Viewer::Player("player1"), open);
    REQUIRE(p0.has_value());
    REQUIRE(p1.has_value());
    // INFO: no per-viewer difference - the filter digest is uniform (no leak).
    CHECK((*p0)["payload"] == (*p1)["payload"]);
    CHECK((*p0)["payload"]["eligible_filter_digest"] == "digest-abc");
    CHECK((*p0)["payload"]["window_id"] == "7");
    CHECK((*p0)["payload"]["deadline_ms"] == 7000);
    CHECK((*p0)["payload"]["kind"] == "jump_in");
    const json bare =
        json{{"type", "window_open"},
             {"payload", json{{"id", 8}, {"deadline_ms", 5}}}};
    CHECK((*WrapOne(builder, Viewer::Spectator(), bare))["payload"]["kind"]
          == "generic");
    CHECK((*p0)["payload"]["responders"].size() == 2);
    CHECK(WrapOne(builder, Viewer::Spectator(), open).has_value());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity card0 = engine->Registries().cards[0];
    const json response =
        json{{"type", "window_response"},
             {"payload", json{{"window", 7},
                              {"player", "player0"},
                              {"card", EntityRef(card0)},
                              {"pass", false},
                              {"outcome", "winning"}}}};
    const std::optional<json> seen_by_other =
        WrapOne(builder, Viewer::Player("player2"), response);
    REQUIRE(seen_by_other.has_value());
    CHECK((*seen_by_other)["payload"]["player"] == "player0");
    CHECK((*seen_by_other)["payload"]["card"]
          == engine->Registries().CardId(card0)->bits);

    const json pass =
        json{{"type", "window_response"},
             {"payload", json{{"window", 7},
                              {"player", "player1"},
                              {"pass", true},
                              {"outcome", "pass"}}}};
    const std::optional<json> pass_seen =
        WrapOne(builder, Viewer::Player("player0"), pass);
    REQUIRE(pass_seen.has_value());
    CHECK((*pass_seen)["payload"]["passed"] == true);

    const json close =
        json{{"type", "window_close"},
             {"payload", json{{"id", 7},
                              {"outcome", "response"},
                              {"winner", "player0"},
                              {"card", EntityRef(card0)}}}};
    const std::optional<json> close_seen =
        WrapOne(builder, Viewer::Player("player3"), close);
    REQUIRE(close_seen.has_value());
    CHECK((*close_seen)["payload"]["outcome"] == "winner");
    CHECK((*close_seen)["payload"]["winner"] == "player0");

    const json timeout =
        json{{"type", "window_close"},
             {"payload", json{{"id", 7},
                              {"outcome", "timeout"},
                              {"winner", nullptr}}}};
    const std::optional<json> timeout_seen =
        WrapOne(builder, Viewer::Player("player1"), timeout);
    REQUIRE(timeout_seen.has_value());
    CHECK((*timeout_seen)["payload"]["outcome"] == "default");
    CHECK_FALSE((*timeout_seen)["payload"].contains("winner"));
    (void)player0;
}

TEST_CASE("view filter: auto_played is public") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity card0 = engine->Registries().cards[0];
    const json event =
        json{{"type", "auto_played"},
             {"payload", json{{"player", EntityRef(player0)},
                              {"card", EntityRef(card0)},
                              {"trigger", "draw"}}}};

    const std::optional<json> seen =
        WrapOne(builder, Viewer::Player("player2"), event);
    REQUIRE(seen.has_value());
    CHECK((*seen)["payload"]["player"] == "player0");
    CHECK((*seen)["payload"]["card"]
          == engine->Registries().CardId(card0)->bits);
    CHECK((*seen)["payload"]["trigger_summary"] == "draw");
    CHECK(WrapOne(builder, Viewer::Spectator(), event).has_value());
}

TEST_CASE("view filter: prompt packets are target only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const json open =
        json{{"type", "prompt_open"},
             {"payload", json{{"prompt_id", "p1"},
                              {"kind", "choose_color"},
                              {"target", EntityRef(player0)},
                              {"payload", json{{"options", json::array()}}},
                              {"response_schema", json::object()},
                              {"deadline_ms", 15000}}}};

    const std::optional<json> target =
        WrapOne(builder, Viewer::Player("player0"), open);
    REQUIRE(target.has_value());
    CHECK((*target)["payload"]["kind"] == "choose_color");
    CHECK((*target)["payload"]["deadline_ms"] == 15000);

    CHECK_FALSE(
        WrapOne(builder, Viewer::Player("player1"), open).has_value());
    CHECK_FALSE(WrapOne(builder, Viewer::Spectator(), open).has_value());

    const json close =
        json{{"type", "prompt_close"},
             {"payload", json{{"prompt_id", "p1"},
                              {"target", EntityRef(player0)},
                              {"outcome", "answered"}}}};
    CHECK(WrapOne(builder, Viewer::Player("player0"), close).has_value());
    CHECK_FALSE(
        WrapOne(builder, Viewer::Player("player1"), close).has_value());
}

TEST_CASE("view filter: visibility grants are viewer only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");

    const json granted =
        json{{"type", "visibility_granted"},
             {"payload", json{{"viewer", EntityRef(player0)},
                              {"target", EntityRef(player1)},
                              {"aspects", json::array({"identity"})}}}};
    const std::optional<json> grantee =
        WrapOne(builder, Viewer::Player("player0"), granted);
    REQUIRE(grantee.has_value());
    CHECK((*grantee)["payload"]["target"] == "player1");
    CHECK((*grantee)["payload"]["aspects"].size() == 1);

    CHECK_FALSE(
        WrapOne(builder, Viewer::Player("player1"), granted).has_value());
    CHECK_FALSE(
        WrapOne(builder, Viewer::Player("player2"), granted).has_value());
    CHECK_FALSE(
        WrapOne(builder, Viewer::Spectator(), granted).has_value());

    const json revoked =
        json{{"type", "visibility_revoked"},
             {"payload", json{{"viewer", EntityRef(player1)},
                              {"target", EntityRef(player0)},
                              {"aspects", json::array({"identity"})}}}};
    CHECK(WrapOne(builder, Viewer::Player("player1"), revoked).has_value());
    CHECK_FALSE(
        WrapOne(builder, Viewer::Player("player0"), revoked).has_value());
}

TEST_CASE("view filter: signal follows the manifest audience") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    std::vector<LoadedMod> mods = content.mods;
    mods.push_back(SignalMod());
    ViewBuilder builder(*engine, mods);

    auto signal = [](const std::string& name) {
        return json{{"type", "signal"},
                    {"payload", json{{"name", name},
                                     {"payload", json{{"x", 1}}}}}};
    };

    // INFO: undeclared -> default `all`.
    CHECK(builder.SignalAudience("force_play:not_playable") == "all");
    CHECK(WrapOne(builder, Viewer::Player("player1"),
                  signal("force_play:not_playable"))
              .has_value());
    CHECK(WrapOne(builder, Viewer::Spectator(),
                  signal("force_play:not_playable"))
              .has_value());

    // INFO: audience `players` excludes spectators.
    CHECK(builder.SignalAudience("anim_players") == "players");
    CHECK(WrapOne(builder, Viewer::Player("player1"),
                  signal("anim_players"))
              .has_value());
    CHECK_FALSE(WrapOne(builder, Viewer::Spectator(),
                        signal("anim_players"))
                    .has_value());

    // INFO: audience `spectators` excludes seated players.
    CHECK(WrapOne(builder, Viewer::Spectator(), signal("anim_spec"))
              .has_value());
    CHECK_FALSE(WrapOne(builder, Viewer::Player("player1"),
                        signal("anim_spec"))
                    .has_value());
    CHECK(WrapOne(builder, Viewer::Player("player1"), signal("anim_all"))
              .has_value());
}

TEST_CASE("view filter: seq stays gap-free for a filtered viewer") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    std::vector<LoadedMod> mods = content.mods;
    mods.push_back(HiddenStatusMod());
    ViewBuilder builder(*engine, mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const json log = json::array(
        {json{{"type", "play_rejected"},
              {"payload", json{{"player", "player0"},
                               {"reason_id", "not_your_turn"}}}},
         json{{"type", "cards_drawn"},
              {"payload", json{{"player", "player0"},
                               {"count", 1},
                               {"source", "draw_pile"}}}},
         json{{"type", "status_applied"},
              {"payload", json{{"target", EntityRef(player0)},
                               {"status_kind", "testmod:secret"},
                               {"magnitude", 1},
                               {"instance", 1}}}},
         json{{"type", "round_advance"},
              {"payload", json{{"round", 1}}}}});

    EventSink sink;
    std::vector<json> visible;
    for (const json& event : log) {
        const std::optional<json> wrapped =
            builder.Wrap(event, Viewer::Player("player1"), sink);
        if (wrapped.has_value()) visible.push_back(*wrapped);
    }

    // INFO: play_rejected (target player0) and the hidden status are dropped
    //       for player1; cards_drawn and round_advance remain, gap-free.
    REQUIRE(visible.size() == 2);
    CHECK(visible[0]["type"] == "cards_drawn");
    CHECK(visible[1]["type"] == "round_advance");
    CHECK(visible[0]["seq"] == 0u);
    CHECK(visible[1]["seq"] == 1u);
    CHECK(sink.NextSeq() == 2u);
}

TEST_CASE("view filter: can_play is own hand only and matches the evaluator") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);

    const ecs::Entity player1 = *engine->FindPlayer("player1");
    REQUIRE(*engine->GetCurrentPlayer() != player1);

    // INFO: a viewer's own hand carries a can_play verdict per card equal to
    //       the snapshot evaluator's CanPlayInTurn; every other hand is
    //       count-only (no grant), so it carries none.
    EventSink own_sink;
    const json own = builder.BuildSnapshot(Viewer::Player("player1"), own_sink);
    const PlayEvaluator evaluator = engine->MakePlayEvaluator();
    bool own_hand_seen = false;
    for (const json& player : own["match_state"]["players"]) {
        const std::string username = player.value("username", std::string());
        if (username != "player1") {
            CHECK_FALSE(player.contains("hand"));
            continue;
        }
        own_hand_seen = true;
        REQUIRE(player.contains("hand"));
        for (const json& entry : player["hand"]) {
            REQUIRE(entry.contains("can_play"));
            const std::optional<ecs::Entity> card =
                FindByBits(*engine, entry["card"].get<uint32_t>());
            REQUIRE(card.has_value());
            CHECK(entry["can_play"].get<bool>()
                  == evaluator.CanPlayInTurn(player1, *card));
        }
    }
    CHECK(own_hand_seen);

    // INFO: a spectator sees every hand in full but never a can_play flag.
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

TEST_CASE("view filter: pending prompt reaches only its target") {
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
    const std::optional<json> target =
        builder.BuildPendingPrompt(Viewer::Player("player0"), target_sink);
    REQUIRE(target.has_value());
    CHECK((*target)["type"] == "prompt_open");
    CHECK((*target)["payload"]["kind"] == "choose_color");
    CHECK(target_sink.NextSeq() == 1u);

    EventSink other_sink;
    CHECK_FALSE(
        builder.BuildPendingPrompt(Viewer::Player("player1"), other_sink)
            .has_value());
    CHECK_FALSE(
        builder.BuildPendingPrompt(Viewer::Spectator(), other_sink)
            .has_value());
    CHECK(other_sink.NextSeq() == 0u);
}
