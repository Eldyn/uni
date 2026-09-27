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
 * Assembles a real 2-player vanilla match, scripts play / prompt-answer /
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
            if (deck.deck_id == "vanilla:classic") out.classic = deck;
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
        if (packet.value("type", std::string()) == type) return &packet;
    }
    return nullptr;
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

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "vanilla:wild");
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

    const json* prompt = FindEvent(s0_play, "prompt_open");
    REQUIRE(prompt != nullptr);
    CHECK((*prompt)["payload"]["kind"] == "choose_color");
    CHECK((*prompt)["payload"]["prompt_id"] == "choose_color");
    CHECK((*prompt)["payload"]["response_schema"]["type"] == "string");
    CHECK(FindEvent(s1_play, "prompt_open") == nullptr);

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
    CHECK(FindEvent(s0_after, "prompt_open") == nullptr);
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
    CHECK((*drawn0)["payload"]["player"] == "player1");
    CHECK((*drawn0)["payload"]["count"] == 1);
    CHECK_FALSE((*drawn0)["payload"].contains("cards"));
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
    REQUIRE(FindEvent(s0_end, "match_end") != nullptr);
    REQUIRE(FindEvent(s1_end, "placement") != nullptr);

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

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "vanilla:wild");
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
    REQUIRE(FindEvent(PacketsFor(fake, s0), "prompt_open") != nullptr);

    // --- answer -> the target gets a matching prompt_close ---------------
    REQUIRE(session.SubmitInput("player0", "choose_color", json("red")));
    CHECK_FALSE(session.Engine().PendingInput().has_value());
    fake.Clear();
    session.EmitEvents(fake);
    const std::vector<json> s0_after = PacketsFor(fake, s0);
    const std::vector<json> s1_after = PacketsFor(fake, s1);

    const json* close0 = FindEvent(s0_after, "prompt_close");
    REQUIRE(close0 != nullptr);
    CHECK(close0->value("action", std::string()) == "match_event");
    CHECK((*close0)["payload"]["prompt_id"] == "choose_color");
    CHECK((*close0)["payload"]["outcome"] == "answered");
    CHECK(close0->contains("seq"));

    // INFO: prompt_close is target-only - the opponent never receives it.
    CHECK(FindEvent(s1_after, "prompt_close") == nullptr);
    // INFO: a cleared prompt never re-sends prompt_open either.
    CHECK(FindEvent(s0_after, "prompt_open") == nullptr);
}

TEST_CASE("match session: per-recipient seq persists across batches") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "vanilla:wild");
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
                seq0.push_back(packet.value("seq", 0u));
            }
        }
        for (const json& packet : PacketsFor(fake, s1)) {
            if (packet.value("action", std::string()) == "match_event") {
                seq1.push_back(packet.value("seq", 0u));
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

    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "vanilla:wild");
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
    CHECK(FindEvent(spectator_packets, "prompt_open") == nullptr);

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
                const std::string type = packet.value("type", std::string());
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
        CHECK(packets[defs_idx].value("seq", 0u) == 0u);
        CHECK(packets[start_idx].value("seq", 0u) == 1u);
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

    // INFO: a vanilla kind appears with the face authored in
    //       `mods/vanilla/cards.json`, not a blank placeholder.
    const json* wild = nullptr;
    for (const json& kind : defs["kinds"]) {
        if (kind.value("string_id", std::string()) == "vanilla:wild") {
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
    CHECK(start_payload["deck_id"] == "vanilla:classic");
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
        const std::string type = packet.value("type", std::string());
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
    CHECK(packets.front().value("type", std::string()) == "defs");
    int defs_count = 0;
    for (const json& packet : packets) {
        if (packet.value("action", std::string()) == "match_event"
            && packet.value("type", std::string()) == "defs") {
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
            packet.value("type", "") == type) {
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

