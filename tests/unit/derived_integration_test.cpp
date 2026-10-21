#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/event_tail.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file derived_integration_test.cpp
 * @brief The derived events through the whole engine: `near_win_entered` and
 *        `blocked` in the event stream, their cause, actor, audience and
 *        retention, the window in the snapshot context, and the loop guard.
 *
 * Hands are forced, so each scenario is exact. Three seats play in the order
 * `player0`, `player1`, `player2`.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;
using match::view::EventSink;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

struct EnvOverride {
    std::string key;
    EnvOverride(std::string name, const std::string& value)
        : key(std::move(name)) {
        Env::SetEnv(key, value);
    }
    ~EnvOverride() { Env::SetEnv(key, std::string()); }
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
    return content;
}

StreakDef TotalOf(const std::string& id, const std::string& event) {
    StreakDef streak;
    streak.id = id;
    streak.namespace_id = "test";
    streak.streak_id = "test:" + id;
    streak.type = "total";
    streak.matcher.event = event;
    return streak;
}

std::unique_ptr<MatchInstance> MakeEngine(
    Content& content, uint64_t seed = 42,
    const std::vector<StreakDef>& extra_streaks = {}) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (int index = 0; index < 3; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyContentResult prepared =
        MatchAssembler::PrepareContent(content.mods, content.classic);
    REQUIRE(prepared.ok());
    for (const StreakDef& streak : extra_streaks) {
        prepared.content->streaks[streak.streak_id] = streak;
    }
    AssemblyResult result =
        MatchAssembler::Assemble(prepared.content, content.classic, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

std::vector<ecs::Entity> CardsOfKind(MatchInstance& engine,
                                     const std::string& kind_id) {
    std::vector<ecs::Entity> found;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) {
            found.push_back(card);
        }
    }
    return found;
}

void ForceHand(MatchInstance& engine, const std::string& username,
               const std::vector<ecs::Entity>& cards) {
    const ecs::Entity player = *engine.FindPlayer(username);
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        match::ops::MoveCardToZone(
            engine.Store(), card, ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

void SetActiveColor(MatchInstance& engine, const std::string& color) {
    engine.Store().Get<ecs::ActiveTypeReq>(engine.Registries().match)->type =
        color;
}

/** @brief The first engine event of `type` after index `from`. */
const json* FindEvent(const MatchInstance& engine, const std::string& type,
                      std::size_t from = 0) {
    const std::vector<json>& events = engine.Events();
    for (std::size_t index = from; index < events.size(); ++index) {
        if (events[index].value("type", "") == type) return &events[index];
    }
    return nullptr;
}

std::size_t IndexOf(const MatchInstance& engine, const json* event) {
    return static_cast<std::size_t>(event - engine.Events().data());
}

std::vector<json> PacketsOf(const MatchInstance& engine,
                            const Content& content, const Viewer& viewer,
                            const std::string& id) {
    const ViewBuilder builder(engine, content.mods);
    std::vector<json> found;
    for (const json& packet : builder.BuildPackets(viewer)) {
        if (packet.value("id", "") == id) found.push_back(packet);
    }
    return found;
}

std::vector<Viewer> AllViewers() {
    return {Viewer::PublicSpectator(), Viewer::Spectator(),
            Viewer::Player("player0"), Viewer::Player("player1"),
            Viewer::Player("player2")};
}

/** @brief player0 holds two red cards, red is active; player0 to move. */
struct Table {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine;
    std::vector<ecs::Entity> fives;
    std::vector<ecs::Entity> skips;
    ecs::Entity six{};
    ecs::Entity spare{};
    std::vector<ecs::Entity> filler;

    explicit Table(const std::vector<StreakDef>& extra_streaks = {}) {
        engine = MakeEngine(content, 42, extra_streaks);
        for (const char* kind : {"uni:red_5", "uni:red_7", "uni:red_8"}) {
            const auto cards = CardsOfKind(*engine, kind);
            REQUIRE_FALSE(cards.empty());
            fives.push_back(cards[0]);
        }
        skips = CardsOfKind(*engine, "uni:red_skip");
        REQUIRE_FALSE(skips.empty());
        six = CardsOfKind(*engine, "uni:red_6").at(0);
        spare = CardsOfKind(*engine, "uni:red_9").at(0);
        for (const char* kind : {"uni:red_1", "uni:red_2", "uni:red_3"}) {
            filler.push_back(CardsOfKind(*engine, kind).at(0));
        }
        SetActiveColor(*engine, "red");
        REQUIRE(engine->GetCurrentPlayerUsername() == "player0");
    }

    /** @brief player0 plays a red 5 and keeps one card: a near win. */
    void OpenNearWin() {
        ForceHand(*engine, "player0", {fives[0], fives[1]});
        REQUIRE(engine->PlayCard("player0", fives[0]));
    }
};

}  // namespace

TEST_CASE("near win event: follows the play that left one card, with a "
          "cause") {
    Table table;
    table.OpenNearWin();
    const json* played = FindEvent(*table.engine, "card_played");
    REQUIRE(played != nullptr);
    const json* near_win = FindEvent(*table.engine, "near_win_entered");
    REQUIRE(near_win != nullptr);
    CHECK(IndexOf(*table.engine, near_win)
          == IndexOf(*table.engine, played) + 1);
    CHECK((*near_win)["derived"] == true);
    CHECK((*near_win)["payload"]["target"] == "player0");
    CHECK((*near_win)["causal_frame"]["cause_event"]
          == (*played)["event_id"]);
    CHECK((*near_win)["causal_frame"]["actor"] == "player0");
    CHECK((*near_win)["event_id"].get<int>()
          == (*played)["event_id"].get<int>() + 1);
}

TEST_CASE("near win event: every viewer gets it with the sequence of the "
          "play as cause") {
    Table table;
    table.OpenNearWin();
    for (const Viewer& viewer : AllViewers()) {
        const auto played =
            PacketsOf(*table.engine, table.content, viewer, "card_played");
        const auto near_win = PacketsOf(*table.engine, table.content, viewer,
                                        "near_win_entered");
        REQUIRE(played.size() == 1);
        REQUIRE(near_win.size() == 1);
        CHECK(near_win[0]["payload"]["target"] == "player0");
        CHECK(near_win[0]["cause"] == played[0]["sequence"]);
        CHECK(near_win[0]["actor"] == "player0");
        CHECK(near_win[0]["sequence"].get<int>()
              == played[0]["sequence"].get<int>() + 1);
    }
}

TEST_CASE("near win event: no event when the hand does not drop to one "
          "card") {
    Table table;
    ForceHand(*table.engine, "player0",
              {table.fives[0], table.fives[1], table.fives[2]});
    REQUIRE(table.engine->PlayCard("player0", table.fives[0]));
    CHECK(FindEvent(*table.engine, "near_win_entered") == nullptr);
}

TEST_CASE("near win event: a draw card that leaves one card opens nothing") {
    Table table;
    const auto draw_twos = CardsOfKind(*table.engine, "uni:red_draw2");
    REQUIRE_FALSE(draw_twos.empty());
    ForceHand(*table.engine, "player0", {draw_twos[0], table.fives[1]});
    REQUIRE(table.engine->PlayCard("player0", draw_twos[0]));
    CHECK(FindEvent(*table.engine, "near_win_entered") == nullptr);
}

TEST_CASE("blocked event: a skip of the near-win seat, read before the "
          "skip closed the window") {
    Table table;
    table.OpenNearWin();
    ForceHand(*table.engine, "player1",
              {table.fives[2], table.spare, table.filler[0]});
    SetActiveColor(*table.engine, "red");
    ForceHand(*table.engine, "player2",
              {table.skips[0], table.six, table.filler[1]});
    REQUIRE(table.engine->PlayCard("player1", table.fives[2]));
    SetActiveColor(*table.engine, "red");
    REQUIRE(table.engine->PlayCard("player2", table.skips[0]));

    const json* skipped = FindEvent(*table.engine, "turn_skipped");
    REQUIRE(skipped != nullptr);
    const json* blocked = FindEvent(*table.engine, "blocked");
    REQUIRE(blocked != nullptr);
    CHECK(IndexOf(*table.engine, blocked)
          == IndexOf(*table.engine, skipped) + 1);
    CHECK((*blocked)["derived"] == true);
    CHECK((*blocked)["payload"]["target"] == "player0");
    CHECK((*blocked)["causal_frame"]["cause_event"]
          == (*skipped)["event_id"]);
    CHECK((*blocked)["causal_frame"]["actor"] == "player2");
    // INFO: the skip closed the window, yet the block was read before it.
    const std::size_t after_block = IndexOf(*table.engine, blocked) + 1;
    CHECK(FindEvent(*table.engine, "blocked", after_block) == nullptr);
    const ViewBuilder builder(*table.engine, table.content.mods);
    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player1"), sink);
    CHECK(snapshot["match_state"]["context"]["near_win"]["near_win_entered"]
                 ["open"]
          == false);
}

TEST_CASE("blocked event: the packet carries the cause and the actor") {
    Table table;
    table.OpenNearWin();
    ForceHand(*table.engine, "player1",
              {table.fives[2], table.spare, table.filler[0]});
    ForceHand(*table.engine, "player2",
              {table.skips[0], table.six, table.filler[1]});
    SetActiveColor(*table.engine, "red");
    REQUIRE(table.engine->PlayCard("player1", table.fives[2]));
    SetActiveColor(*table.engine, "red");
    REQUIRE(table.engine->PlayCard("player2", table.skips[0]));
    for (const Viewer& viewer : AllViewers()) {
        const auto skips =
            PacketsOf(*table.engine, table.content, viewer, "turn_skipped");
        const auto blocked =
            PacketsOf(*table.engine, table.content, viewer, "blocked");
        REQUIRE(skips.size() == 1);
        REQUIRE(blocked.size() == 1);
        CHECK(blocked[0]["cause"] == skips[0]["sequence"]);
        CHECK(blocked[0]["actor"] == "player2");
        CHECK(blocked[0]["payload"]["target"] == "player0");
    }
}

TEST_CASE("blocked event: the target who draws or plays first escapes") {
    Table table;
    table.OpenNearWin();
    ForceHand(*table.engine, "player1",
              {table.fives[2], table.spare, table.filler[0]});
    SetActiveColor(*table.engine, "red");
    REQUIRE(table.engine->PlayCard("player1", table.fives[2]));
    ForceHand(*table.engine, "player2",
              {table.skips[0], table.six, table.filler[1]});
    SetActiveColor(*table.engine, "red");
    // player2 plays a plain card; the skip comes later, after player0 moved.
    REQUIRE(table.engine->PlayCard(
        "player2", table.six));
    REQUIRE(table.engine->DrawCard("player0"));
    CHECK(FindEvent(*table.engine, "blocked") == nullptr);
    const ViewBuilder builder(*table.engine, table.content.mods);
    EventSink sink;
    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player1"), sink);
    CHECK(snapshot["match_state"]["context"]["near_win"]["near_win_entered"]
                 ["open"]
          == false);
}

TEST_CASE("derived events: they are logged, so a late joiner replays them "
          "with the cause") {
    Table table;
    table.OpenNearWin();
    match::view::EventTail tail(200);
    for (const json& event : table.engine->Events()) tail.Record(event);
    const ViewBuilder builder(*table.engine, table.content.mods);
    for (const Viewer& viewer : AllViewers()) {
        const json replay = builder.BuildTail(viewer, tail);
        json played;
        json near_win;
        for (const json& packet : replay) {
            if (packet.value("id", "") == "card_played") played = packet;
            if (packet.value("id", "") == "near_win_entered") near_win = packet;
        }
        REQUIRE(played.is_object());
        REQUIRE(near_win.is_object());
        CHECK(near_win["payload"]["target"] == "player0");
        CHECK(near_win["cause"] == played["sequence"]);
        CHECK(near_win["actor"] == "player0");
        CHECK(near_win["replayed"] == true);
    }
}

TEST_CASE("snapshot context: the near-win window of a late joiner equals "
          "the window of the engine") {
    Table table;
    const ViewBuilder builder(*table.engine, table.content.mods);
    EventSink fresh;
    json context = builder.BuildSnapshot(Viewer::Player("player2"), fresh)
                       ["match_state"]["context"];
    REQUIRE(context.contains("near_win"));
    CHECK(context["near_win"]["near_win_entered"]["target"].is_null());
    CHECK(context["near_win"]["near_win_entered"]["open"] == false);

    table.OpenNearWin();
    for (const Viewer& viewer : AllViewers()) {
        EventSink late;
        context = builder.BuildSnapshot(viewer, late)["match_state"]
                                      ["context"];
        CHECK(context["near_win"]["near_win_entered"]["target"] == "player0");
        CHECK(context["near_win"]["near_win_entered"]["open"] == true);
    }
}

TEST_CASE("derived loop guard: no derived event feeds a streak") {
    // INFO: the validator refuses a streak over a derived event, so the test
    //       puts the streaks into the registry of the match directly. The
    //       engine must still keep them at 0 while it emits the real derived
    //       events.
    Table table({TotalOf("plays", "card_played"),
                 TotalOf("near_wins", "near_win_entered"),
                 TotalOf("blocks", "blocked")});
    table.OpenNearWin();
    ForceHand(*table.engine, "player1", {table.fives[2], table.spare,
                                         table.filler[0]});
    ForceHand(*table.engine, "player2",
              {table.skips[0], table.six, table.filler[1]});
    SetActiveColor(*table.engine, "red");
    REQUIRE(table.engine->PlayCard("player1", table.fives[2]));
    SetActiveColor(*table.engine, "red");
    REQUIRE(table.engine->PlayCard("player2", table.skips[0]));

    int near_wins = 0;
    int blocks = 0;
    for (const json& event : table.engine->Events()) {
        near_wins += event.value("type", "") == "near_win_entered" ? 1 : 0;
        blocks += event.value("type", "") == "blocked" ? 1 : 0;
    }
    REQUIRE(near_wins == 1);
    REQUIRE(blocks == 1);
    const json values = table.engine->Streaks().AllValues();
    CHECK(values["test:plays"] == 3);
    CHECK(values["test:near_wins"] == 0);
    CHECK(values["test:blocks"] == 0);
}

TEST_CASE("derived cost: the derived pass counts toward UNI_STREAK_COST_MAX "
          "and a core event is never dropped") {
    // INFO: a card_played costs 3 streak evaluations in the shipped mod (the
    //       skip, reverse, and wild runs reset on it). The window definition
    //       is the fourth.
    {
        EnvOverride limit("UNI_STREAK_COST_MAX", "3");
        Table table;
        table.OpenNearWin();
        CHECK(FindEvent(*table.engine, "card_played") != nullptr);
        CHECK(FindEvent(*table.engine, "near_win_entered") == nullptr);
    }
    {
        EnvOverride limit("UNI_STREAK_COST_MAX", "4");
        Table table;
        table.OpenNearWin();
        CHECK(FindEvent(*table.engine, "near_win_entered") != nullptr);
    }
    {
        EnvOverride limit("UNI_STREAK_COST_MAX", "0");
        Table table;
        table.OpenNearWin();
        CHECK(FindEvent(*table.engine, "card_played") != nullptr);
        CHECK(FindEvent(*table.engine, "near_win_entered") == nullptr);
    }
}

TEST_CASE("derived events: the audience table gives them to every seat") {
    Table table;
    table.OpenNearWin();
    for (const Viewer& viewer : AllViewers()) {
        const auto packets =
            PacketsOf(*table.engine, table.content, viewer,
                      "near_win_entered");
        REQUIRE(packets.size() == 1);
        CHECK(packets[0]["payload"].size() == 1);
    }
}

TEST_CASE("event context: a draw carries the draw run and the total draws") {
    Table table;
    ForceHand(*table.engine, "player0", {table.fives[0], table.fives[1]});
    REQUIRE(table.engine->DrawCard("player0"));
    if (table.engine->PendingPlayDrawnState().has_value()) {
        REQUIRE(table.engine->KeepDrawn("player0"));
    }
    for (const Viewer& viewer : AllViewers()) {
        const auto draws =
            PacketsOf(*table.engine, table.content, viewer, "cards_drawn");
        REQUIRE(draws.size() == 1);
        REQUIRE(draws[0].contains("context"));
        CHECK(draws[0]["context"]["uni:draw_run"]["player0"] == 1);
        CHECK(draws[0]["context"]["uni:total_draws"] == 1);
        CHECK_FALSE(draws[0]["context"].contains("uni:wild_run"));
    }
}

TEST_CASE("event context: a color choice carries the wild run and the "
          "colors") {
    Table table;
    const auto wilds = CardsOfKind(*table.engine, "uni:wild");
    REQUIRE_FALSE(wilds.empty());
    ForceHand(*table.engine, "player0", {wilds[0], table.fives[1]});
    REQUIRE(table.engine->PlayCard("player0", wilds[0]));
    REQUIRE(table.engine->SubmitInput("player0", "blue"));
    for (const Viewer& viewer : AllViewers()) {
        const auto changes = PacketsOf(*table.engine, table.content, viewer,
                                       "active_type_changed");
        REQUIRE(changes.size() == 1);
        REQUIRE(changes[0].contains("context"));
        CHECK(changes[0]["context"]["uni:wild_run"] == 1);
        CHECK(changes[0]["context"]["uni:last_wild_color"]["last"] == "blue");
        CHECK(changes[0]["context"]["uni:last_wild_color"]["previous"]
                  .is_null());
    }
}

TEST_CASE("event context: the core table asks for the counters") {
    using match::view::CoreContextNames;
    CHECK(CoreContextNames("cards_drawn")
          == std::vector<std::string>{"uni:draw_run", "uni:total_draws"});
    CHECK(CoreContextNames("active_type_changed")
          == std::vector<std::string>{"uni:wild_run", "uni:last_wild_color"});
    CHECK(CoreContextNames("turn_skipped")
          == std::vector<std::string>{"uni:skip_run"});
    CHECK(CoreContextNames("direction_changed")
          == std::vector<std::string>{"uni:reverse_run"});
    CHECK(CoreContextNames("near_win_entered").empty());
    CHECK(CoreContextNames("blocked").empty());
}

namespace {

json LateJoinView(Table& table, const Viewer& viewer) {
    match::view::EventTail tail(200);
    for (const json& event : table.engine->Events()) tail.Record(event);
    const ViewBuilder builder(*table.engine, table.content.mods);
    EventSink sink;
    json view;
    view["tail"] = builder.BuildTail(viewer, tail);
    view["near_win"] = builder.BuildSnapshot(viewer, sink)["match_state"]
                                           ["context"]["near_win"];
    return view;
}

void CheckGolden(const std::string& name, const json& actual) {
    const fs::path golden =
        ProjectRoot() / "tests" / "fixtures" / "golden_derived" / name;
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(1) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}

/** @brief The derived envelopes of a stream, without the per-stream numbers. */
std::vector<json> DerivedCues(const std::vector<json>& packets,
                              const std::vector<json>& all) {
    std::vector<json> cues;
    for (const json& packet : packets) {
        const std::string id = packet.value("id", "");
        if (id != "near_win_entered" && id != "blocked") continue;
        std::string cause_id;
        for (const json& other : all) {
            if (packet.contains("cause")
                && other["sequence"] == packet["cause"]) {
                cause_id = other.value("id", "");
            }
        }
        cues.push_back(json{{"id", id},
                            {"payload", packet["payload"]},
                            {"actor", packet.value("actor", json())},
                            {"cause_id", cause_id}});
    }
    return cues;
}

void PlayNearWinAndBlock(Table& table) {
    table.OpenNearWin();
    ForceHand(*table.engine, "player1",
              {table.fives[2], table.spare, table.filler[0]});
    ForceHand(*table.engine, "player2",
              {table.skips[0], table.six, table.filler[1]});
    SetActiveColor(*table.engine, "red");
    REQUIRE(table.engine->PlayCard("player1", table.fives[2]));
    SetActiveColor(*table.engine, "red");
    REQUIRE(table.engine->PlayCard("player2", table.skips[0]));
}

}  // namespace

TEST_CASE("derived golden: a late joiner gets the near-win window and cue") {
    Table table;
    table.OpenNearWin();
    CheckGolden("near_win_open_late_join.json",
                LateJoinView(table, Viewer::Player("player2")));
}

TEST_CASE("derived golden: a late joiner after the block") {
    Table table;
    PlayNearWinAndBlock(table);
    CheckGolden("near_win_blocked_late_join.json",
                LateJoinView(table, Viewer::Player("player1")));
}

TEST_CASE("derived golden: a late joiner shows the same cue as a continuous "
          "seat") {
    Table table;
    PlayNearWinAndBlock(table);
    const ViewBuilder builder(*table.engine, table.content.mods);
    for (const Viewer& viewer : AllViewers()) {
        EventSink sink;
        std::vector<json> continuous;
        for (const json& event : table.engine->Events()) {
            const std::optional<json> packet =
                builder.Wrap(event, viewer, sink);
            if (packet.has_value()) continuous.push_back(*packet);
        }
        match::view::EventTail tail(200);
        for (const json& event : table.engine->Events()) tail.Record(event);
        std::vector<json> replay;
        for (const json& packet : builder.BuildTail(viewer, tail)) {
            replay.push_back(packet);
        }
        const std::vector<json> live = DerivedCues(continuous, continuous);
        const std::vector<json> late = DerivedCues(replay, replay);
        REQUIRE(live.size() == 2);
        CHECK(late == live);
        CHECK(live[0]["id"] == "near_win_entered");
        CHECK(live[0]["cause_id"] == "card_played");
        CHECK(live[1]["id"] == "blocked");
        CHECK(live[1]["cause_id"] == "turn_skipped");
    }
}

TEST_CASE("derived golden: a default run never rewrites a golden file") {
    const fs::path golden = ProjectRoot() / "tests" / "fixtures"
                            / "golden_derived" / "near_win_open_late_join.json";
    std::error_code error;
    const auto before = fs::last_write_time(golden, error);
    Table table;
    table.OpenNearWin();
    (void)LateJoinView(table, Viewer::Player("player2"));
    if (std::getenv("UNI_UPDATE_GOLDEN") == nullptr) {
        CHECK(fs::last_write_time(golden, error) == before);
    }
}
