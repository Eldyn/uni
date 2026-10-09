#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/timers.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/view_util.hpp>

#include "support/fake_broadcaster.hpp"
#include "support/hidden_cards.hpp"
#include "support/leak_scanner.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file match_session_tail_test.cpp
 * @brief The event tail: retention, late join, replay marks and redaction.
 *
 * Bots play real matches on a fake clock, so every run is the same. A late
 * joiner gets `match_state.tail`: the retained client events, projected for
 * that viewer with the live projector. The tail is a self-contained list. It
 * has its own `sequence` numbers (0, 1, 2, ...) and every envelope has
 * `replayed: true`. It does not consume the live stream of the viewer, so the
 * live stream has no gap and no duplicate.
 *
 * The golden file `tests/fixtures/golden_tail/late_join_tail.json` holds the
 * tail of one late seat. Set `UNI_UPDATE_GOLDEN=1` to write it again after a
 * change that you want.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using leak_scanner::HiddenCard;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadedMod;
using match::modload::ScanModsDirectory;
using match::server::BotStep;
using match::server::HeuristicBotPolicy;
using match::server::MatchSession;
using match::view::Retention;
using nlohmann::json;

namespace {

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
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
    match::modload::LoadResult load =
        ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

struct FakeClock {
    int64_t now = 0;
    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

std::unique_ptr<MatchInstance> MakeEngine(
    Content& content, const std::vector<std::string>& seats,
    const std::vector<std::string>& mods, FakeClock& clock, uint64_t seed) {
    DeckDef deck = content.classic;
    if (!mods.empty()) deck.mods = mods;
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (const std::string& name : seats) {
        MatchPlayerSpec spec;
        spec.username = name;
        options.players.push_back(spec);
    }
    match::engine::AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE(result.ok());
    match::WindowConfig window;
    window.window_ms = 7000;
    window.mode = match::WindowMode::kFixed;
    return std::make_unique<MatchInstance>(std::move(result.assembly), window,
                                           clock.Fn());
}

AppWebSocket* Socket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x500 + index));
}

/** @brief A bot match: its clock, its session and the sockets by seat. */
struct BotTable {
    std::unique_ptr<FakeClock> clock = std::make_unique<FakeClock>();
    std::unique_ptr<MatchSession> session;
    HeuristicBotPolicy policy{12345, {}};
    FakeBroadcaster fake;
};

/**
 * @brief Build a bot match. A seat in `unbound` has no socket at the start.
 */
std::unique_ptr<BotTable> MakeBotTable(uint64_t seed,
                                       const std::vector<std::string>& unbound
                                       = {}) {
    Content content;
    REQUIRE(LoadContent(content));
    auto table = std::make_unique<BotTable>();
    const std::vector<std::string> seats = {"bot0", "bot1", "bot2"};
    MatchSession::SocketMap sockets;
    for (std::size_t index = 0; index < seats.size(); ++index) {
        const bool is_unbound =
            std::ranges::find(unbound, seats[index]) != unbound.end();
        sockets[seats[index]] =
            is_unbound ? nullptr : Socket(static_cast<int>(index));
    }
    table->session = std::make_unique<MatchSession>(
        MakeEngine(content, seats, {}, *table->clock, seed),
        std::move(content.mods), sockets);
    table->session->EmitMatchStart(table->fake);
    return table;
}

/** @brief Let the bots act `steps` times, emitting events after each. */
void Drive(BotTable& table, int steps) {
    MatchSession& session = *table.session;
    for (int step = 0; step < steps && !session.Engine().IsMatchOver();
         ++step) {
        const std::optional<json> pending = session.Engine().PendingInput();
        if (pending.has_value()) {
            const std::string target = match::view::ResolvePlayer(
                session.Engine(), pending->value("target", json()));
            if (!target.empty()) BotStep(session, table.policy, target);
        } else if (session.Engine().WindowOpen()) {
            const json window = session.Engine().ExportWindow();
            for (const json& responder :
                 window.value("responders", json::array())) {
                if (!responder.is_string()) continue;
                BotStep(session, table.policy, responder.get<std::string>());
            }
        } else {
            const std::string current =
                session.Engine().GetCurrentPlayerUsername();
            if (!current.empty()) BotStep(session, table.policy, current);
        }
        session.Tick();
        session.EmitEvents(table.fake);
    }
}

std::vector<json> FramesTo(const FakeBroadcaster& fake, AppWebSocket* socket,
                           std::size_t from = 0) {
    std::vector<json> frames;
    for (std::size_t index = from; index < fake.sent.size(); ++index) {
        if (fake.sent[index].to != socket) continue;
        frames.push_back(json::parse(fake.sent[index].payload));
    }
    return frames;
}

json LastSnapshot(const FakeBroadcaster& fake, AppWebSocket* socket) {
    json found;
    for (const json& frame : FramesTo(fake, socket)) {
        if (frame.value("action", std::string()) == "match_state_updated") {
            found = frame;
        }
    }
    return found;
}

/** @brief True for a client event that the audience table declares. */
bool IsCoreEvent(const json& envelope) {
    return match::view::FindEventRule(envelope.value("id", std::string()))
           != nullptr;
}

/** @brief True when the event belongs to the play log tail. */
bool IsLogEvent(const json& envelope) {
    return IsCoreEvent(envelope)
           && match::view::RetentionOf(
                  envelope["id"].get<std::string>(),
                  envelope.value("payload", json::object()))
                  == Retention::kLog;
}

/** @brief What two viewers must agree on: no sequence, no cause. */
json Comparable(const json& envelope) {
    json out = json::object();
    for (const char* key : {"id", "actor", "turn", "round", "payload"}) {
        if (envelope.contains(key)) out[key] = envelope[key];
    }
    return out;
}

std::vector<json> LiveLogEvents(const std::vector<json>& frames) {
    std::vector<json> events;
    for (const json& frame : frames) {
        if (frame.value("action", std::string()) != "match_event") continue;
        if (IsLogEvent(frame)) events.push_back(Comparable(frame));
    }
    return events;
}

std::vector<json> TailLogEvents(const json& snapshot) {
    std::vector<json> events;
    for (const json& envelope : snapshot["match_state"]["tail"]) {
        if (IsLogEvent(envelope)) events.push_back(Comparable(envelope));
    }
    return events;
}

/** @brief Drive until the seat has seen `count` log events live. */
int StepsForLogEvents(uint64_t seed, std::size_t count) {
    std::unique_ptr<BotTable> table = MakeBotTable(seed);
    int steps = 0;
    while (LiveLogEvents(FramesTo(table->fake, Socket(2))).size() < count
           && steps < 2000) {
        Drive(*table, 1);
        ++steps;
    }
    return steps;
}

struct EnvOverride {
    std::string key;
    EnvOverride(std::string name, const std::string& value)
        : key(std::move(name)) {
        Env::SetEnv(key, value);
    }
    ~EnvOverride() { Env::SetEnv(key, std::string()); }
};

}  // namespace

TEST_CASE("tail: a late spectator gets the tail in the warm-up snapshot") {
    std::unique_ptr<BotTable> table = MakeBotTable(7);
    Drive(*table, 12);

    table->session->BindViewer("late", Socket(9));
    table->session->SendSnapshot(table->fake, Socket(9), "late", true);

    const json snapshot = LastSnapshot(table->fake, Socket(9));
    REQUIRE_FALSE(snapshot.is_null());
    const json& tail = snapshot["match_state"]["tail"];
    REQUIRE(tail.is_array());
    REQUIRE_FALSE(tail.empty());
    for (std::size_t index = 0; index < tail.size(); ++index) {
        CHECK(tail[index]["replayed"] == true);
        CHECK(tail[index]["sequence"] == index);
    }
}

TEST_CASE("tail: the tail does not consume the live stream of the viewer") {
    std::unique_ptr<BotTable> table = MakeBotTable(7);
    Drive(*table, 12);
    table->session->BindViewer("late", Socket(9));
    const std::size_t join_start = table->fake.sent.size();
    table->session->SendSnapshot(table->fake, Socket(9), "late", true);
    const json watermark = LastSnapshot(table->fake, Socket(9))
        ["match_state"]["seq_watermark"];

    Drive(*table, 6);

    uint32_t expected = watermark.get<uint32_t>();
    std::size_t live_count = 0;
    bool after_snapshot = false;
    for (const json& frame : FramesTo(table->fake, Socket(9), join_start)) {
        if (frame.value("action", std::string()) == "match_state_updated") {
            after_snapshot = true;
        }
        if (!after_snapshot) continue;
        if (frame.value("action", std::string()) != "match_event") continue;
        CHECK(frame["sequence"] == expected);
        CHECK_FALSE(frame.contains("replayed"));
        ++expected;
        ++live_count;
    }
    CHECK(live_count > 0);
}

TEST_CASE("tail: a snapshot request carries the tail and a broadcast not") {
    std::unique_ptr<BotTable> table = MakeBotTable(7);
    Drive(*table, 20);

    table->session->BroadcastSnapshot(table->fake);
    const json broadcast = LastSnapshot(table->fake, Socket(0));
    REQUIRE_FALSE(broadcast.is_null());
    CHECK_FALSE(broadcast["match_state"].contains("tail"));

    table->fake.Clear();
    REQUIRE(table->session->HandleSnapshotRequest(table->fake, "bot0", {}));
    const json answer = LastSnapshot(table->fake, Socket(0));
    REQUIRE_FALSE(answer.is_null());
    CHECK_FALSE(answer["match_state"]["tail"].empty());
}

TEST_CASE("tail: a late seat has the log tail of a continuous seat") {
    const int steps = StepsForLogEvents(7, 15);
    REQUIRE(steps < 2000);

    std::unique_ptr<BotTable> continuous = MakeBotTable(7);
    Drive(*continuous, steps);
    std::unique_ptr<BotTable> late = MakeBotTable(7, {"bot2"});
    Drive(*late, steps);
    REQUIRE(late->session->BindSocket("bot2", Socket(2)));
    late->session->SendSnapshot(late->fake, Socket(2), "bot2", false);

    const std::vector<json> expected =
        LiveLogEvents(FramesTo(continuous->fake, Socket(2)));
    const json snapshot = LastSnapshot(late->fake, Socket(2));
    REQUIRE_FALSE(snapshot.is_null());
    const std::vector<json> actual = TailLogEvents(snapshot);
    REQUIRE(expected.size() >= 15);
    REQUIRE(expected.size() < late->session->Tail().Limit());
    CHECK(actual == expected);
}

TEST_CASE("tail: the tail of a late seat matches the golden file") {
    const int steps = StepsForLogEvents(7, 15);
    std::unique_ptr<BotTable> late = MakeBotTable(7, {"bot2"});
    Drive(*late, steps);
    REQUIRE(late->session->BindSocket("bot2", Socket(2)));
    late->session->SendSnapshot(late->fake, Socket(2), "bot2", false);
    const json snapshot = LastSnapshot(late->fake, Socket(2));
    REQUIRE_FALSE(snapshot.is_null());
    const json tail = snapshot["match_state"]["tail"];

    const fs::path golden =
        ProjectRoot() / "tests" / "fixtures" / "golden_tail"
        / "late_join_tail.json";
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << tail.dump(2) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == tail);
}

namespace {

std::vector<std::string> LeakFindings(const json& tail,
                                      const std::vector<HiddenCard>& hidden) {
    std::vector<std::string> findings;
    for (const leak_scanner::Leak& leak :
         leak_scanner::FindLeaks(tail, hidden)) {
        findings.push_back(leak.kind + " at " + leak.path);
    }
    return findings;
}

}  // namespace

TEST_CASE("tail: a late joiner learns no hidden card from the tail") {
    for (const uint64_t seed : {7u, 11u, 23u}) {
        for (const int steps : {12, 30, 60}) {
            CAPTURE(seed);
            CAPTURE(steps);
            std::unique_ptr<BotTable> table = MakeBotTable(seed, {"bot2"});
            table->session->SetSeatPrivacy("bot1", true);
            Drive(*table, steps);
            REQUIRE(table->session->BindSocket("bot2", Socket(2)));
            table->session->BindViewer("open", Socket(8));
            table->session->BindViewer("watcher", Socket(9));
            table->session->SendSnapshot(table->fake, Socket(2), "bot2",
                                         false);
            table->session->SendSnapshot(table->fake, Socket(9), "watcher",
                                         true);
            const MatchInstance& engine = table->session->Engine();

            const json seat_tail =
                (LastSnapshot(table->fake, Socket(2)))
                    ["match_state"]["tail"];
            CHECK_FALSE(seat_tail.empty());
            for (const std::string& finding : LeakFindings(
                     seat_tail, hidden_cards::HiddenFrom(engine, "bot2"))) {
                FAIL_CHECK("seat: " << finding);
            }

            const json private_tail =
                (LastSnapshot(table->fake, Socket(9)))
                    ["match_state"]["tail"];
            CHECK_FALSE(private_tail.empty());
            for (const std::string& finding : LeakFindings(
                     private_tail,
                     hidden_cards::HiddenFromSpectator(engine, {"bot1"}))) {
                FAIL_CHECK("private spectator: " << finding);
            }

            table->session->SetSeatPrivacy("bot1", false);
            table->session->SendSnapshot(table->fake, Socket(8), "open",
                                         true);
            const json open_tail =
                (LastSnapshot(table->fake, Socket(8)))
                    ["match_state"]["tail"];
            CHECK_FALSE(open_tail.empty());
            for (const std::string& finding : LeakFindings(
                     open_tail,
                     hidden_cards::HiddenFromSpectator(engine, {}))) {
                FAIL_CHECK("open spectator: " << finding);
            }
        }
    }
}

TEST_CASE("tail: the leak scan finds a card that the tail does carry") {
    std::unique_ptr<BotTable> table = MakeBotTable(7, {"bot2"});
    Drive(*table, 30);
    REQUIRE(table->session->BindSocket("bot2", Socket(2)));
    table->session->SendSnapshot(table->fake, Socket(2), "bot2", false);
    const json tail = (LastSnapshot(table->fake, Socket(2)))
        ["match_state"]["tail"];

    // INFO: a played card is public, so the tail names it. The scan must find
    //       it when we claim that it is hidden.
    std::optional<uint32_t> played_bits;
    for (const json& envelope : tail) {
        if (envelope["id"] == "card_played") {
            played_bits = envelope["payload"]["card"].get<uint32_t>();
            break;
        }
    }
    REQUIRE(played_bits.has_value());
    HiddenCard claimed;
    claimed.bits = *played_bits;
    CHECK_FALSE(LeakFindings(tail, {claimed}).empty());
}

TEST_CASE("tail: the log of the store is capped by UNI_EVENT_TAIL_MAX") {
    EnvOverride limit("UNI_EVENT_TAIL_MAX", "5");
    std::unique_ptr<BotTable> table = MakeBotTable(7, {"bot2"});
    Drive(*table, 60);
    CHECK(table->session->Tail().Limit() == 5);

    std::size_t log_entries = 0;
    for (const match::view::TailEntry& entry :
         table->session->Tail().Entries()) {
        if (entry.retention == Retention::kLog) ++log_entries;
    }
    CHECK(log_entries == 5);

    REQUIRE(table->session->BindSocket("bot2", Socket(2)));
    table->session->SendSnapshot(table->fake, Socket(2), "bot2", false);
    const json snapshot = LastSnapshot(table->fake, Socket(2));
    REQUIRE_FALSE(snapshot.is_null());
    CHECK(TailLogEvents(snapshot).size() <= 5);
}

TEST_CASE("tail: the store drops turn events at the turn advance") {
    std::unique_ptr<BotTable> table = MakeBotTable(7);
    Drive(*table, 60);
    const ecs::MatchMeta* meta = table->session->Engine().Store()
        .Get<ecs::MatchMeta>(table->session->Engine().Registries().match);
    REQUIRE(meta != nullptr);
    std::size_t turn_entries = 0;
    std::size_t older_log_entries = 0;
    for (const match::view::TailEntry& entry :
         table->session->Tail().Entries()) {
        if (entry.retention == Retention::kUntilTurnEnd) {
            CHECK(entry.turn == meta->turn);
            ++turn_entries;
        }
        if (entry.retention == Retention::kLog && entry.turn < meta->turn) {
            ++older_log_entries;
        }
    }
    CHECK(turn_entries > 0);
    CHECK(older_log_entries > 0);
}

namespace {

std::optional<ecs::Entity> FindCard(MatchInstance& engine,
                                    const std::string& color,
                                    const std::string& label,
                                    std::optional<ecs::Entity> excluded = {}) {
    for (ecs::Entity card : engine.Registries().cards) {
        if (excluded.has_value() && card == *excluded) continue;
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

/** @brief Top the draw pile with a colored card and make it playable. */
bool ArmPlayableDraw(MatchInstance& engine) {
    for (ecs::Entity pile : engine.Store().EntitiesWith<ecs::PileContents>()) {
        ecs::PileContents* draw = engine.Store().Get<ecs::PileContents>(pile);
        if (draw == nullptr || draw->kind != ecs::PileKind::kDraw) continue;
        for (std::size_t index = 0; index < draw->cards.size(); ++index) {
            const ecs::FaceSpec* face =
                engine.Store().Get<ecs::FaceSpec>(draw->cards[index]);
            if (face == nullptr || face->color == "white") continue;
            std::swap(draw->cards[index], draw->cards.back());
            for (std::size_t slot = 0; slot < draw->cards.size(); ++slot) {
                if (ecs::InZone* zone =
                        engine.Store().Get<ecs::InZone>(draw->cards[slot])) {
                    zone->ordinal = static_cast<uint32_t>(slot);
                }
            }
            SetActiveColor(engine, face->color);
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("tail: a late joiner during a held draw gets the hold from the "
          "snapshot, not from the tail") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, {"player0", "player1", "player2"}, {}, clock, 42);
    REQUIRE(ArmPlayableDraw(*engine));
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    MatchSession session(std::move(engine), std::move(content.mods),
                         {{"player0", Socket(0)},
                          {"player1", nullptr},
                          {"player2", Socket(2)}});
    FakeBroadcaster fake;
    session.EmitMatchStart(fake);
    session.EmitEvents(fake);
    REQUIRE(session.BindSocket("player1", Socket(1)));
    session.SendSnapshot(fake, Socket(1), "player1", false);

    const json snapshot = LastSnapshot(fake, Socket(1));
    REQUIRE_FALSE(snapshot.is_null());
    const json& context = snapshot["match_state"]["context"];
    CHECK(context["hold"]["player"] == "player0");
    CHECK(context["hold"]["held"] == true);
    for (const json& envelope : snapshot["match_state"]["tail"]) {
        CHECK(envelope["id"] != "cards_drawn");
        CHECK(envelope["id"] != "drawn_card_kept");
    }
}

TEST_CASE("tail: a jump-in ends the turn of the retained events") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, {"player0", "player1", "player2"}, {"uni", "jump_in"}, clock,
        42);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player2 = *engine->FindPlayer("player2");
    const auto played = FindCard(*engine, "red", "6");
    REQUIRE(played.has_value());
    const auto twin = FindCard(*engine, "red", "6", played);
    const auto filler = FindCard(*engine, "blue", "5");
    REQUIRE((twin && filler));
    ForceHand(*engine, player0, {*played, *filler});
    ForceHand(*engine, player2, {*twin, *filler});
    SetActiveColor(*engine, "red");

    MatchInstance& engine_ref = *engine;
    MatchSession session(std::move(engine), std::move(content.mods),
                         {{"player0", Socket(0)}, {"player1", Socket(1)},
                          {"player2", nullptr}});
    FakeBroadcaster fake;
    session.EmitMatchStart(fake);
    REQUIRE(engine_ref.PlayCard("player0", *played));
    session.EmitEvents(fake);
    REQUIRE(engine_ref.WindowOpen());
    REQUIRE(engine_ref.RespondWindow("player2", *twin));
    clock.now += 7000;
    session.Tick();
    session.EmitEvents(fake);

    const ecs::MatchMeta* meta = engine_ref.Store().Get<ecs::MatchMeta>(
        engine_ref.Registries().match);
    REQUIRE(meta != nullptr);
    std::size_t played_cards = 0;
    for (const match::view::TailEntry& entry : session.Tail().Entries()) {
        if (entry.retention == Retention::kUntilTurnEnd) {
            CHECK(entry.turn == meta->turn);
        }
        if (entry.engine_event["type"] == "card_played") ++played_cards;
    }
    CHECK(played_cards == 2);

    REQUIRE(session.BindSocket("player2", Socket(2)));
    session.SendSnapshot(fake, Socket(2), "player2", false);
    const json snapshot = LastSnapshot(fake, Socket(2));
    REQUIRE_FALSE(snapshot.is_null());
    std::size_t tail_played = 0;
    for (const json& envelope : snapshot["match_state"]["tail"]) {
        if (envelope["id"] == "card_played") ++tail_played;
        if (envelope["id"] == "turn_advance") {
            CHECK(envelope["turn"] == meta->turn);
        }
    }
    CHECK(tail_played == 2);
}

TEST_CASE("tail: a debt draw reaches the tail without the drawn cards") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, {"player0", "player1", "player2"}, {"uni", "draw_stacking"},
        clock, 42);
    const auto draw_two = FindCard(*engine, "red", "+2");
    const auto filler0 = FindCard(*engine, "blue", "5");
    const auto filler1 = FindCard(*engine, "blue", "6");
    const auto filler2 = FindCard(*engine, "blue", "7");
    REQUIRE((draw_two && filler0 && filler1 && filler2));
    ForceHand(*engine, *engine->FindPlayer("player0"), {*draw_two, *filler0});
    ForceHand(*engine, *engine->FindPlayer("player1"), {*filler1});
    ForceHand(*engine, *engine->FindPlayer("player2"), {*filler2});
    SetActiveColor(*engine, "red");

    MatchInstance& engine_ref = *engine;
    MatchSession session(std::move(engine), std::move(content.mods),
                         {{"player0", Socket(0)},
                          {"player1", nullptr},
                          {"player2", nullptr}});
    FakeBroadcaster fake;
    session.EmitMatchStart(fake);
    REQUIRE(engine_ref.PlayCard("player0", *draw_two));
    bool debt_draw_seen = false;
    for (int round = 0; round < 6 && !debt_draw_seen; ++round) {
        session.EmitEvents(fake);
        clock.now += 7000;
        session.Tick();
        for (const json& event : engine_ref.Events()) {
            if (event.value("type", "") != "cards_drawn") continue;
            debt_draw_seen |= event["payload"].value("from_debt", false);
        }
    }
    REQUIRE(debt_draw_seen);
    session.EmitEvents(fake);

    const std::vector<HiddenCard> victim_cards = [&]() {
        std::vector<HiddenCard> cards;
        const ecs::Hand* hand = engine_ref.Store().Get<ecs::Hand>(
            *engine_ref.FindPlayer("player1"));
        for (ecs::Entity card : hand->cards) {
            cards.push_back(hidden_cards::HiddenCardOf(engine_ref, card));
        }
        return cards;
    }();
    REQUIRE_FALSE(victim_cards.empty());

    const auto drawn_in = [](const json& tail) -> const json* {
        for (const json& envelope : tail) {
            if (envelope["id"] == "cards_drawn") return &envelope;
        }
        return nullptr;
    };

    REQUIRE(session.BindSocket("player2", Socket(2)));
    session.SendSnapshot(fake, Socket(2), "player2", false);
    const json other_seat =
        LastSnapshot(fake, Socket(2))["match_state"]["tail"];
    REQUIRE(drawn_in(other_seat) != nullptr);
    CHECK((*drawn_in(other_seat))["payload"]["count"] > 0);
    CHECK_FALSE((*drawn_in(other_seat))["payload"].contains("cards"));
    CHECK(LeakFindings(other_seat, victim_cards).empty());

    session.SetSeatPrivacy("player1", true);
    session.BindViewer("watcher", Socket(9));
    session.SendSnapshot(fake, Socket(9), "watcher", true);
    const json spectator = LastSnapshot(fake, Socket(9))["match_state"]["tail"];
    REQUIRE(drawn_in(spectator) != nullptr);
    CHECK_FALSE((*drawn_in(spectator))["payload"].contains("cards"));
    CHECK(LeakFindings(spectator, victim_cards).empty());

    session.SetSeatPrivacy("player1", false);
    session.BindViewer("open", Socket(8));
    session.SendSnapshot(fake, Socket(8), "open", true);
    const json open_spectator =
        LastSnapshot(fake, Socket(8))["match_state"]["tail"];
    REQUIRE(drawn_in(open_spectator) != nullptr);
    CHECK((*drawn_in(open_spectator))["payload"].contains("cards"));

    REQUIRE(session.BindSocket("player1", Socket(1)));
    session.SendSnapshot(fake, Socket(1), "player1", false);
    const json owner = LastSnapshot(fake, Socket(1))["match_state"]["tail"];
    REQUIRE(drawn_in(owner) != nullptr);
    CHECK((*drawn_in(owner))["payload"].contains("cards"));
}

namespace {

/** @brief A three seat table whose first draw is a playable colored card. */
struct DrawTable {
    std::unique_ptr<FakeClock> clock = std::make_unique<FakeClock>();
    std::unique_ptr<MatchSession> session;
    FakeBroadcaster fake;
    ecs::Entity drawn{};
};

std::unique_ptr<DrawTable> MakeDrawTable() {
    Content content;
    REQUIRE(LoadContent(content));
    auto table = std::make_unique<DrawTable>();
    std::unique_ptr<MatchInstance> engine = MakeEngine(
        content, {"player0", "player1", "player2"}, {}, *table->clock, 42);
    REQUIRE(ArmPlayableDraw(*engine));
    for (ecs::Entity pile : engine->Store().EntitiesWith<ecs::PileContents>()) {
        const ecs::PileContents* draw =
            engine->Store().Get<ecs::PileContents>(pile);
        if (draw->kind == ecs::PileKind::kDraw) {
            table->drawn = draw->cards.back();
        }
    }
    table->session = std::make_unique<MatchSession>(
        std::move(engine), std::move(content.mods),
        MatchSession::SocketMap{{"player0", Socket(0)},
                                {"player1", nullptr},
                                {"player2", nullptr}});
    table->session->EmitMatchStart(table->fake);
    return table;
}

json TailOf(DrawTable& table, AppWebSocket* socket, const std::string& name,
            bool spectator) {
    table.session->SendSnapshot(table.fake, socket, name, spectator);
    return LastSnapshot(table.fake, socket)["match_state"]["tail"];
}

HiddenCard HiddenOf(const DrawTable& table, ecs::Entity card) {
    return hidden_cards::HiddenCardOf(table.session->Engine(), card);
}

void RejoinSeat(DrawTable& table, const std::string& name, int index) {
    REQUIRE(table.session->BindSocket(name, nullptr));
    REQUIRE(table.session->BindSocket(name, Socket(index)));
}

}  // namespace

TEST_CASE("tail: a drawn card that was played stays out of cards_drawn") {
    std::unique_ptr<DrawTable> table = MakeDrawTable();
    MatchSession& session = *table->session;
    REQUIRE(session.Engine().DrawCard("player0"));
    REQUIRE(session.Engine().PlayCard("player0", table->drawn));
    session.EmitEvents(table->fake);
    const HiddenCard drawn = HiddenOf(*table, table->drawn);

    REQUIRE(session.BindSocket("player1", Socket(1)));
    session.BindViewer("watcher", Socket(9));
    const json seat_tail = TailOf(*table, Socket(1), "player1", false);
    const json watcher_tail = TailOf(*table, Socket(9), "watcher", true);
    for (const json& tail : {seat_tail, watcher_tail}) {
        bool played_is_public = false;
        for (const json& envelope : tail) {
            if (envelope["id"] == "cards_drawn") {
                CHECK_FALSE(envelope["payload"].contains("cards"));
                CHECK(LeakFindings(envelope, {drawn}).empty());
            }
            if (envelope["id"] == "card_played") {
                played_is_public |= !LeakFindings(envelope, {drawn}).empty();
            }
        }
        CHECK(played_is_public);
    }
}

TEST_CASE("tail: a rejoining seat learns no hidden card after draw and keep") {
    std::unique_ptr<DrawTable> table = MakeDrawTable();
    MatchSession& session = *table->session;
    REQUIRE(session.BindSocket("player1", Socket(1)));
    REQUIRE(session.Engine().DrawCard("player0"));
    session.EmitEvents(table->fake);

    RejoinSeat(*table, "player0", 0);
    json tail = TailOf(*table, Socket(0), "player0", false);
    for (const std::string& finding : LeakFindings(
             tail, hidden_cards::HiddenFrom(session.Engine(), "player0"))) {
        FAIL_CHECK("after draw: " << finding);
    }
    tail = TailOf(*table, Socket(1), "player1", false);
    for (const std::string& finding : LeakFindings(
             tail, hidden_cards::HiddenFrom(session.Engine(), "player1"))) {
        FAIL_CHECK("other seat after draw: " << finding);
    }

    REQUIRE(session.KeepDrawn("player0"));
    session.EmitEvents(table->fake);
    RejoinSeat(*table, "player0", 0);
    tail = TailOf(*table, Socket(0), "player0", false);
    for (const std::string& finding : LeakFindings(
             tail, hidden_cards::HiddenFrom(session.Engine(), "player0"))) {
        FAIL_CHECK("after keep: " << finding);
    }
    tail = TailOf(*table, Socket(1), "player1", false);
    CHECK_FALSE(tail.empty());
    for (const std::string& finding : LeakFindings(
             tail, hidden_cards::HiddenFrom(session.Engine(), "player1"))) {
        FAIL_CHECK("other seat after keep: " << finding);
    }
}

TEST_CASE("tail: a card_replaced after a draw names no card identity") {
    std::unique_ptr<DrawTable> table = MakeDrawTable();
    MatchSession& session = *table->session;
    REQUIRE(session.Engine().DrawCard("player0"));
    REQUIRE(session.KeepDrawn("player0"));
    const ecs::Entity player0 = *session.Engine().FindPlayer("player0");
    const json handle = {{"index", player0.index},
                         {"generation", player0.generation}};
    match::view::EventTail tail;
    for (const json& event : session.Engine().Events()) tail.Record(event);
    tail.Record(json{{"type", "card_replaced"},
                     {"payload", json{{"target", handle}, {"zone", "hand"},
                                      {"card", handle}}},
                     {"event_id", 100000},
                     {"causal_frame", json{{"turn", 1}, {"round", 0}}}});

    const HiddenCard kept = HiddenOf(*table, table->drawn);
    const std::vector<match::view::Viewer> viewers = {
        match::view::Viewer::Player("player1"),
        match::view::Viewer::Player("player2"),
        session.SpectatorViewer(), match::view::Viewer::PublicSpectator()};
    for (const match::view::Viewer& viewer : viewers) {
        const json replayed = session.View().BuildTail(viewer, tail);
        bool found = false;
        for (const json& envelope : replayed) {
            if (envelope["id"] != "card_replaced") continue;
            found = true;
            CHECK_FALSE(envelope["payload"].contains("card"));
            CHECK(LeakFindings(envelope, {kept}).empty());
        }
        CHECK(found);
    }
}
