#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/match_session.hpp>
#include <match/view/defs_builder.hpp>

#include "support/fake_broadcaster.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file match_session_test.cpp
 * @brief Headless integration test for `match::server::MatchSession`.
 *
 * Assembles a real 2-player uni match, scripts play / prompt-answer /
 * draw / win through the session surface, and asserts the per-recipient
 * `match_event` / `match_state_updated` / `match_over` wire output plus the
 * visibility rules (a non-owner never sees another hand's identity) and the
 * persistent per-recipient `seq` stream.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using match::engine::AssemblyResult;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadedMod;
using match::modload::LoadResult;
using match::modload::ScanModsDirectory;
using nlohmann::json;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

/* INFO: locate the project root from this file so the test is cwd-independent
 *       (mirrors engine_core_test.cpp). */
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
            if (deck.deck_id == "uni:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (int i = 0; i < players; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
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

/** @brief Replace a player's hand with exactly `cards` (test setup). */
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

uint32_t BitsOf(const MatchInstance& engine, ecs::Entity card) {
    const std::optional<ecs::CompactCardV2> id =
        engine.Registries().CardId(card);
    REQUIRE(id.has_value());
    return id->bits;
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
 * @brief Top the draw pile with a non-wild card and make it playable, so the
 * next voluntary draw parks a `PendingPlayDrawn` choice on the drawer.
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

/** @brief An opaque test socket key; never dereferenced by the fake. */
AppWebSocket* PlayerSocket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x100 + index));
}

std::vector<json> PacketsFor(const FakeBroadcaster& fake,
                             AppWebSocket* socket) {
    std::vector<json> out;
    for (const SentFrame& frame : fake.sent) {
        if (frame.to != socket) continue;
        out.push_back(json::parse(frame.payload));
    }
    return out;
}

const json* FindPacket(const std::vector<json>& packets,
                       const std::string& action) {
    for (const json& packet : packets) {
        if (packet.value("action", std::string()) == action) return &packet;
    }
    return nullptr;
}

const json* FindEvent(const std::vector<json>& packets,
                      const std::string& type) {
    for (const json& packet : packets) {
        if (packet.value("action", std::string()) != "match_event") continue;
        if (packet.value("id", std::string()) == type) return &packet;
    }
    return nullptr;
}

std::size_t CountEvents(const std::vector<json>& packets,
                        const std::string& type) {
    std::size_t count = 0;
    for (const json& packet : packets) {
        if (packet.value("action", std::string()) != "match_event") continue;
        if (packet.value("id", std::string()) == type) ++count;
    }
    return count;
}

/** @brief The snapshot's player row for `username`, or nullptr. */
const json* PlayerRow(const json& snapshot, const std::string& username) {
    const json& players = snapshot["match_state"]["players"];
    for (const json& row : players) {
        if (row.value("username", std::string()) == username) return &row;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("match session: scripted two-player match emits per-recipient wire") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "uni:wild");
    REQUIRE(wilds.size() >= 4);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});
    ForceHand(*engine, player1, {wilds[2], wilds[3]});

    const uint32_t wild0 = BitsOf(*engine, wilds[0]);
    const uint32_t wild1 = BitsOf(*engine, wilds[1]);

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    FakeBroadcaster fake;

    // --- opening snapshot: both recipients get their own view --------------
    session.BroadcastSnapshot(fake);
    const std::vector<json> s0_open = PacketsFor(fake, s0);
    const std::vector<json> s1_open = PacketsFor(fake, s1);
    const json* s0_snapshot = FindPacket(s0_open, "match_state_updated");
    const json* s1_snapshot = FindPacket(s1_open, "match_state_updated");
    REQUIRE(s0_snapshot != nullptr);
    REQUIRE(s1_snapshot != nullptr);
    CHECK((*s0_snapshot)["match_state"]["status"] == "playing");

    // INFO: visibility - the owner sees a hand array, the opponent does not.
    const json* own_row = PlayerRow(*s0_snapshot, "player0");
    const json* other_row = PlayerRow(*s0_snapshot, "player1");
    REQUIRE(own_row != nullptr);
    REQUIRE(other_row != nullptr);
    REQUIRE(own_row->contains("hand"));
    CHECK((*own_row)["hand"].size() == 2);
    CHECK_FALSE(other_row->contains("hand"));
    CHECK((*other_row)["card_count"] == 2);

    // --- play a wild -> colour prompt (target-only) ------------------------
    REQUIRE(session.PlayCard("player0", wild0));
    fake.Clear();
    session.EmitEvents(fake);
    session.BroadcastSnapshot(fake);
    const std::vector<json> s0_play = PacketsFor(fake, s0);
    const std::vector<json> s1_play = PacketsFor(fake, s1);

    const json* card_played = FindEvent(s0_play, "card_played");
    REQUIRE(card_played != nullptr);
    CHECK((*card_played)["payload"]["card"] == wild0);
    CHECK(FindEvent(s1_play, "card_played") != nullptr);

    const json* prompt = FindEvent(s0_play, "prompt_opened");
    REQUIRE(prompt != nullptr);
    CHECK((*prompt)["payload"]["kind"] == "choose_color");
    CHECK((*prompt)["payload"]["prompt_id"] == "choose_color");
    CHECK((*prompt)["payload"]["response_schema"]["type"] == "string");
    CHECK(FindEvent(s1_play, "prompt_opened") == nullptr);

    // --- prompt response validation before SubmitInput ---------------------
    CHECK_FALSE(session.SubmitInput("player1", "choose_color", json("red")));
    CHECK_FALSE(session.SubmitInput("player0", "choose_player", json("red")));
    CHECK_FALSE(session.SubmitInput("player0", "choose_color", json("purple")));
    CHECK_FALSE(session.SubmitInput("player0", "choose_color", json(7)));
    REQUIRE(session.Engine().PendingInput().has_value());

    REQUIRE(session.SubmitInput("player0", "choose_color", json("red")));
    CHECK_FALSE(session.Engine().PendingInput().has_value());
    fake.Clear();
    session.EmitEvents(fake);
    session.BroadcastSnapshot(fake);
    const std::vector<json> s0_after = PacketsFor(fake, s0);
    const std::vector<json> s1_after = PacketsFor(fake, s1);
    CHECK(FindEvent(s0_after, "prompt_opened") == nullptr);
    REQUIRE(FindEvent(s0_after, "turn_advance") != nullptr);
    REQUIRE(FindEvent(s1_after, "turn_advance") != nullptr);
    CHECK(session.Engine().GetCurrentPlayerUsername() == "player1");

    // --- opponent draws: public count, owner identity only -----------------
    REQUIRE(session.DrawCard("player1"));
    fake.Clear();
    session.EmitEvents(fake);
    session.BroadcastSnapshot(fake);
    const std::vector<json> s0_draw = PacketsFor(fake, s0);
    const std::vector<json> s1_draw = PacketsFor(fake, s1);
    const json* drawn0 = FindEvent(s0_draw, "cards_drawn");
    const json* drawn1 = FindEvent(s1_draw, "cards_drawn");
    REQUIRE(drawn0 != nullptr);
    REQUIRE(drawn1 != nullptr);
    CHECK((*drawn0)["payload"]["target"] == "player1");
    CHECK((*drawn0)["payload"]["count"] == 1);
    CHECK_FALSE((*drawn0)["payload"].contains("cards"));

    // INFO: a voluntary playable draw now holds the turn on the drawer; a keep
    //       resolves it and advances. The draw either held (playable) or
    //       advanced (unplayable); either way player0 owns the turn before the
    //       next scripted play.
    if (session.Engine().PendingPlayDrawnState().has_value()) {
        CHECK(session.Engine().GetCurrentPlayerUsername() == "player1");
        REQUIRE(session.KeepDrawn("player1"));
    }
    CHECK(session.Engine().GetCurrentPlayerUsername() == "player0");

    // --- finish: player0's last wild empties the hand and wins -------------
    REQUIRE(session.PlayCard("player0", wild1));
    REQUIRE(session.SubmitInput("player0", "choose_color", json("blue")));
    fake.Clear();
    session.EmitEvents(fake);
    session.BroadcastSnapshot(fake);
    CHECK(session.Engine().IsMatchOver());
    CHECK(session.Engine().GetWinner() == "player0");
    CHECK(session.MatchOverNotified());

    const std::vector<json> s0_end = PacketsFor(fake, s0);
    const std::vector<json> s1_end = PacketsFor(fake, s1);
    const json* over0 = FindPacket(s0_end, "match_over");
    const json* over1 = FindPacket(s1_end, "match_over");
    REQUIRE(over0 != nullptr);
    REQUIRE(over1 != nullptr);
    CHECK((*over0)["winner"] == "player0");
    CHECK((*over1)["winner"] == "player0");
    CHECK((*over0)["placements"][0] == "player0");
    REQUIRE(FindEvent(s0_end, "match_ended") != nullptr);
    REQUIRE(FindEvent(s1_end, "placement_set") != nullptr);

    // INFO: match_over is idempotent - a second broadcast re-sends nothing.
    fake.Clear();
    session.BroadcastSnapshot(fake);
    CHECK(FindPacket(PacketsFor(fake, s0), "match_over") == nullptr);
    CHECK(FindPacket(PacketsFor(fake, s1), "match_over") == nullptr);
}

TEST_CASE("match session: answer closes the prompt for the target only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "uni:wild");
    REQUIRE(wilds.size() >= 4);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});
    ForceHand(*engine, player1, {wilds[2], wilds[3]});
    const uint32_t wild0 = BitsOf(*engine, wilds[0]);

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    FakeBroadcaster fake;

    // INFO: emit the prompt_open first so the session records it per
    //       recipient before the answer arrives (the dedupe signature).
    REQUIRE(session.PlayCard("player0", wild0));
    fake.Clear();
    session.EmitEvents(fake);
    REQUIRE(FindEvent(PacketsFor(fake, s0), "prompt_opened") != nullptr);

    // --- answer -> the target gets a matching prompt_close ---------------
    REQUIRE(session.SubmitInput("player0", "choose_color", json("red")));
    CHECK_FALSE(session.Engine().PendingInput().has_value());
    fake.Clear();
    session.EmitEvents(fake);
    const std::vector<json> s0_after = PacketsFor(fake, s0);
    const std::vector<json> s1_after = PacketsFor(fake, s1);

    const json* close0 = FindEvent(s0_after, "prompt_answered");
    REQUIRE(close0 != nullptr);
    CHECK(close0->value("action", std::string()) == "match_event");
    CHECK((*close0)["payload"]["prompt_id"] == "choose_color");
    CHECK((*close0)["payload"]["outcome"] == "answered");
    CHECK(close0->contains("sequence"));

    // INFO: prompt_close is target-only - the opponent never receives it.
    CHECK(FindEvent(s1_after, "prompt_answered") == nullptr);
    // INFO: a cleared prompt never re-sends prompt_opened either.
    CHECK(FindEvent(s0_after, "prompt_opened") == nullptr);
}

TEST_CASE("match session: a prompt sends prompt_opened and prompt_answered "
          "one time each") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "uni:wild");
    REQUIRE(wilds.size() >= 4);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});
    ForceHand(*engine, player1, {wilds[2], wilds[3]});
    const uint32_t wild0 = BitsOf(*engine, wilds[0]);

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    FakeBroadcaster fake;

    REQUIRE(session.PlayCard("player0", wild0));
    session.EmitEvents(fake);
    REQUIRE(session.SubmitInput("player0", "choose_color", json("red")));
    session.EmitEvents(fake);

    const std::vector<json> s0_packets = PacketsFor(fake, s0);
    const std::vector<json> s1_packets = PacketsFor(fake, s1);
    const json* opened = FindEvent(s0_packets, "prompt_opened");
    REQUIRE(opened != nullptr);
    CHECK(CountEvents(s0_packets, "prompt_opened") == 1);

    const json* answered = FindEvent(s0_packets, "prompt_answered");
    REQUIRE(answered != nullptr);
    CHECK(CountEvents(s0_packets, "prompt_answered") == 1);
    CHECK((*answered)["payload"]["outcome"] == "answered");
    CHECK((*answered)["payload"]["prompt_id"] == "choose_color");

    CHECK(FindEvent(s1_packets, "prompt_opened") == nullptr);
    CHECK(FindEvent(s1_packets, "prompt_answered") == nullptr);

    for (const std::vector<json>* packets : {&s0_packets, &s1_packets}) {
        uint32_t previous = 0;
        bool first = true;
        for (const json& packet : *packets) {
            if (!packet.contains("sequence")) continue;
            const uint32_t sequence = packet["sequence"].get<uint32_t>();
            if (!first) CHECK(sequence == previous + 1);
            previous = sequence;
            first = false;
        }
    }
}

TEST_CASE("match session: KeepDrawn forwards to the engine for the owner only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);
    REQUIRE(engine->GetCurrentPlayerUsername() == "player0");

    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});

    // INFO: a non-owner cannot resolve another seat's parked choice.
    CHECK_FALSE(session.KeepDrawn("player1"));
    CHECK(session.Engine().PendingPlayDrawnState().has_value());

    // INFO: the owner's keep clears the choice and advances the turn.
    CHECK(session.KeepDrawn("player0"));
    CHECK_FALSE(session.Engine().PendingPlayDrawnState().has_value());
    CHECK(session.Engine().GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("match session: per-recipient seq persists across batches") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "uni:wild");
    REQUIRE(wilds.size() >= 4);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});
    ForceHand(*engine, player1, {wilds[2], wilds[3]});
    const uint32_t wild0 = BitsOf(*engine, wilds[0]);

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    FakeBroadcaster fake;

    std::vector<uint32_t> seq0;
    std::vector<uint32_t> seq1;
    auto drain = [&]() {
        fake.Clear();
        session.EmitEvents(fake);
        session.BroadcastSnapshot(fake);
        for (const json& packet : PacketsFor(fake, s0)) {
            if (packet.value("action", std::string()) == "match_event") {
                seq0.push_back(packet.value("sequence", 0u));
            }
        }
        for (const json& packet : PacketsFor(fake, s1)) {
            if (packet.value("action", std::string()) == "match_event") {
                seq1.push_back(packet.value("sequence", 0u));
            }
        }
    };

    drain();
    REQUIRE(session.PlayCard("player0", wild0));
    drain();
    REQUIRE(session.SubmitInput("player0", "choose_color", json("red")));
    drain();
    REQUIRE(session.DrawCard("player1"));
    drain();

    // INFO: the stream never resets between batches and never reuses a seq.
    REQUIRE(seq0.size() >= 4);
    REQUIRE(seq1.size() >= 3);
    for (std::size_t i = 1; i < seq0.size(); ++i) {
        CHECK(seq0[i] > seq0[i - 1]);
    }
    for (std::size_t i = 1; i < seq1.size(); ++i) {
        CHECK(seq1[i] > seq1[i - 1]);
    }
    CHECK(seq0.front() == 0);
    CHECK(seq1.front() == 0);
}

TEST_CASE("match session: a bound spectator keeps receiving live updates") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "uni:wild");
    REQUIRE(wilds.size() >= 4);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});
    ForceHand(*engine, player1, {wilds[2], wilds[3]});
    const uint32_t wild0 = BitsOf(*engine, wilds[0]);

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});

    // INFO: A mid-match spectator registers a viewer socket and
    //       must receive every subsequent `match_event` / snapshot, not just
    //       the one snapshot taken at join time.
    AppWebSocket* spectator = PlayerSocket(9);
    session.BindViewer("watcher", spectator);
    REQUIRE(session.Viewers().count("watcher") == 1);
    REQUIRE(session.Viewers().at("watcher") == spectator);

    FakeBroadcaster fake;
    session.SendSnapshot(fake, spectator, "watcher", /*is_spectator=*/true);
    CHECK(FindPacket(PacketsFor(fake, spectator), "match_state_updated")
          != nullptr);

    fake.Clear();
    REQUIRE(session.PlayCard("player0", wild0));
    session.EmitEvents(fake);
    session.BroadcastSnapshot(fake);
    const std::vector<json> spectator_packets = PacketsFor(fake, spectator);
    const json* played = FindEvent(spectator_packets, "card_played");
    REQUIRE(played != nullptr);
    CHECK((*played)["payload"]["card"] == wild0);
    // INFO: the spectator view is omniscient, so it sees the owner's hand.
    const json* snapshot = FindPacket(spectator_packets, "match_state_updated");
    REQUIRE(snapshot != nullptr);

    // INFO: a spectator is never a prompt target, so it sees no prompt_open.
    CHECK(FindEvent(spectator_packets, "prompt_opened") == nullptr);

    // INFO: unbinding drops the viewer from the live stream entirely.
    REQUIRE(session.UnbindViewer("watcher"));
    CHECK(session.Viewers().count("watcher") == 0);
    fake.Clear();
    session.BroadcastSnapshot(fake);
    CHECK(FindPacket(PacketsFor(fake, spectator), "match_state_updated")
          == nullptr);
}

TEST_CASE("match session: an armed turn deadline reaches the snapshot") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    FakeBroadcaster fake;

    // INFO: C2 - the controller arms the engine deadline on every turn start;
    //       the snapshot must carry that absolute epoch-ms value.
    const int64_t now = session.Engine().Timers().Turn().Now();
    REQUIRE(session.ArmTurnTimer(15'000));

    session.BroadcastSnapshot(fake);
    const std::vector<json> packets = PacketsFor(fake, s0);
    const json* snapshot = FindPacket(packets, "match_state_updated");
    REQUIRE(snapshot != nullptr);
    const int64_t deadline =
        (*snapshot)["match_state"].value("turn_deadline_ms", int64_t{0});
    CHECK(deadline > now);
    CHECK(deadline <= now + 15'000);
}

// ---------------------------------------------------------------------------
// The tests `defs` + `match_start` are published once, before any other
// packet, through each seated recipient's persistent stream.
// ---------------------------------------------------------------------------

TEST_CASE("match session: defs and match_start precede the first snapshot") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    FakeBroadcaster fake;

    session.EmitMatchStart(fake);
    session.EmitEvents(fake);
    session.BroadcastSnapshot(fake);

    for (AppWebSocket* sock : {s0, s1}) {
        const std::vector<json> packets = PacketsFor(fake, sock);
        const std::size_t npos = packets.size();
        std::size_t defs_idx = npos;
        std::size_t start_idx = npos;
        std::size_t snap_idx = npos;
        int defs_count = 0;
        int start_count = 0;
        std::size_t match_events = 0;
        for (std::size_t i = 0; i < packets.size(); ++i) {
            const json& packet = packets[i];
            const std::string action =
                packet.value("action", std::string());
            if (action == "match_event") {
                ++match_events;
                const std::string type = packet.value("id", std::string());
                if (type == "defs") {
                    ++defs_count;
                    if (defs_idx == npos) defs_idx = i;
                } else if (type == "match_start") {
                    ++start_count;
                    if (start_idx == npos) start_idx = i;
                }
            } else if (action == "match_state_updated") {
                if (snap_idx == npos) snap_idx = i;
            }
        }

        REQUIRE(defs_count == 1);
        REQUIRE(start_count == 1);
        REQUIRE(snap_idx != npos);
        CHECK(defs_idx < start_idx);
        CHECK(start_idx < snap_idx);
        // INFO: the persistent stream starts at seq 0 and never renumbers.
        CHECK(packets[defs_idx].value("sequence", 0u) == 0u);
        CHECK(packets[start_idx].value("sequence", 0u) == 1u);
        // INFO: the snapshot watermark reconciles with the packets already
        //       emitted into this recipient's sink.
        CHECK(packets[snap_idx]["match_state"].value("seq_watermark", 0u)
              == match_events);
    }
}

TEST_CASE("match session: defs carries the frozen mods and kind table") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    FakeBroadcaster fake;

    session.EmitMatchStart(fake);

    // INFO: `defs` is all-visibility, so both recipients see the same payload.
    const std::vector<json> p0 = PacketsFor(fake, s0);
    const std::vector<json> p1 = PacketsFor(fake, s1);
    const json* defs0 = FindEvent(p0, "defs");
    const json* defs1 = FindEvent(p1, "defs");
    REQUIRE(defs0 != nullptr);
    REQUIRE(defs1 != nullptr);
    CHECK((*defs0)["payload"] == (*defs1)["payload"]);

    const json& defs = (*defs0)["payload"];
    const match::engine::MatchRegistries& regs = session.Engine().Registries();

    REQUIRE(defs["mods"].is_array());
    REQUIRE(defs["mods"].size() == regs.mods.size());
    for (std::size_t i = 0; i < regs.mods.size(); ++i) {
        CHECK(defs["mods"][i]["id"] == regs.mods[i].id);
        CHECK(defs["mods"][i]["index"] == static_cast<int>(i));
    }

    // INFO: kinds are grouped by mod in frozen order; `index` is the kind's
    //       `kind_index` within its owning mod.
    REQUIRE(defs["kinds"].is_array());
    std::size_t cursor = 0;
    for (std::size_t mi = 0; mi < regs.kinds_by_mod.size(); ++mi) {
        for (std::size_t ki = 0; ki < regs.kinds_by_mod[mi].size(); ++ki) {
            const json& kind = defs["kinds"][cursor];
            CHECK(kind["index"] == static_cast<int>(ki));
            CHECK(kind["string_id"] == regs.kinds_by_mod[mi][ki]);
            CHECK(kind["face"].is_object());
            CHECK(kind["tags"].is_array());
            ++cursor;
        }
    }
    CHECK(cursor == defs["kinds"].size());

    // INFO: a uni kind appears with the face authored in
    //       `mods/uni/cards.json`, not a blank placeholder.
    const json* wild = nullptr;
    for (const json& kind : defs["kinds"]) {
        if (kind.value("string_id", std::string()) == "uni:wild") {
            wild = &kind;
        }
    }
    REQUIRE(wild != nullptr);
    CHECK((*wild)["face"]["kind"] == "text");
    CHECK((*wild)["face"]["color"] == "white");
    CHECK((*wild)["face"]["label"] == "jolly");

    CHECK(defs["defs_digest"] == match::view::DefsBuilder::Digest(defs));
    CHECK(defs["defs_digest"].get<std::string>().size() == 16);
}

TEST_CASE("match session: match_start carries the retained deck snapshot") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    AppWebSocket* s0 = PlayerSocket(0);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods), {{"player0", s0}});
    FakeBroadcaster fake;

    session.EmitMatchStart(fake);

    const std::vector<json> packets = PacketsFor(fake, s0);
    const json* defs = FindEvent(packets, "defs");
    const json* start = FindEvent(packets, "match_start");
    REQUIRE(defs != nullptr);
    REQUIRE(start != nullptr);

    const json& start_payload = (*start)["payload"];
    CHECK(start_payload["deck_id"] == "uni:classic");
    CHECK(start_payload["deck_name"] == "Classic");
    REQUIRE(start_payload["settings"].is_object());
    CHECK_FALSE(start_payload["settings"].empty());
    // INFO: both packets advertise the same content digest.
    CHECK(start_payload["defs_digest"]
          == (*defs)["payload"]["defs_digest"]);
    CHECK(start_payload["mods"] == (*defs)["payload"]["mods"]);
}

TEST_CASE("match session: a second EmitMatchStart does not re-send") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    AppWebSocket* s0 = PlayerSocket(0);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods), {{"player0", s0}});
    FakeBroadcaster fake;

    session.EmitMatchStart(fake);
    session.EmitMatchStart(fake);

    int defs_count = 0;
    int start_count = 0;
    for (const json& packet : PacketsFor(fake, s0)) {
        if (packet.value("action", std::string()) != "match_event") continue;
        const std::string type = packet.value("id", std::string());
        if (type == "defs") ++defs_count;
        if (type == "match_start") ++start_count;
    }
    CHECK(defs_count == 1);
    CHECK(start_count == 1);
}

TEST_CASE("match session: a spectator bound after start receives defs") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    AppWebSocket* s0 = PlayerSocket(0);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods), {{"player0", s0}});
    FakeBroadcaster fake;
    session.EmitMatchStart(fake);

    AppWebSocket* spectator = PlayerSocket(9);
    session.BindViewer("watcher", spectator);
    // INFO: the real spectator join path binds then snapshots the socket.
    session.SendSnapshot(fake, spectator, "watcher", /*is_spectator=*/true);
    session.EmitEvents(fake);
    session.BroadcastSnapshot(fake);

    const std::vector<json> packets = PacketsFor(fake, spectator);
    const json* defs = FindEvent(packets, "defs");
    REQUIRE(defs != nullptr);
    // INFO: a mid-match spectator gets the kind table exactly once, before
    //       any event; it does not need `match_start` (no deck UI).
    CHECK(packets.front().value("id", std::string()) == "defs");
    int defs_count = 0;
    for (const json& packet : packets) {
        if (packet.value("action", std::string()) == "match_event"
            && packet.value("id", std::string()) == "defs") {
            ++defs_count;
        }
    }
    CHECK(defs_count == 1);
    CHECK(FindEvent(packets, "match_start") == nullptr);

    // INFO: the digest matches the seated copy, so the face cache key agrees.
    const std::vector<json> seated_packets = PacketsFor(fake, s0);
    const json* seated_defs = FindEvent(seated_packets, "defs");
    REQUIRE(seated_defs != nullptr);
    CHECK((*defs)["payload"]["defs_digest"]
          == (*seated_defs)["payload"]["defs_digest"]);
}

namespace {

std::vector<json> EventsOfType(const FakeBroadcaster& broadcaster,
                               AppWebSocket* socket, const std::string& type) {
    std::vector<json> out;
    for (const SentFrame& frame : broadcaster.FramesFor(socket)) {
        const json packet = json::parse(frame.payload, nullptr, false);
        if (packet.is_object() && packet.value("action", "") == "match_event" &&
            packet.value("id", "") == type) {
            out.push_back(packet);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("MatchSession ready barrier: open by default") {
    Content content;
    REQUIRE(LoadContent(content));
    match::server::MatchSession session(
        MakeEngine(content, 2, 1), content.mods,
        {{"player0", PlayerSocket(1)}, {"player1", PlayerSocket(2)}});
    CHECK(session.ReadyBarrierOpen());
}

TEST_CASE("MatchSession ready barrier: begin broadcasts players_ready") {
    Content content;
    REQUIRE(LoadContent(content));
    match::server::MatchSession session(
        MakeEngine(content, 3, 1), content.mods,
        {{"player0", PlayerSocket(1)}, {"player1", PlayerSocket(2)}});
    FakeBroadcaster broadcaster;
    session.BeginReadyBarrier(broadcaster);

    CHECK_FALSE(session.ReadyBarrierOpen());
    const std::vector<json> packets =
        EventsOfType(broadcaster, PlayerSocket(1), "players_ready");
    REQUIRE(packets.size() == 1);
    CHECK(packets[0]["payload"]["ready"] == 1);   // player2 has no socket (bot)
    CHECK(packets[0]["payload"]["total"] == 3);
}

TEST_CASE(
    "MatchSession ready barrier: all humans ready completes, open sends "
    "match_begin once") {
    Content content;
    REQUIRE(LoadContent(content));
    match::server::MatchSession session(
        MakeEngine(content, 2, 1), content.mods,
        {{"player0", PlayerSocket(1)}, {"player1", PlayerSocket(2)}});
    FakeBroadcaster broadcaster;
    session.BeginReadyBarrier(broadcaster);

    CHECK(session.MarkSeatReady("player0", broadcaster));
    CHECK_FALSE(session.ReadyBarrierComplete());
    CHECK_FALSE(session.MarkSeatReady("player0", broadcaster));
    CHECK(session.MarkSeatReady("player1", broadcaster));
    CHECK(session.ReadyBarrierComplete());

    CHECK(session.OpenReadyBarrier(broadcaster));
    CHECK_FALSE(session.OpenReadyBarrier(broadcaster));
    CHECK(EventsOfType(broadcaster, PlayerSocket(1), "match_begin").size() == 1);
    CHECK(EventsOfType(broadcaster, PlayerSocket(2), "match_begin").size() == 1);
}

TEST_CASE("MatchSession ready barrier: rebind to bot marks the old seat ready") {
    Content content;
    REQUIRE(LoadContent(content));
    match::server::MatchSession session(
        MakeEngine(content, 2, 1), content.mods,
        {{"player0", PlayerSocket(1)}, {"player1", PlayerSocket(2)}});
    FakeBroadcaster broadcaster;
    session.BeginReadyBarrier(broadcaster);
    session.MarkSeatReady("player0", broadcaster);

    REQUIRE(session.HandSeatToBot("player1", "Bot Ada"));
    CHECK(session.ReadyBarrierComplete());
}

TEST_CASE("MatchSession ready barrier: snapshot while closed carries players_ready") {
    Content content;
    REQUIRE(LoadContent(content));
    match::server::MatchSession session(
        MakeEngine(content, 2, 1), content.mods,
        {{"player0", PlayerSocket(1)}, {"player1", PlayerSocket(2)}});
    FakeBroadcaster broadcaster;
    session.BeginReadyBarrier(broadcaster);
    broadcaster.Clear();

    session.SendSnapshot(broadcaster, PlayerSocket(1), "player0", false);
    CHECK(EventsOfType(broadcaster, PlayerSocket(1), "players_ready").size() == 1);
    CHECK(EventsOfType(broadcaster, PlayerSocket(1), "match_begin").empty());
}

TEST_CASE("MatchSession ready barrier: snapshot after open carries match_begin") {
    Content content;
    REQUIRE(LoadContent(content));
    match::server::MatchSession session(
        MakeEngine(content, 2, 1), content.mods,
        {{"player0", PlayerSocket(1)}, {"player1", PlayerSocket(2)}});
    FakeBroadcaster broadcaster;
    session.BeginReadyBarrier(broadcaster);
    session.OpenReadyBarrier(broadcaster);
    broadcaster.Clear();

    session.SendSnapshot(broadcaster, PlayerSocket(1), "player0", false);
    CHECK(EventsOfType(broadcaster, PlayerSocket(1), "match_begin").size() == 1);
}


TEST_CASE("MatchSession ready barrier: players_ready reports the time left") {
    Content content;
    REQUIRE(LoadContent(content));
    match::server::MatchSession session(
        MakeEngine(content, 2, 1), content.mods,
        {{"player0", PlayerSocket(1)}, {"player1", PlayerSocket(2)}});
    FakeBroadcaster broadcaster;
    session.BeginReadyBarrier(broadcaster, 15000);

    const std::vector<json> begun =
        EventsOfType(broadcaster, PlayerSocket(1), "players_ready");
    REQUIRE(begun.size() == 1);
    CHECK(begun[0]["payload"]["timeout_ms"].get<int64_t>() <= 15000);
    CHECK(begun[0]["payload"]["timeout_ms"].get<int64_t>() > 14000);

    broadcaster.Clear();
    session.SendSnapshot(broadcaster, PlayerSocket(1), "player0", false);
    const std::vector<json> resent =
        EventsOfType(broadcaster, PlayerSocket(1), "players_ready");
    REQUIRE(resent.size() == 1);
    CHECK(resent[0]["payload"]["timeout_ms"].get<int64_t>() > 0);
}

TEST_CASE("MatchSession ready barrier: no timeout omits timeout_ms") {
    Content content;
    REQUIRE(LoadContent(content));
    match::server::MatchSession session(
        MakeEngine(content, 2, 1), content.mods,
        {{"player0", PlayerSocket(1)}, {"player1", PlayerSocket(2)}});
    FakeBroadcaster broadcaster;
    session.BeginReadyBarrier(broadcaster);
    const std::vector<json> packets =
        EventsOfType(broadcaster, PlayerSocket(1), "players_ready");
    REQUIRE(packets.size() == 1);
    CHECK_FALSE(packets[0]["payload"].contains("timeout_ms"));
}

namespace {

struct TwoSeatSession {
    std::unique_ptr<match::server::MatchSession> session;
    AppWebSocket* socket0 = PlayerSocket(0);
    AppWebSocket* socket1 = PlayerSocket(1);
    uint32_t wild0 = 0;
    uint32_t wild2 = 0;
};

TwoSeatSession MakeTwoSeatSession() {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);
    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "uni:wild");
    REQUIRE(wilds.size() >= 4);
    ForceHand(*engine, *engine->FindPlayer("player0"), {wilds[0], wilds[1]});
    ForceHand(*engine, *engine->FindPlayer("player1"), {wilds[2], wilds[3]});

    TwoSeatSession result;
    result.wild0 = BitsOf(*engine, wilds[0]);
    result.wild2 = BitsOf(*engine, wilds[2]);
    result.session = std::make_unique<match::server::MatchSession>(
        std::move(engine), std::move(content.mods),
        match::server::MatchSession::SocketMap{{"player0", result.socket0},
                                               {"player1", result.socket1}});
    return result;
}

std::vector<json> MatchEventsFor(const FakeBroadcaster& fake,
                                 AppWebSocket* socket) {
    std::vector<json> events;
    for (const json& packet : PacketsFor(fake, socket)) {
        if (packet.value("action", std::string()) == "match_event") {
            events.push_back(packet);
        }
    }
    return events;
}

}  // namespace

TEST_CASE("match session: a seat that misses events gets its own sequence") {
    TwoSeatSession seats = MakeTwoSeatSession();
    FakeBroadcaster fake;

    REQUIRE_FALSE(seats.session->PlayCard("player1", seats.wild2));
    REQUIRE(seats.session->PlayCard("player0", seats.wild0));
    seats.session->EmitEvents(fake);

    const std::vector<json> events0 = MatchEventsFor(fake, seats.socket0);
    const std::vector<json> events1 = MatchEventsFor(fake, seats.socket1);
    const json* played0 = FindEvent(events0, "card_played");
    const json* played1 = FindEvent(events1, "card_played");
    REQUIRE(played0 != nullptr);
    REQUIRE(played1 != nullptr);
    CHECK(FindEvent(events0, "play_rejected") == nullptr);
    CHECK(FindEvent(events1, "play_rejected") != nullptr);
    CHECK((*played0)["sequence"] != (*played1)["sequence"]);
    CHECK((*played0)["actor"] == "player0");
}

TEST_CASE("match session: a one seat event leaves no gap for the other seat") {
    TwoSeatSession seats = MakeTwoSeatSession();
    FakeBroadcaster fake;

    REQUIRE_FALSE(seats.session->PlayCard("player1", seats.wild2));
    REQUIRE(seats.session->PlayCard("player0", seats.wild0));
    seats.session->EmitEvents(fake);

    for (AppWebSocket* socket : {seats.socket0, seats.socket1}) {
        const std::vector<json> events = MatchEventsFor(fake, socket);
        REQUIRE_FALSE(events.empty());
        for (std::size_t index = 0; index < events.size(); ++index) {
            CHECK(events[index]["sequence"] == index);
        }
    }
}

TEST_CASE("match session: a prompt takes its sequence from the seat counter") {
    TwoSeatSession seats = MakeTwoSeatSession();
    FakeBroadcaster fake;

    REQUIRE(seats.session->PlayCard("player0", seats.wild0));
    seats.session->EmitEvents(fake);

    const std::vector<json> events0 = MatchEventsFor(fake, seats.socket0);
    const json* prompt = FindEvent(events0, "prompt_opened");
    REQUIRE(prompt != nullptr);
    CHECK((*prompt)["sequence"] == events0.size() - 1);
    CHECK((*prompt)["id"] == "prompt_opened");
    CHECK(events0.back()["id"] == "prompt_opened");
    CHECK(events0.back()["sequence"] == events0.size() - 1);
}

TEST_CASE("match session: a seat that reconnects with a prompt pending keeps "
          "one prompt and a gap-free sequence") {
    TwoSeatSession seats = MakeTwoSeatSession();
    FakeBroadcaster fake;

    REQUIRE(seats.session->PlayCard("player0", seats.wild0));
    seats.session->EmitEvents(fake);
    const std::size_t first_count =
        MatchEventsFor(fake, seats.socket0).size();
    REQUIRE(first_count > 0);

    REQUIRE(seats.session->BindSocket("player0", nullptr));
    REQUIRE(seats.session->BindSocket("player0", seats.socket0));
    fake.Clear();
    seats.session->EmitEvents(fake);
    // INFO: the prompt is already open for this seat. A reconnect sends no
    //       second prompt packet; the snapshot carries it.
    const std::vector<json> quiet = MatchEventsFor(fake, seats.socket0);
    CHECK(FindEvent(quiet, "prompt_opened") == nullptr);

    REQUIRE(seats.session->SubmitInput("player0", "choose_color",
                                       json("red")));
    fake.Clear();
    seats.session->EmitEvents(fake);
    const std::vector<json> answered = MatchEventsFor(fake, seats.socket0);
    REQUIRE(FindEvent(answered, "prompt_answered") != nullptr);
    CHECK(answered.front()["sequence"] == first_count);
    for (std::size_t index = 0; index < answered.size(); ++index) {
        CHECK(answered[index]["sequence"] == first_count + index);
    }
}

TEST_CASE("match session: a snapshot request is answered with a watermark") {
    TwoSeatSession seats = MakeTwoSeatSession();
    FakeBroadcaster fake;
    REQUIRE(seats.session->PlayCard("player0", seats.wild0));
    seats.session->EmitEvents(fake);
    const std::size_t events_before =
        MatchEventsFor(fake, seats.socket1).size();
    fake.Clear();

    CHECK(seats.session->HandleSnapshotRequest(
        fake, "player1", match::view::SnapshotOptions{}));

    const std::vector<json> packets = PacketsFor(fake, seats.socket1);
    const json* snapshot = FindPacket(packets, "match_state_updated");
    REQUIRE(snapshot != nullptr);
    CHECK((*snapshot)["match_state"]["seq_watermark"] == events_before);
    CHECK(PacketsFor(fake, seats.socket0).empty());
}

TEST_CASE("match session: a flood of snapshot requests is limited") {
    TwoSeatSession seats = MakeTwoSeatSession();
    seats.session->SetSnapshotRequestIntervalMs(60'000);
    FakeBroadcaster fake;

    CHECK(seats.session->HandleSnapshotRequest(
        fake, "player1", match::view::SnapshotOptions{}));
    const std::size_t sent_after_first = fake.sent.size();
    for (int attempt = 0; attempt < 20; ++attempt) {
        CHECK_FALSE(seats.session->HandleSnapshotRequest(
            fake, "player1", match::view::SnapshotOptions{}));
    }
    CHECK(fake.sent.size() == sent_after_first);
    CHECK(seats.session->HandleSnapshotRequest(
        fake, "player0", match::view::SnapshotOptions{}));
}

TEST_CASE("match session: an unknown recipient gets no snapshot") {
    TwoSeatSession seats = MakeTwoSeatSession();
    FakeBroadcaster fake;

    CHECK_FALSE(seats.session->HandleSnapshotRequest(
        fake, "stranger", match::view::SnapshotOptions{}));
    CHECK(fake.sent.empty());
}

TEST_CASE("match session: RenameSeat preserves flags and moves seat keying") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);
    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});

    // INFO: a human rename keeps the live socket and the human flags.
    REQUIRE(session.RenameSeat("player0", "Alice"));
    CHECK(session.Engine().FindPlayer("player0") == std::nullopt);
    const std::optional<ecs::Entity> alice = session.Engine().FindPlayer("Alice");
    REQUIRE(alice.has_value());
    const ecs::PlayerInfo* alice_info =
        session.Engine().Store().Get<ecs::PlayerInfo>(*alice);
    REQUIRE(alice_info != nullptr);
    CHECK_FALSE(alice_info->is_bot);
    CHECK(alice_info->connected);
    CHECK(session.Sockets().count("Alice") == 1);
    CHECK(session.Sockets().at("Alice") == s0);
    CHECK(session.Sockets().count("player0") == 0);

    // INFO: a bot rename must preserve is_bot (RebindPlayer would clear it).
    session.SetDirector("player1", true);
    REQUIRE(session.HandSeatToBot("player1", "BotBob"));
    REQUIRE(session.RenameSeat("BotBob", "BotZed"));
    CHECK(session.Engine().FindPlayer("BotBob") == std::nullopt);
    const std::optional<ecs::Entity> zed = session.Engine().FindPlayer("BotZed");
    REQUIRE(zed.has_value());
    const ecs::PlayerInfo* zed_info =
        session.Engine().Store().Get<ecs::PlayerInfo>(*zed);
    REQUIRE(zed_info != nullptr);
    CHECK(zed_info->is_bot);
    CHECK(session.Sockets().count("BotZed") == 1);
    CHECK(session.Sockets().count("BotBob") == 0);
    // INFO: director x-ray follows the seat, not the old name.
    CHECK(session.IsDirector("BotZed"));
    CHECK_FALSE(session.IsDirector("BotBob"));

    // INFO: the stream/prompt bookkeeping moved: the new key answers, the
    //       old key is unknown (Alice kept the live socket, so it answers).
    FakeBroadcaster fake;
    CHECK(session.HandleSnapshotRequest(
        fake, "Alice", match::view::SnapshotOptions{}));
    CHECK_FALSE(session.HandleSnapshotRequest(
        fake, "player0", match::view::SnapshotOptions{}));

    // INFO: rejects empty / same / duplicate / unknown seats.
    CHECK_FALSE(session.RenameSeat("BotZed", "Alice"));
    CHECK_FALSE(session.RenameSeat("BotZed", ""));
    CHECK_FALSE(session.RenameSeat("", "BotNew"));
    CHECK_FALSE(session.RenameSeat("BotZed", "BotZed"));
    CHECK_FALSE(session.RenameSeat("Ghost", "BotNew"));
    CHECK(session.Engine().FindPlayer("BotZed").has_value());
}

TEST_CASE("match session: SetSeatSkin stores display overrides and the "
          "snapshot carries skin") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);
    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});

    CHECK(session.SetSeatSkin("player0", "player"));
    CHECK(session.SetSeatSkin("player1", "bot"));
    CHECK_FALSE(session.SetSeatSkin("player1", "alien"));
    CHECK_FALSE(session.SetSeatSkin("player1", ""));
    CHECK_FALSE(session.SetSeatSkin("Ghost", "bot"));
    CHECK(session.SeatSkin("player0") == "player");
    CHECK(session.SeatSkin("player1") == "bot");
    CHECK(session.SeatSkin("Ghost").empty());

    // INFO: the override follows a rename.
    REQUIRE(session.RenameSeat("player0", "Alice"));
    CHECK(session.SeatSkin("Alice") == "player");
    CHECK(session.SeatSkin("player0").empty());

    FakeBroadcaster fake;
    session.BroadcastSnapshot(fake);
    const std::vector<json> packets = PacketsFor(fake, s0);
    const json* snapshot = FindPacket(packets, "match_state_updated");
    REQUIRE(snapshot != nullptr);
    const json* alice_row = PlayerRow(*snapshot, "Alice");
    const json* other_row = PlayerRow(*snapshot, "player1");
    REQUIRE(alice_row != nullptr);
    REQUIRE(other_row != nullptr);
    CHECK(alice_row->value("skin", std::string()) == "player");
    CHECK(other_row->value("skin", std::string()) == "bot");
    // INFO: display-only: a bot-strip override on a human seat never flips
    //       the turn-logic flag it mirrors.
    CHECK(other_row->value("is_bot", true) == false);
    const std::optional<ecs::Entity> other =
        session.Engine().FindPlayer("player1");
    REQUIRE(other.has_value());
    const ecs::PlayerInfo* info =
        session.Engine().Store().Get<ecs::PlayerInfo>(*other);
    REQUIRE(info != nullptr);
    CHECK_FALSE(info->is_bot);
}

TEST_CASE("match session: same-seat HandSeatToBot and RebindPlayer flip the "
          "bot flag in place") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);
    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});

    // INFO: to-bot keeps the seat name, drops the socket, flags the bot.
    REQUIRE(session.HandSeatToBot("player0", "player0"));
    const std::optional<ecs::Entity> seat0 = session.Engine().FindPlayer("player0");
    REQUIRE(seat0.has_value());
    const ecs::PlayerInfo* bot_info =
        session.Engine().Store().Get<ecs::PlayerInfo>(*seat0);
    REQUIRE(bot_info != nullptr);
    CHECK(bot_info->is_bot);
    CHECK(session.Sockets().at("player0") == nullptr);

    // INFO: back to human keeps the seat (hand/turn state) and reconnects
    //       the flags, the dedicated rename path's job is names only.
    REQUIRE(session.RebindPlayer("player0", "player0", nullptr));
    const ecs::PlayerInfo* human_info =
        session.Engine().Store().Get<ecs::PlayerInfo>(*seat0);
    REQUIRE(human_info != nullptr);
    CHECK_FALSE(human_info->is_bot);
    CHECK(human_info->connected);

    CHECK_FALSE(session.HandSeatToBot("Ghost", "Ghost"));
    CHECK_FALSE(session.RebindPlayer("Ghost", "Ghost", nullptr));
}
