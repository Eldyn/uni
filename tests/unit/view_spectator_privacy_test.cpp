#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/server/match_session.hpp>
#include <match/view/event_projector.hpp>
#include <match/view/view_builder.hpp>

#include "support/fake_broadcaster.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file view_spectator_privacy_test.cpp
 * @brief The spectator follows the Streamer/Privacy Mode of each seat owner.
 *
 * A seat whose owner has the mode off shows its hidden cards to a spectator.
 * A seat whose owner has the mode on shows card backs. The draw pile has no
 * owner, so its order never reaches a spectator. A seat that lost the match
 * becomes a spectator and gets the same projection.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using match::engine::AssemblyResult;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadedMod;
using match::modload::LoadResult;
using match::modload::ScanModsDirectory;
using match::server::MatchSession;
using match::view::SeatRoles;
using match::view::ViewBuilder;
using match::view::Viewer;
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

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < players; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

AppWebSocket* FakeSocket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x500 + index));
}

ecs::Entity FirstHandCard(const MatchInstance& engine,
                          const std::string& username) {
    return engine.Store()
        .Get<ecs::Hand>(*engine.FindPlayer(username))
        ->cards.front();
}

ecs::Entity FirstDrawPileCard(const MatchInstance& engine) {
    return engine.Store()
        .Get<ecs::PileContents>(engine.Registries().draw_pile)
        ->cards.front();
}

uint32_t BitsOf(const MatchInstance& engine, ecs::Entity card) {
    return engine.Registries().CardId(card)->bits;
}

const json* FindPlayer(const json& snapshot, const std::string& username) {
    for (const json& player : snapshot["match_state"]["players"]) {
        if (player["username"] == username) return &player;
    }
    return nullptr;
}

std::vector<json> PacketsTo(const FakeBroadcaster& fake,
                            AppWebSocket* socket) {
    std::vector<json> packets;
    for (const SentFrame& frame : fake.FramesFor(socket)) {
        packets.push_back(json::parse(frame.payload));
    }
    return packets;
}

const json* LastSnapshot(const std::vector<json>& packets) {
    const json* found = nullptr;
    for (const json& packet : packets) {
        if (packet.value("action", std::string()) == "match_state_updated") {
            found = &packet;
        }
    }
    return found;
}

std::unique_ptr<MatchSession> MakeSession(Content& content,
                                          std::vector<std::string> seats) {
    MatchSession::SocketMap sockets;
    for (std::size_t index = 0; index < seats.size(); ++index) {
        sockets[seats[index]] = FakeSocket(static_cast<int>(index));
    }
    return std::make_unique<MatchSession>(
        MakeEngine(content, static_cast<int>(seats.size())),
        std::move(content.mods), std::move(sockets));
}

}  // namespace

TEST_CASE("spectator privacy: a hand card is hidden only for a private seat") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4);
    const ecs::Entity open_card = FirstHandCard(*engine, "player0");
    const ecs::Entity private_card = FirstHandCard(*engine, "player1");

    const Viewer spectator = Viewer::Spectator({"player1"});
    CHECK_FALSE(match::view::IsCardHiddenFrom(*engine, open_card, spectator));
    CHECK(match::view::IsCardHiddenFrom(*engine, private_card, spectator));
    CHECK_FALSE(match::view::IsCardHiddenFrom(*engine, private_card,
                                              Viewer::Spectator()));
}

TEST_CASE("spectator privacy: a hand without an owner record stays hidden") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4);
    const ecs::Entity card = FirstHandCard(*engine, "player0");
    const ecs::Entity not_a_player = engine->Registries().draw_pile;
    engine->Store().Get<ecs::InZone>(card)->zone =
        ecs::ZoneRef{ecs::ZoneKind::kHand, not_a_player};

    CHECK(match::view::IsCardHiddenFrom(*engine, card, Viewer::Spectator()));
}

TEST_CASE("spectator privacy: the public spectator sees no owner field") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4);
    const json wire = json{
        {"player", "player1"},
        {"cards", json::array(
                      {BitsOf(*engine, FirstHandCard(*engine, "player1"))})}};
    SeatRoles roles;
    roles.owner = "player1";

    const Viewer viewer = Viewer::PublicSpectator();
    CHECK(viewer.is_spectator);
    CHECK(viewer.SeesCardBacksOf("player0"));
    CHECK_FALSE(match::view::ProjectForViewer(*engine, "cards_drawn", wire,
                                              roles, viewer)
                    .contains("cards"));
}

TEST_CASE("spectator privacy: the draw pile is hidden for every spectator") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4);
    const ecs::Entity pile_card = FirstDrawPileCard(*engine);

    CHECK(match::view::IsCardHiddenFrom(*engine, pile_card,
                                        Viewer::Spectator()));
    CHECK(match::view::IsCardHiddenFrom(*engine, pile_card,
                                        Viewer::Spectator({"player1"})));
}

TEST_CASE("spectator privacy: the projector follows the owner of the hand") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4);
    const json wire = json{
        {"player", "player1"},
        {"cards", json::array(
                      {BitsOf(*engine, FirstHandCard(*engine, "player1"))})}};
    SeatRoles roles;
    roles.owner = "player1";

    const auto cards_for = [&](const Viewer& viewer) {
        return match::view::ProjectForViewer(*engine, "cards_drawn", wire,
                                             roles, viewer);
    };
    CHECK(cards_for(Viewer::Spectator()).contains("cards"));
    CHECK(cards_for(Viewer::Spectator({"player2"})).contains("cards"));
    CHECK(cards_for(Viewer::Spectator({"player1"}))["cards"].empty());
    CHECK_FALSE(cards_for(Viewer::Player("player2")).contains("cards"));
    CHECK(cards_for(Viewer::Player("player1")).contains("cards"));
}

TEST_CASE("spectator privacy: the snapshot follows each seat owner") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4);
    ViewBuilder builder(*engine, content.mods);
    match::view::EventSink sink;

    const json snapshot = builder.BuildSnapshot(
        Viewer::Spectator({"player1", "player3"}), sink);

    CHECK(FindPlayer(snapshot, "player0")->contains("hand"));
    CHECK(FindPlayer(snapshot, "player2")->contains("hand"));
    CHECK_FALSE(FindPlayer(snapshot, "player1")->contains("hand"));
    CHECK_FALSE(FindPlayer(snapshot, "player3")->contains("hand"));
    CHECK((*FindPlayer(snapshot, "player1"))["card_count"] == 7);
}

TEST_CASE("spectator privacy: the snapshot never lists the draw pile") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4);
    ViewBuilder builder(*engine, content.mods);
    match::view::EventSink sink;

    const json snapshot = builder.BuildSnapshot(Viewer::Spectator(), sink);

    const json& draw_pile = snapshot["match_state"]["draw_pile"];
    CHECK(draw_pile["count"].get<int>() > 0);
    CHECK_FALSE(draw_pile.contains("cards"));
}

TEST_CASE("spectator privacy: a joining spectator gets the current setting") {
    Content content;
    REQUIRE(LoadContent(content));
    const std::unique_ptr<MatchSession> session_owner =
        MakeSession(content, {"player0", "player1", "player2", "player3"});
    MatchSession& session = *session_owner;
    session.SetSeatPrivacy("player1", true);
    AppWebSocket* socket = FakeSocket(9);
    session.BindViewer("watcher", socket);

    FakeBroadcaster fake;
    session.SendSnapshot(fake, socket, "watcher", /*is_spectator=*/true);

    const std::vector<json> packets = PacketsTo(fake, socket);
    const json* snapshot = LastSnapshot(packets);
    REQUIRE(snapshot != nullptr);
    CHECK(FindPlayer(*snapshot, "player0")->contains("hand"));
    CHECK_FALSE(FindPlayer(*snapshot, "player1")->contains("hand"));
}

TEST_CASE("spectator privacy: a change reaches a watching spectator") {
    Content content;
    REQUIRE(LoadContent(content));
    const std::unique_ptr<MatchSession> session_owner =
        MakeSession(content, {"player0", "player1", "player2", "player3"});
    MatchSession& session = *session_owner;
    AppWebSocket* socket = FakeSocket(9);
    session.BindViewer("watcher", socket);
    FakeBroadcaster fake;

    const auto player0_hand_visible = [&]() {
        fake.Clear();
        session.BroadcastSnapshot(fake);
        const std::vector<json> packets = PacketsTo(fake, socket);
        const json* snapshot = LastSnapshot(packets);
        REQUIRE(snapshot != nullptr);
        return FindPlayer(*snapshot, "player0")->contains("hand");
    };
    CHECK(player0_hand_visible());
    session.SetSeatPrivacy("player0", true);
    CHECK_FALSE(player0_hand_visible());
    session.SetSeatPrivacy("player0", false);
    CHECK(player0_hand_visible());
}

TEST_CASE("spectator privacy: the setting of another seat does not matter") {
    Content content;
    REQUIRE(LoadContent(content));
    const std::unique_ptr<MatchSession> session_owner =
        MakeSession(content, {"player0", "player1", "player2", "player3"});
    MatchSession& session = *session_owner;
    session.SetSeatPrivacy("player1", true);
    session.SetSeatPrivacy("player2", true);

    const Viewer viewer = session.SpectatorViewer();
    CHECK_FALSE(viewer.SeesCardBacksOf("player0"));
    CHECK(viewer.SeesCardBacksOf("player1"));
    CHECK(viewer.SeesCardBacksOf("player2"));
    CHECK_FALSE(viewer.SeesCardBacksOf("player3"));
}

TEST_CASE("spectator privacy: a demoted seat gets the spectator projection") {
    Content content;
    REQUIRE(LoadContent(content));
    const std::unique_ptr<MatchSession> session_owner =
        MakeSession(content, {"player0", "player1", "player2", "player3"});
    MatchSession& session = *session_owner;
    session.SetSeatPrivacy("player1", true);
    AppWebSocket* demoted_socket = FakeSocket(0);
    FakeBroadcaster fake;
    session.BroadcastSnapshot(fake);
    const std::vector<json> before_packets = PacketsTo(fake, demoted_socket);
    const json before = *LastSnapshot(before_packets);
    REQUIRE(FindPlayer(before, "player0")->contains("hand"));

    REQUIRE(session.DemoteSeatToSpectator("player0"));
    CHECK(session.Sockets().count("player0") == 0);
    CHECK(session.Viewers().count("player0") == 1);

    fake.Clear();
    session.BroadcastSnapshot(fake);
    const std::vector<json> after_packets = PacketsTo(fake, demoted_socket);
    const json* after = LastSnapshot(after_packets);
    REQUIRE(after != nullptr);
    CHECK(FindPlayer(*after, "player2")->contains("hand"));
    CHECK_FALSE(FindPlayer(*after, "player1")->contains("hand"));
    CHECK_FALSE((*after)["match_state"]["draw_pile"].contains("cards"));
    CHECK((*after)["match_state"]["seq_watermark"]
          >= before["match_state"]["seq_watermark"]);
}

TEST_CASE("spectator privacy: demoting an unknown seat does nothing") {
    Content content;
    REQUIRE(LoadContent(content));
    const std::unique_ptr<MatchSession> session_owner =
        MakeSession(content, {"player0", "player1"});
    MatchSession& session = *session_owner;
    CHECK_FALSE(session.DemoteSeatToSpectator("nobody"));
}
