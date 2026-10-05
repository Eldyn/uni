#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
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
 * @file engine_causal_frame_test.cpp
 * @brief Turn counter and causal frame on engine events.
 *
 * Every engine event carries `causal_frame`: the `actor` username (null for
 * the table), the `cause` label, and the `turn` and `round` it belongs to.
 * The cases cover a play, a rule chain, a turn timer expiry, a window timeout
 * and a bot move.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;

namespace {

using nlohmann::json;

constexpr int64_t kTurnLimitMs = 15'000;
constexpr int64_t kWindowMs = 1'000;

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

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

Content LoadContent() {
    Content content;
    LoadResult load = ScanModsDirectory((ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    content.mods = std::move(load.mods);
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") content.classic = deck;
        }
    }
    REQUIRE_FALSE(content.classic.deck_id.empty());
    content.classic.mods = {"uni"};
    return content;
}

std::unique_ptr<MatchAssembly> Assemble(Content& content, int players,
                                        const std::string& prefix) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < players; ++index) {
        MatchPlayerSpec spec;
        spec.username = prefix + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE(result.ok());
    return std::move(result.assembly);
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          match::WindowConfig config,
                                          match::NowMs clock) {
    return std::make_unique<MatchInstance>(
        Assemble(content, players, "player"), config, std::move(clock));
}

match::WindowConfig FixedWindow(int64_t window_ms) {
    match::WindowConfig config;
    config.window_ms = window_ms;
    config.mode = match::WindowMode::kFixed;
    return config;
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

std::optional<ecs::Entity> FindNumberedCard(MatchInstance& engine,
                                            const std::string& label) {
    const json active = engine.ExportState()["active_type"];
    if (!active.is_string()) return std::nullopt;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        const ecs::CardBehavior* behavior =
            engine.Store().Get<ecs::CardBehavior>(card);
        if (face == nullptr || face->label != label) continue;
        if (face->color != active.get<std::string>()) continue;
        if (behavior != nullptr && !behavior->triggers.empty()) continue;
        const ecs::PileContents* discard =
            engine.Store().Get<ecs::PileContents>(
                engine.Registries().discard_pile);
        if (discard != nullptr && !discard->cards.empty()
            && discard->cards.back() == card) {
            continue;
        }
        return card;
    }
    return std::nullopt;
}

const json* FindEvent(const std::vector<json>& events,
                      const std::string& type) {
    for (const json& event : events) {
        if (event.value("type", "") == type) return &event;
    }
    return nullptr;
}

const json* FindSignal(const std::vector<json>& events,
                       const std::string& name) {
    for (const json& event : events) {
        if (event.value("type", "") != "signal") continue;
        if (event["payload"].value("name", "") == name) return &event;
    }
    return nullptr;
}

uint32_t MatchTurn(MatchInstance& engine) {
    const ecs::MatchMeta* meta =
        engine.Store().Get<ecs::MatchMeta>(engine.Registries().match);
    REQUIRE(meta != nullptr);
    return meta->turn;
}

void AttachSystem(MatchInstance& engine, BehaviorGraph graph) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:system";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}

BehaviorGraph SignalGraph(const std::string& name) {
    BehaviorGraph graph;
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"op", "emit_signal"},
              {"args", json{{"name", name}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

BehaviorGraph WindowGraph() {
    BehaviorGraph graph;
    const json window = json{{"responders", "@others"},
                             {"respond_with", json::object()},
                             {"duration", "env"},
                             {"filter_digest", "any"}};
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"window", window},
              {"default", "n2"},
              {"on_response", json{{"any", "n2"}}}},
         json{{"id", "n2"},
              {"op", "emit_signal"},
              {"args", json{{"name", "window:default"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

class DrawingPolicy : public match::server::IBotPolicy {
public:
    std::optional<uint32_t> ChoosePlay(
        const match::server::BotView&) override {
        return std::nullopt;
    }
    json ChoosePrompt(const match::server::BotView&) override {
        return nullptr;
    }
    std::vector<uint32_t> ChooseWindowResponses(
        const match::server::BotView&) override {
        return {};
    }
};

}  // namespace

TEST_CASE("causal frame: the turn counter grows at each turn advance") {
    Content content = LoadContent();
    FakeClock clock;
    auto engine = MakeEngine(content, 2, match::WindowConfig{}, clock.Fn());
    CHECK(MatchTurn(*engine) == 0);

    engine->SyncClocks(kTurnLimitMs);
    clock.now = kTurnLimitMs + 1;
    engine->Tick();
    CHECK(MatchTurn(*engine) == 1);

    engine->SyncClocks(kTurnLimitMs);
    clock.now += kTurnLimitMs + 1;
    engine->Tick();
    CHECK(MatchTurn(*engine) == 2);
}

TEST_CASE("causal frame: a play stamps the player as actor") {
    Content content = LoadContent();
    FakeClock clock;
    auto engine = MakeEngine(content, 2, match::WindowConfig{}, clock.Fn());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> card = FindNumberedCard(*engine, "5");
    REQUIRE(card.has_value());
    ForceHand(*engine, player0, {*card});
    engine->TakeEvents();

    REQUIRE(engine->PlayCard("player0", *card));

    const std::vector<json> events = engine->Events();
    const json* played = FindEvent(events, "card_played");
    REQUIRE(played != nullptr);
    const json& played_frame = (*played)["causal_frame"];
    CHECK(played_frame["actor"] == "player0");
    CHECK(played_frame["cause"] == "play");
    CHECK(played_frame["turn"] == 0);
    CHECK(played_frame["round"] == 0);
}

TEST_CASE("causal frame: an op in a rule chain passes actor and cause") {
    Content content = LoadContent();
    FakeClock clock;
    auto engine = MakeEngine(content, 2, match::WindowConfig{}, clock.Fn());
    AttachSystem(*engine, SignalGraph("chain:signal"));
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> card = FindNumberedCard(*engine, "5");
    REQUIRE(card.has_value());
    ForceHand(*engine, player0, {*card});
    engine->TakeEvents();

    REQUIRE(engine->PlayCard("player0", *card));

    const std::vector<json> events = engine->Events();
    const json* signal = FindSignal(events, "chain:signal");
    REQUIRE(signal != nullptr);
    CHECK((*signal)["causal_frame"]["actor"] == "player0");
    CHECK((*signal)["causal_frame"]["cause"] == "play");
    CHECK((*signal)["causal_frame"]["turn"] == 0);
}

TEST_CASE("causal frame: a turn timer expiry names the lapsed seat") {
    Content content = LoadContent();
    FakeClock clock;
    auto engine = MakeEngine(content, 2, match::WindowConfig{}, clock.Fn());
    engine->SyncClocks(kTurnLimitMs);
    engine->TakeEvents();

    clock.now = kTurnLimitMs + 1;
    engine->Tick();

    const std::vector<json> events = engine->Events();
    const json* advance = FindEvent(events, "turn_advance");
    REQUIRE(advance != nullptr);
    const json& frame = (*advance)["causal_frame"];
    CHECK(frame["actor"] == "player0");
    CHECK(frame["cause"] == "turn_timer_expired");
    CHECK(frame["turn"] == 1);
    CHECK(MatchTurn(*engine) == 1);
}

TEST_CASE("causal frame: a window timeout has no seat actor") {
    Content content = LoadContent();
    FakeClock clock;
    auto engine =
        MakeEngine(content, 3, FixedWindow(kWindowMs), clock.Fn());
    AttachSystem(*engine, WindowGraph());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> card = FindNumberedCard(*engine, "5");
    REQUIRE(card.has_value());
    ForceHand(*engine, player0, {*card, *FindNumberedCard(*engine, "6")});
    REQUIRE(engine->PlayCard("player0", *card));
    REQUIRE(engine->WindowOpen());
    engine->TakeEvents();

    clock.now = kWindowMs;
    engine->Tick();

    const std::vector<json> events = engine->Events();
    const json* closed = FindEvent(events, "window_close");
    REQUIRE(closed != nullptr);
    CHECK((*closed)["causal_frame"]["actor"].is_null());
    CHECK((*closed)["causal_frame"]["cause"] == "window_timeout");
    const json* routed = FindSignal(events, "window:default");
    REQUIRE(routed != nullptr);
    CHECK((*routed)["causal_frame"]["actor"].is_null());
    CHECK((*routed)["causal_frame"]["cause"] == "window_timeout");
}

TEST_CASE("causal frame: a bot move names the bot seat as actor") {
    Content content = LoadContent();
    auto engine = std::make_unique<MatchInstance>(
        Assemble(content, 2, "bot"));
    MatchInstance& raw = *engine;
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        match::server::MatchSession::SocketMap{
            {"bot0", reinterpret_cast<AppWebSocket*>(0x200)},
            {"bot1", reinterpret_cast<AppWebSocket*>(0x201)}});
    raw.TakeEvents();

    DrawingPolicy policy;
    REQUIRE(match::server::BotStep(session, policy, "bot0"));

    const std::vector<json> events = raw.Events();
    const json* drawn = FindEvent(events, "cards_drawn");
    REQUIRE(drawn != nullptr);
    CHECK((*drawn)["causal_frame"]["actor"] == "bot0");
    CHECK((*drawn)["causal_frame"]["cause"] == "draw");
}

TEST_CASE("causal frame: each engine event has a growing event id") {
    Content content = LoadContent();
    FakeClock clock;
    auto engine = MakeEngine(content, 2, match::WindowConfig{}, clock.Fn());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> card = FindNumberedCard(*engine, "5");
    REQUIRE(card.has_value());
    ForceHand(*engine, player0, {*card});
    engine->TakeEvents();

    REQUIRE(engine->PlayCard("player0", *card));

    const std::vector<json> events = engine->Events();
    REQUIRE(events.size() > 1);
    int64_t previous_id = -1;
    for (const json& event : events) {
        REQUIRE(event.contains("event_id"));
        const int64_t event_id = event["event_id"].get<int64_t>();
        CHECK(event_id > previous_id);
        previous_id = event_id;
    }
}

TEST_CASE("causal frame: a rule chain event names the played card event") {
    Content content = LoadContent();
    FakeClock clock;
    auto engine = MakeEngine(content, 2, match::WindowConfig{}, clock.Fn());
    AttachSystem(*engine, SignalGraph("chain:signal"));
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> card = FindNumberedCard(*engine, "5");
    REQUIRE(card.has_value());
    ForceHand(*engine, player0, {*card});
    engine->TakeEvents();

    REQUIRE(engine->PlayCard("player0", *card));

    const std::vector<json> events = engine->Events();
    const json* played = FindEvent(events, "card_played");
    const json* signal = FindSignal(events, "chain:signal");
    REQUIRE(played != nullptr);
    REQUIRE(signal != nullptr);
    CHECK_FALSE((*played)["causal_frame"].contains("cause_event"));
    CHECK((*signal)["causal_frame"]["cause_event"] == (*played)["event_id"]);
}
