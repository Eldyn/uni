#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/match_session.hpp>
#include <match/view/wire.hpp>

#include "support/fake_broadcaster.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

/**
 * @file broadcast_test.cpp
 * @brief Backend-perf Task 9: shared broadcast serialization.
 *
 * `BroadcastMatchOver` must serialize the terminal frame ONCE into a shared
 * buffer and send the same bytes to all N recipients (seats + spectators),
 * instead of paying one dump per recipient. The observable contract: N
 * sends, exactly 1 distinct payload.
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

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

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
AppWebSocket* TestSocket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x500 + index));
}

}  // namespace

TEST_CASE("match_over serializes once for N recipients") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 42);

    // Same scripted win as match_session_test: two wilds each, player0
    // empties the hand second and takes the match.
    const std::vector<ecs::Entity> wilds = CardsByKind(*engine, "uni:wild");
    REQUIRE(wilds.size() >= 4);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});
    ForceHand(*engine, player1, {wilds[2], wilds[3]});
    const uint32_t wild0 = BitsOf(*engine, wilds[0]);
    const uint32_t wild1 = BitsOf(*engine, wilds[1]);

    AppWebSocket* s0 = TestSocket(0);
    AppWebSocket* s1 = TestSocket(1);
    AppWebSocket* v0 = TestSocket(2);
    AppWebSocket* v1 = TestSocket(3);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    session.BindViewer("spec0", v0);
    session.BindViewer("spec1", v1);
    FakeBroadcaster fake;

    REQUIRE(session.PlayCard("player0", wild0));
    REQUIRE(session.SubmitInput("player0", "choose_color", json("red")));
    REQUIRE(session.DrawCard("player1"));
    if (session.Engine().PendingPlayDrawnState().has_value()) {
        REQUIRE(session.KeepDrawn("player1"));
    }
    REQUIRE(session.PlayCard("player0", wild1));
    REQUIRE(session.SubmitInput("player0", "choose_color", json("blue")));
    session.EmitEvents(fake);
    REQUIRE(session.Engine().IsMatchOver());

    fake.Clear();
    CHECK(session.BroadcastMatchOver(fake));

    // 2 seats + 2 spectators: 4 sends of byte-identical frames.
    CHECK(fake.sent.size() == 4);
    std::set<std::string> distinct;
    for (const SentFrame& frame : fake.sent) {
        distinct.insert(frame.payload);
    }
    CHECK(distinct.size() == 1);

    const json over = json::parse(*distinct.begin());
    CHECK(over.value("action", std::string()) == "match_over");
    CHECK(over.value("winner", std::string()) == "player0");

    // Idempotent: a second broadcast re-sends nothing.
    CHECK(session.BroadcastMatchOver(fake));
    CHECK(fake.sent.size() == 4);
}

/** @brief Test double that drops the first Send (reports backpressure). */
struct DropFirstBroadcaster : FakeBroadcaster {
    bool drop_armed = true;

    bool Send(AppWebSocket* socket, const std::string& msg,
              uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true) override {
        if (drop_armed) {
            drop_armed = false;
            return false;
        }
        FakeBroadcaster::Send(socket, msg, op, compress);
        return true;
    }
};

TEST_CASE("match_over dropped under backpressure is resent, not lost") {
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
    const uint32_t wild1 = BitsOf(*engine, wilds[1]);

    AppWebSocket* s0 = TestSocket(0);
    AppWebSocket* s1 = TestSocket(1);
    AppWebSocket* v0 = TestSocket(2);
    AppWebSocket* v1 = TestSocket(3);
    match::server::MatchSession session(
        std::move(engine), std::move(content.mods),
        {{"player0", s0}, {"player1", s1}});
    session.BindViewer("spec0", v0);
    session.BindViewer("spec1", v1);
    FakeBroadcaster play;

    REQUIRE(session.PlayCard("player0", wild0));
    REQUIRE(session.SubmitInput("player0", "choose_color", json("red")));
    REQUIRE(session.DrawCard("player1"));
    if (session.Engine().PendingPlayDrawnState().has_value()) {
        REQUIRE(session.KeepDrawn("player1"));
    }
    REQUIRE(session.PlayCard("player0", wild1));
    REQUIRE(session.SubmitInput("player0", "choose_color", json("blue")));
    session.EmitEvents(play);
    REQUIRE(session.Engine().IsMatchOver());

    // First broadcast loses one recipient to backpressure: the one-shot
    // must NOT latch as delivered.
    DropFirstBroadcaster bus;
    CHECK(!session.BroadcastMatchOver(bus));
    CHECK(!session.MatchOverNotified());

    // Retry delivers to everyone and latches; a further call is a no-op.
    CHECK(session.BroadcastMatchOver(bus));
    CHECK(session.MatchOverNotified());
    for (AppWebSocket* sock : {s0, s1, v0, v1}) {
        bool got_over = false;
        for (const SentFrame& frame : bus.FramesFor(sock)) {
            const json packet = json::parse(frame.payload);
            if (packet.value("action", std::string()) == "match_over") {
                got_over = true;
                CHECK(packet.value("winner", std::string()) == "player0");
            }
        }
        CHECK(got_over);
    }
    const std::size_t sent = bus.sent.size();
    CHECK(session.BroadcastMatchOver(bus));
    CHECK(bus.sent.size() == sent);
}

TEST_CASE("match_over WritePacket reuses scratch capacity") {
    match::view::MatchOverWire over{"match_over", "player0",
                                    {"player0", "player1"}};
    std::string scratch;
    scratch.reserve(512);
    REQUIRE(match::view::WritePacket(over, scratch));
    const std::string first = scratch;
    const std::size_t capacity = scratch.capacity();
    REQUIRE(match::view::WritePacket(over, scratch));
    CHECK(scratch == first);
    // Glaze appends into the cleared buffer: no reallocation on reuse.
    CHECK(scratch.capacity() == capacity);
}
