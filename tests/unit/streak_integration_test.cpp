#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/event_tail.hpp>
#include <match/view/streak_view.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/**
 * @file streak_integration_test.cpp
 * @brief Streaks through the whole engine: the evaluation pass, the context
 *        on the envelope, the tail replay, the hidden-field gate, and the
 *        cost and size limits.
 *
 * A system on the `draw_attempt` hook emits the mod event `test:apple_eaten`
 * once for each draw, so one draw by the current seat is one event.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

constexpr const char* kEventId = "test:apple_eaten";
constexpr const char* kSecretText = "hunter2_not_for_streaks";

struct EnvOverride {
    std::string key;
    EnvOverride(std::string name, const std::string& value)
        : key(std::move(name)) {
        Env::SetEnv(key, value);
    }
    ~EnvOverride() { Env::SetEnv(key, std::string()); }
};

struct ConsoleCapture {
    std::ostringstream text;
    std::streambuf* saved;
    ConsoleCapture() : saved(std::cout.rdbuf(text.rdbuf())) {}
    ~ConsoleCapture() { std::cout.rdbuf(saved); }
    bool Has(const std::string& needle) const {
        return text.str().find(needle) != std::string::npos;
    }
};

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        const fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

StreakDef AppleStreak(const std::string& id, const std::string& type) {
    StreakDef streak;
    streak.id = id;
    streak.namespace_id = "test";
    streak.streak_id = "test:" + id;
    streak.type = type;
    streak.matcher.event = kEventId;
    return streak;
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef deck;
};

/** @brief The shipped mods, with extra streaks added to the `test` mod. */
Content LoadContent(const std::vector<StreakDef>& streaks) {
    Content content;
    LoadResult load = ScanModsDirectory((ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    content.mods = std::move(load.mods);
    for (LoadedMod& mod : content.mods) {
        if (mod.manifest.id == "test") mod.streaks = streaks;
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "test:classic_bombs") content.deck = deck;
        }
    }
    REQUIRE_FALSE(content.deck.deck_id.empty());
    return content;
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < 3; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.deck, options);
    REQUIRE(result.ok());
    auto engine = std::make_unique<MatchInstance>(std::move(result.assembly));
    engine->Assembly().registries.events[kEventId].context = {
        "test:apple_total"};
    return engine;
}

void AttachAppleSystem(MatchInstance& engine) {
    BehaviorGraph graph;
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"op", "emit_event"},
              {"args",
               {{"id", kEventId},
                {"payload",
                 {{"target", "@self"},
                  {"color", "red"},
                  {"count", 1},
                  {"secret", kSecretText}}}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"draw_attempt", ecs::HookPhase::kAfter};
    system.source_id = "test:apple_system";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}

void DrawOnce(MatchInstance& engine) {
    const std::string name = engine.GetCurrentPlayerUsername();
    REQUIRE(engine.DrawCard(name));
    if (engine.PendingPlayDrawnState().has_value()) {
        REQUIRE(engine.KeepDrawn(name));
    }
}

std::vector<json> Packets(MatchInstance& engine, const Content& content,
                          const Viewer& viewer) {
    const ViewBuilder builder(engine, content.mods);
    return builder.BuildPackets(viewer);
}

std::vector<json> AppleEvents(const std::vector<json>& packets) {
    std::vector<json> found;
    for (const json& packet : packets) {
        if (packet.value("id", "") == kEventId) found.push_back(packet);
    }
    return found;
}

std::vector<Viewer> AllViewers() {
    return {Viewer::PublicSpectator(), Viewer::Spectator(),
            Viewer::Player("player0"), Viewer::Player("player1"),
            Viewer::Player("player2")};
}

}  // namespace

TEST_CASE("streak context: a mod event gets the value after the event") {
    Content content = LoadContent({AppleStreak("apple_total", "total")});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    AttachAppleSystem(*engine);
    DrawOnce(*engine);
    DrawOnce(*engine);
    DrawOnce(*engine);
    const std::vector<json> apples =
        AppleEvents(Packets(*engine, content, Viewer::PublicSpectator()));
    REQUIRE(apples.size() == 3);
    for (std::size_t index = 0; index < apples.size(); ++index) {
        REQUIRE(apples[index].contains("context"));
        CHECK(apples[index]["context"]
              == json{{"test:apple_total", index + 1}});
    }
    CHECK(engine->Streaks().ValueOf("test:apple_total") == 3);
}

TEST_CASE("streak context: every seat and spectator gets the same object") {
    Content content = LoadContent({AppleStreak("apple_total", "total")});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    AttachAppleSystem(*engine);
    DrawOnce(*engine);
    DrawOnce(*engine);
    const json reference =
        AppleEvents(Packets(*engine, content, Viewer::PublicSpectator()))[1]
                   ["context"];
    for (const Viewer& viewer : AllViewers()) {
        const std::vector<json> apples =
            AppleEvents(Packets(*engine, content, viewer));
        REQUIRE(apples.size() == 2);
        CHECK(apples[1]["context"] == reference);
    }
}

TEST_CASE("streak context: an event that asks for nothing has no context") {
    Content content = LoadContent({AppleStreak("apple_total", "total")});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    AttachAppleSystem(*engine);
    DrawOnce(*engine);
    for (const json& packet :
         Packets(*engine, content, Viewer::PublicSpectator())) {
        const std::string id = packet.value("id", "");
        if (id == kEventId) continue;
        // INFO: a core event that the table asks a context for has one.
        if (!match::view::CoreContextNames(id).empty()) continue;
        CHECK_MESSAGE(!packet.contains("context"), id);
    }
}

TEST_CASE("streak context: a name that no streak defines is skipped") {
    Content content = LoadContent({AppleStreak("apple_total", "total")});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    engine->Assembly().registries.events[kEventId].context = {
        "test:missing"};
    AttachAppleSystem(*engine);
    DrawOnce(*engine);
    const std::vector<json> apples =
        AppleEvents(Packets(*engine, content, Viewer::PublicSpectator()));
    REQUIRE(apples.size() == 1);
    CHECK_FALSE(apples[0].contains("context"));
}

TEST_CASE("streak context: the tail replay carries the same context") {
    Content content = LoadContent({AppleStreak("apple_total", "total")});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    AttachAppleSystem(*engine);
    match::view::EventTail tail(200);
    std::size_t observed = 0;
    for (int round = 0; round < 3; ++round) {
        DrawOnce(*engine);
        for (; observed < engine->Events().size(); ++observed) {
            tail.Record(engine->Events()[observed]);
        }
    }
    const ViewBuilder builder(*engine, content.mods);
    const json replay = builder.BuildTail(Viewer::Player("player2"), tail);
    std::vector<json> replayed;
    for (const json& packet : replay) {
        if (packet.value("id", "") == kEventId) replayed.push_back(packet);
    }
    REQUIRE(replayed.size() == 3);
    for (std::size_t index = 0; index < replayed.size(); ++index) {
        CHECK(replayed[index]["context"]
              == json{{"test:apple_total", index + 1}});
    }
}

TEST_CASE("streak leak: a streak over a hidden field reads nothing of it") {
    // INFO: the validator rejects this streak (see streak_definitions_test);
    //       the test builds it in memory to prove that the engine gate holds
    //       even then.
    StreakDef by_secret = AppleStreak("by_secret", "total");
    by_secret.group_by = "secret";
    StreakDef last_secret = AppleStreak("last_secret", "last_value");
    last_secret.value_field = "secret";
    StreakDef last_color = AppleStreak("last_color", "last_value");
    last_color.value_field = "color";
    Content content = LoadContent({AppleStreak("apple_total", "total"),
                                   by_secret, last_secret, last_color});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    engine->Assembly().registries.events[kEventId].context = {
        "test:by_secret", "test:last_secret", "test:last_color"};
    AttachAppleSystem(*engine);
    DrawOnce(*engine);
    CHECK(engine->Streaks().ValueOf("test:by_secret") == json{{"", 1}});
    CHECK(engine->Streaks().ValueOf("test:last_secret")
          == json{{"last", nullptr}, {"previous", nullptr}});
    // INFO: `color` has the audience `actor`, so it is hidden too.
    CHECK(engine->Streaks().ValueOf("test:last_color")
          == json{{"last", nullptr}, {"previous", nullptr}});
    for (const Viewer& viewer : AllViewers()) {
        for (const json& packet : Packets(*engine, content, viewer)) {
            if (packet.contains("context")) {
                CHECK(packet["context"].dump().find(kSecretText)
                      == std::string::npos);
            }
        }
    }
}

TEST_CASE("streak leak: the view of a draw holds no card identity") {
    Content content = LoadContent({});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    const std::string name = engine->GetCurrentPlayerUsername();
    REQUIRE(engine->DrawCard(name));
    bool saw_draw = false;
    for (const json& event : engine->Events()) {
        const std::string type = event.value("type", "");
        const auto view = match::view::BuildStreakEventView(*engine, event);
        if (type == "cards_drawn" || type == "drawn_card_kept") {
            saw_draw = true;
            REQUIRE(view.has_value());
            CHECK_FALSE(view->payload.contains("cards"));
            CHECK_FALSE(view->payload.contains("card"));
            for (auto it = view->payload.begin(); it != view->payload.end();
                 ++it) {
                CHECK_MESSAGE(match::view::AudienceOf(type, it.key())
                                  == match::view::Audience::kAll,
                              type << "." << it.key());
            }
        }
    }
    CHECK(saw_draw);
}

TEST_CASE("streak leak: the view of a mod event holds the all fields only") {
    Content content = LoadContent({});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    const json descriptor = {
        {"type", "mod_event"},
        {"payload",
         {{"id", kEventId},
          {"payload",
           {{"target", "player0"},
            {"color", "red"},
            {"count", 1},
            {"secret", kSecretText}}}}},
        {"causal_frame", {{"actor", "player0"}, {"turn", 4}, {"round", 2}}}};
    const auto view = match::view::BuildStreakEventView(*engine, descriptor);
    REQUIRE(view.has_value());
    CHECK(view->id == kEventId);
    CHECK(view->payload == json{{"target", "player0"}, {"count", 1}});
    CHECK(view->state.turn == 4);
    CHECK(view->state.round == 2);
    CHECK(view->state.hand_size == 7);
    CHECK_FALSE(view->derived);
}

TEST_CASE("streak view: a derived descriptor is marked derived") {
    Content content = LoadContent({});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    json descriptor = {{"type", "direction_changed"},
                       {"payload", {{"direction", -1}}},
                       {"derived", true}};
    const auto view = match::view::BuildStreakEventView(*engine, descriptor);
    REQUIRE(view.has_value());
    CHECK(view->derived);
}

TEST_CASE("streak view: events that no streak may read give no view") {
    Content content = LoadContent({});
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    for (const char* type : {"play_rejected", "prompt_opened", "signal",
                             "status_applied", "chain_aborted"}) {
        const json descriptor = {{"type", type}, {"payload", json::object()}};
        CHECK_MESSAGE(
            !match::view::BuildStreakEventView(*engine, descriptor).has_value(),
            type);
    }
}

TEST_CASE("streak cost: a limit skips streaks and keeps every core event") {
    Content content = LoadContent({AppleStreak("apple_total", "total"),
                                   AppleStreak("apple_second", "total")});
    std::unique_ptr<MatchInstance> limited;
    std::string log_text;
    {
        ConsoleCapture console;
        EnvOverride cost("UNI_STREAK_COST_MAX", "1");
        limited = MakeEngine(content);
        AttachAppleSystem(*limited);
        DrawOnce(*limited);
        DrawOnce(*limited);
        log_text = console.text.str();
    }
    std::unique_ptr<MatchInstance> unlimited = MakeEngine(content);
    AttachAppleSystem(*unlimited);
    DrawOnce(*unlimited);
    DrawOnce(*unlimited);

    const auto event_types = [](MatchInstance& engine) {
        std::vector<std::string> types;
        for (const json& event : engine.Events()) {
            types.push_back(event.value("type", ""));
        }
        return types;
    };
    CHECK(event_types(*limited) == event_types(*unlimited));
    // INFO: the streaks run in the sorted order of their ids, so the limit
    //       keeps `apple_second` and skips `apple_total`.
    CHECK(unlimited->Streaks().ValueOf("test:apple_total") == 2);
    CHECK(limited->Streaks().ValueOf("test:apple_second") == 2);
    CHECK(limited->Streaks().ValueOf("test:apple_total") == 0);
    CHECK(log_text.find("test:apple_total") != std::string::npos);
    const std::vector<json> packets =
        Packets(*limited, content, Viewer::PublicSpectator());
    for (std::size_t index = 0; index < packets.size(); ++index) {
        CHECK(packets[index]["sequence"] == index);
    }
}

TEST_CASE("streak context: the size limit leaves the context out only") {
    Content content = LoadContent({AppleStreak("apple_total", "total")});
    std::unique_ptr<MatchInstance> engine;
    std::string log_text;
    {
        ConsoleCapture console;
        EnvOverride size("UNI_CONTEXT_SIZE_MAX", "5");
        engine = MakeEngine(content);
        AttachAppleSystem(*engine);
        DrawOnce(*engine);
        log_text = console.text.str();
    }
    const std::vector<json> apples =
        AppleEvents(Packets(*engine, content, Viewer::PublicSpectator()));
    REQUIRE(apples.size() == 1);
    CHECK_FALSE(apples[0].contains("context"));
    CHECK(engine->Streaks().ValueOf("test:apple_total") == 1);
    CHECK(log_text.find("UNI_CONTEXT_SIZE_MAX") != std::string::npos);
}
