#include <doctest/doctest.h>
#include <action_router.hpp>
#include <controllers/lobby_controller.hpp>
#include <controllers/match_controller.hpp>
#include <common/payloads.hpp>
#include <common/ws.hpp>
#include <database.hpp>
#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/match_session.hpp>
#include <match/server/stats_gate.hpp>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "support/fake_broadcaster.hpp"
#include "support/fake_timer_service.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/* INFO: locate the project root from this file so deck tests can scan the
 *       shipped `mods/` tree regardless of the test's working directory
 *       (mirrors engine_core_test.cpp). */
static std::string ProjectModsRoot() {
    std::filesystem::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(p / "contract" / "schemas", ec)) {
            return (p / "mods").string();
        }
        std::filesystem::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return "mods";
}

/* INFO: every loaded mod of the shipped tree, or empty when unavailable. */
static std::vector<match::modload::LoadedMod> ShippedMods() {
    auto loaded = match::modload::ScanModsDirectory(ProjectModsRoot());
    if (!loaded.ok()) return {};
    return std::move(loaded.mods);
}

// uWS's WebSocket::getUserData() is pure pointer arithmetic:
//   (char*)this + sizeof(us_socket_t) + sizeof(WebSocketData)
// so a fake socket must sit exactly that far *before* its PerSocketData for
// getUserData() to hand the controller the real object. Returning &sd directly
// (the old approach) made RemoveMember write lobby_code.clear() past the end of
// the struct — silent stack corruption that eventually segfaulted.
static std::size_t UserDataOffset() {
    static const std::size_t offset = [] {
        auto* probe = reinterpret_cast<AppWebSocket*>(0x1000);
        return reinterpret_cast<std::uintptr_t>(probe->getUserData())
             - static_cast<std::uintptr_t>(0x1000);
    }();
    return offset;
}

static AppWebSocket* fake_sock(PerSocketData& sd) {
    return reinterpret_cast<AppWebSocket*>(
        reinterpret_cast<std::byte*>(&sd) - UserDataOffset());
}

static WsContext make_ctx(AppWebSocket* sock, PerSocketData* sd) {
    return WsContext{sock, sd, uWS::OpCode::TEXT};
}

static json create_msg(bool is_public = false, const std::string& req = "req-1") {
    return {{"action", ws::ClientAction::kLobbyCreate},
            {"request_id", req},
            {"is_public", is_public}};
}

static json join_msg(const std::string& code, const std::string& req = "req-2") {
    return {{"action", ws::ClientAction::kLobbyJoin},
            {"request_id", req},
            {"code", code}};
}

static json leave_msg(const std::string& req = "req-3") {
    return {{"action", ws::ClientAction::kLobbyLeave}, {"request_id", req}};
}

static json quick_join_msg(const std::string& req = "req-qj") {
    return {{"action", ws::ClientAction::kLobbyQuickJoin}, {"request_id", req}};
}

static json list_msg(const std::string& req = "req-4") {
    return {{"action", ws::ClientAction::kLobbyList}, {"request_id", req}};
}

static json start_msg(const std::string& req = "req-5") {
    return {{"action", ws::ClientAction::kLobbyStartMatch}, {"request_id", req}};
}

static json toggle_ready_msg(const std::string& req = "req-9") {
    return {{"action", ws::ClientAction::kLobbyToggleReady}, {"request_id", req}};
}

static json kick_msg(const std::string& target, const std::string& req = "req-6") {
    return {{"action", ws::ClientAction::kLobbyKick},
            {"request_id", req},
            {"username", target}};
}

static json promote_msg(const std::string& target, const std::string& req = "req-7") {
    return {{"action", ws::ClientAction::kLobbyPromote},
            {"request_id", req},
            {"username", target}};
}

// ---------------------------------------------------------------------------
// Fixture: fresh router/bus/timers/controller per test case.
// ---------------------------------------------------------------------------
struct LobbyFixture {
    ActionRouter     router;
    FakeBroadcaster  bus;
    FakeTimerService timers;
    PresenceRegistry presence;
    std::string      mods_root;
    LobbyController  lobby;

    PerSocketData alice_sd, bob_sd;
    AppWebSocket* alice_sock;
    AppWebSocket* bob_sock;

    explicit LobbyFixture(std::string root = "")
        : mods_root(std::move(root)),
          lobby(router, bus, timers, presence, nullptr, mods_root) {
        alice_sd.username = "alice";
        bob_sd.username   = "bob";
        alice_sock        = fake_sock(alice_sd);
        bob_sock          = fake_sock(bob_sd);
    }

    WsContext actx() { return make_ctx(alice_sock, &alice_sd); }
    WsContext bctx() { return make_ctx(bob_sock,   &bob_sd);   }

    std::string alice_creates(bool is_public = false) {
        router.Dispatch(actx(), create_msg(is_public));
        auto frames = bus.FramesFor(alice_sock);
        REQUIRE(!frames.empty());
        auto resp = json::parse(frames.back().payload);
        REQUIRE(resp.contains("lobby"));
        std::string code = resp["lobby"]["invite_code"].get<std::string>();
        alice_sd.lobby_code = code;
        bus.Clear();
        return code;
    }

    void bob_joins(const std::string& code) {
        router.Dispatch(bctx(), join_msg(code));
        bob_sd.lobby_code = code;
        bus.Clear();
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
TEST_SUITE("LobbyController") {

TEST_CASE("create: response contains invite_code") {
    LobbyFixture f;
    f.router.Dispatch(f.actx(), create_msg());
    auto frames = f.bus.FramesFor(f.alice_sock);
    REQUIRE(!frames.empty());
    auto resp = json::parse(frames.back().payload);
    CHECK(resp.contains("lobby"));
    CHECK(!resp["lobby"]["invite_code"].get<std::string>().empty());
}

TEST_CASE("create: host is alice") {
    LobbyFixture f;
    f.router.Dispatch(f.actx(), create_msg());
    auto resp = json::parse(f.bus.FramesFor(f.alice_sock).back().payload);
    CHECK(resp["lobby"]["host"].get<std::string>() == "alice");
}

TEST_CASE("create: alice subscribes to lobby topic") {
    LobbyFixture f;
    f.router.Dispatch(f.actx(), create_msg());
    auto resp = json::parse(f.bus.FramesFor(f.alice_sock).back().payload);
    std::string code = resp["lobby"]["invite_code"].get<std::string>();
    CHECK(f.bus.subscriptions.count({f.alice_sock, "lobby_" + code}) == 1);
}

TEST_CASE("create: already-in-lobby returns error") {
    LobbyFixture f;
    f.alice_sd.lobby_code = "XXXXXX";
    f.bus.Clear();
    f.router.Dispatch(f.actx(), create_msg());
    auto resp = json::parse(f.bus.FramesFor(f.alice_sock).back().payload);
    CHECK(resp.value("action", "") == "error");
}

TEST_CASE("join: bob joins alice's lobby") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.router.Dispatch(f.bctx(), join_msg(code));
    auto resp = json::parse(f.bus.FramesFor(f.bob_sock).back().payload);
    CHECK(resp.contains("lobby"));
    CHECK(resp["lobby"]["invite_code"].get<std::string>() == code);
}

TEST_CASE("join: non-existent code returns error") {
    LobbyFixture f;
    f.router.Dispatch(f.actx(), join_msg("ZZZZZZ"));
    auto resp = json::parse(f.bus.FramesFor(f.alice_sock).back().payload);
    CHECK(resp.value("action", "") == "error");
}

TEST_CASE("quick_join: joins the fullest open public lobby") {
    LobbyFixture f;
    std::string small_code = f.alice_creates(/*is_public=*/true);

    PerSocketData carol_sd;
    carol_sd.username = "carol";
    AppWebSocket* carol_sock = fake_sock(carol_sd);
    WsContext cctx = make_ctx(carol_sock, &carol_sd);

    f.bus.Clear();
    f.router.Dispatch(cctx, quick_join_msg());

    Lobby* lp = f.lobby.GetLobbyByCode(small_code);
    REQUIRE(lp);
    bool carol_present = false;
    for (const auto& m : lp->members) {
        if (m.username == "carol") carol_present = true;
    }
    CHECK(carol_present);
}

TEST_CASE("quick_join: selects a bot-filled lobby when bot takeover is enabled") {
    LobbyFixture f;
    std::string code = f.alice_creates(/*is_public=*/true);

    f.router.Dispatch(f.actx(), json{{"action", ws::ClientAction::kLobbyUpdateSettings},
                                      {"request_id", "req-set"},
                                      {"max_players", 2},
                                      {"bot_count", 1},
                                      {"allow_bot_takeover", true}});
    f.bus.Clear();

    PerSocketData carol_sd;
    carol_sd.username = "carol";
    AppWebSocket* carol_sock = fake_sock(carol_sd);
    WsContext cctx = make_ctx(carol_sock, &carol_sd);

    f.router.Dispatch(cctx, quick_join_msg());

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    bool carol_present = false;
    for (const auto& m : lp->members) {
        if (m.username == "carol") carol_present = true;
    }
    CHECK(carol_present);
}

TEST_CASE("quick_join: errors when no public lobby is open") {
    LobbyFixture f;
    f.alice_creates(/*is_public=*/false);

    PerSocketData carol_sd;
    carol_sd.username = "carol";
    AppWebSocket* carol_sock = fake_sock(carol_sd);
    WsContext cctx = make_ctx(carol_sock, &carol_sd);

    f.router.Dispatch(cctx, quick_join_msg());

    auto resp = json::parse(f.bus.FramesFor(carol_sock).back().payload);
    CHECK(resp.value("action", "") == "error");
    CHECK(resp.value("code", "") == "lobby_not_found");
}

TEST_CASE("leave: solo creator destroys lobby") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.lobby.OnClose(f.alice_sock, &f.alice_sd);
    f.router.Dispatch(f.actx(), leave_msg());
    CHECK(f.lobby.GetLobbyByCode(code) == nullptr);
}

TEST_CASE("list: returns public lobby") {
    LobbyFixture f;
    f.alice_creates(true);

    PerSocketData charlie_sd; charlie_sd.username = "charlie";
    auto* charlie = fake_sock(charlie_sd);
    f.router.Dispatch(make_ctx(charlie, &charlie_sd), list_msg());
    auto resp = json::parse(f.bus.FramesFor(charlie).back().payload);
    CHECK(resp.contains("lobbies"));
    CHECK(resp["lobbies"].size() >= 1);
}

TEST_CASE("start: not-host returns error") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.router.Dispatch(f.bctx(), start_msg());
    auto resp = json::parse(f.bus.FramesFor(f.bob_sock).back().payload);
    CHECK(resp.value("action", "") == "error");
    CHECK(resp.value("code", "") == "not_host");
}

TEST_CASE("toggle_ready: flips the caller's own ready state") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.bus.Clear();

    f.router.Dispatch(f.bctx(), toggle_ready_msg());

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    bool bob_ready = false;
    for (const auto& m : lp->members) {
        if (m.username == "bob") bob_ready = m.is_ready;
    }
    CHECK(bob_ready);
}

TEST_CASE("toggle_ready: a second toggle flips it back") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);

    f.router.Dispatch(f.bctx(), toggle_ready_msg());
    f.router.Dispatch(f.bctx(), toggle_ready_msg());

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    bool bob_ready = true;
    for (const auto& m : lp->members) {
        if (m.username == "bob") bob_ready = m.is_ready;
    }
    CHECK_FALSE(bob_ready);
}

TEST_CASE("start: refuses when a human member is not ready") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.bus.Clear();

    // Alice (host) readies up; bob never does.
    f.router.Dispatch(f.actx(), toggle_ready_msg());
    f.bus.Clear();

    f.router.Dispatch(f.actx(), start_msg());

    auto resp = json::parse(f.bus.FramesFor(f.alice_sock).back().payload);
    CHECK(resp.value("action", "") == "error");
    CHECK(resp.value("code", "") == "not_enough_ready");
}

TEST_CASE("start: succeeds once every human member is ready") {
    LobbyFixture f{ProjectModsRoot()};
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.bus.Clear();

    f.router.Dispatch(f.actx(), toggle_ready_msg());
    f.router.Dispatch(f.bctx(), toggle_ready_msg());
    f.bus.Clear();

    f.router.Dispatch(f.actx(), start_msg());

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->session != nullptr);
}

// ---------------------------------------------------------------------------
// a disconnect must unbind the live session's socket, or the
// session keeps a freed `AppWebSocket*` and sends through it once the seat is
// bot-driven.
// ---------------------------------------------------------------------------

TEST_CASE("close: disconnecting a seated player unbinds the live session socket") {
    LobbyFixture f{ProjectModsRoot()};
    std::string code = f.alice_creates();
    f.bob_joins(code);

    f.router.Dispatch(f.actx(), toggle_ready_msg());
    f.router.Dispatch(f.bctx(), toggle_ready_msg());
    f.bus.Clear();
    f.router.Dispatch(f.actx(), start_msg());

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    REQUIRE(lp->session != nullptr);
    // INFO: the seated sockets were bound at match start.
    REQUIRE(lp->session->Sockets().count("alice") == 1);
    CHECK(lp->session->Sockets().at("alice") == f.alice_sock);

    f.lobby.OnClose(f.alice_sock, &f.alice_sd);

    // INFO: C1 - the member socket is nulled AND the session entry is nulled,
    //       so BroadcastSnapshot/EmitEvents skip the dead pointer.
    REQUIRE(lp->session != nullptr);
    REQUIRE(lp->session->Sockets().count("alice") == 1);
    CHECK(lp->session->Sockets().at("alice") == nullptr);
    CHECK(lp->session->Sockets().at("bob") == f.bob_sock);
}

TEST_CASE("close: a spectator is unbound from the live session viewer map") {
    LobbyFixture f{ProjectModsRoot()};
    std::string code = f.alice_creates();
    f.bob_joins(code);

    f.router.Dispatch(f.actx(), toggle_ready_msg());
    f.router.Dispatch(f.bctx(), toggle_ready_msg());
    f.bus.Clear();
    f.router.Dispatch(f.actx(), start_msg());

    // A mid-match spectator joins while the session is live.
    PerSocketData carol_sd;
    carol_sd.username = "carol";
    AppWebSocket* carol_sock = fake_sock(carol_sd);
    f.router.Dispatch(make_ctx(carol_sock, &carol_sd), join_msg(code));

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    REQUIRE(lp->session != nullptr);
    REQUIRE(lp->session->Viewers().count("carol") == 1);
    CHECK(lp->session->Viewers().at("carol") == carol_sock);

    f.lobby.OnClose(carol_sock, &carol_sd);

    // INFO: /I2 - a disconnected spectator is dropped entirely
    //       rather than left holding a freed socket.
    CHECK(lp->session->Viewers().count("carol") == 0);
}

TEST_CASE("start: clears is_spectator left over from a prior elimination for seated members") {
    LobbyFixture f{ProjectModsRoot()};
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.bus.Clear();

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);

    // Simulate bob having been eliminated mid-match last round: he kept his
    // seat but match_controller.cpp flagged him as a spectator.
    for (auto& m : lp->members) {
        if (m.username == "bob") m.is_spectator = true;
    }

    // Simulate a voluntary spectator who joined mid-match with no seat; they
    // must stay a spectator after the flag reset.
    lp->members.emplace_back("carol", nullptr, true, false, -1, /*is_spectator=*/true, false);
    lp->members.back().is_ready = true;

    f.router.Dispatch(f.actx(), toggle_ready_msg());
    f.router.Dispatch(f.bctx(), toggle_ready_msg());
    f.bus.Clear();

    f.router.Dispatch(f.actx(), start_msg());

    bool bob_spectator = true;
    bool carol_spectator = false;
    for (const auto& m : lp->members) {
        if (m.username == "bob") bob_spectator = m.is_spectator;
        if (m.username == "carol") carol_spectator = m.is_spectator;
    }
    CHECK_FALSE(bob_spectator);
    CHECK(carol_spectator);
}

TEST_CASE("start: broadcasts the cleared is_spectator so clients drop the stale spectator view") {
    LobbyFixture f{ProjectModsRoot()};
    std::string code = f.alice_creates();
    f.bob_joins(code);

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    for (auto& m : lp->members) {
        if (m.username == "bob") m.is_spectator = true;
    }

    f.router.Dispatch(f.actx(), toggle_ready_msg());
    f.router.Dispatch(f.bctx(), toggle_ready_msg());
    f.bus.Clear();

    f.router.Dispatch(f.actx(), start_msg());

    bool bob_broadcast_seated = false;
    for (const auto& [topic, payload] : f.bus.published) {
        if (topic != "lobby_" + code) continue;
        auto frame = json::parse(payload);
        if (frame.value("action", "") != "lobby_updated") continue;
        for (const auto& m : frame["lobby"]["members"]) {
            if (m.value("username", "") == "bob") {
                bob_broadcast_seated = !m.value("is_spectator", true);
            }
        }
    }
    CHECK(bob_broadcast_seated);
}

TEST_CASE("kick: host can kick bob") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    // Simulate bob disconnect so RemoveMember skips getUserData().
    f.lobby.OnClose(f.bob_sock, &f.bob_sd);
    f.bus.Clear();
    f.router.Dispatch(f.actx(), kick_msg("bob"));
    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    bool bob_present = false;
    for (const auto& m : lp->members)
        if (m.username == "bob") { bob_present = true; break; }
    CHECK_FALSE(bob_present);
}

TEST_CASE("promote: host can promote bob") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.bus.Clear();
    f.router.Dispatch(f.actx(), promote_msg("bob"));
    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->host == "bob");
}

TEST_CASE("eviction: AFK member removed after grace expires") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.lobby.OnClose(f.alice_sock, &f.alice_sd);

    // Push disconnected_at far into the past to exceed the grace window.
    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    for (auto& m : lp->members) {
        if (m.username == "alice")
            m.disconnected_at = std::chrono::steady_clock::now() - std::chrono::hours(1);
    }

    f.timers.Fire("lobby_eviction");

    lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    bool alice_present = false;
    for (const auto& m : lp->members)
        if (m.username == "alice") { alice_present = true; break; }
    CHECK_FALSE(alice_present);
}

// ---------------------------------------------------------------------------
// RemoveMember edge cases
// ---------------------------------------------------------------------------

TEST_CASE("remove: leave destroys lobby when only bots remain") {
    LobbyFixture f;
    std::string code = f.alice_creates();

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    lp->members.emplace_back("Bot1", nullptr, true, true);

    f.lobby.OnClose(f.alice_sock, &f.alice_sd);
    f.router.Dispatch(f.actx(), leave_msg());

    CHECK(f.lobby.GetLobbyByCode(code) == nullptr);
}

TEST_CASE("kick: unsubscribes a still-connected target from the lobby topic") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.bus.Clear();

    f.router.Dispatch(f.actx(), kick_msg("bob"));

    CHECK(f.bus.subscriptions.count({f.bob_sock, "lobby_" + code}) == 0);
}

TEST_CASE("kick: removes the targeted bot without LIFO-collateral when bot_count drifted") {
    LobbyFixture f;
    std::string code = f.alice_creates(/*is_public=*/false);

    f.router.Dispatch(f.actx(), json{{"action", ws::ClientAction::kLobbyUpdateSettings},
                                      {"request_id", "req-set"},
                                      {"max_players", 4},
                                      {"bot_count", 3}});
    f.bus.Clear();

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    std::vector<std::string> bots;
    for (const auto& m : lp->members)
        if (m.is_bot) bots.push_back(m.username);
    REQUIRE_EQ(bots.size(), 3);

    // Model the pre-fix client "X" path: it lowered the configured bot target
    // without removing a member, leaving settings out of sync with reality.
    // A later SyncBots would have LIFO-erased the last bot, not the intended one.
    lp->settings.bot_count = 2;

    const std::string target = bots[0];
    f.router.Dispatch(f.actx(), kick_msg(target));

    lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->FindMember(target) == nullptr);
    CHECK(lp->FindMember(bots[1]) != nullptr);
    CHECK(lp->FindMember(bots[2]) != nullptr);

    int bot_members = 0;
    for (const auto& m : lp->members)
        if (m.is_bot) ++bot_members;
    CHECK_EQ(bot_members, 2);
    CHECK_EQ(lp->settings.bot_count, 2);
}

// ---------------------------------------------------------------------------
// Host succession, duplicated between HandleLeave and the eviction callback.
// ---------------------------------------------------------------------------

TEST_CASE("leave: host leaving passes host to next connected member") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.bus.Clear();

    f.router.Dispatch(f.actx(), leave_msg());

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->host == "bob");
}

TEST_CASE("leave: host leaving with only a disconnected member left keeps a stale host") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.lobby.OnClose(f.bob_sock, &f.bob_sd);
    f.bus.Clear();

    f.router.Dispatch(f.actx(), leave_msg());

    // No connected non-bot member exists to take over, so HandleLeave's
    // succession loop finds nothing, the host field is left pointing at
    // alice even though she is no longer a member of the lobby.
    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->host == "alice");
}

TEST_CASE("eviction: host succession when the evicted member was host") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.lobby.OnClose(f.alice_sock, &f.alice_sd);

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    for (auto& m : lp->members) {
        if (m.username == "alice")
            m.disconnected_at = std::chrono::steady_clock::now() - std::chrono::hours(1);
    }

    f.timers.Fire("lobby_eviction");

    lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->host == "bob");
}

TEST_CASE("eviction: evicted host leaves a stale host when nobody else is connected") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.lobby.OnClose(f.alice_sock, &f.alice_sd);
    f.lobby.OnClose(f.bob_sock, &f.bob_sd);

    // Only alice (the host) is past the grace window; bob stays disconnected
    // but within grace, so he is not a candidate for succession either.
    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    for (auto& m : lp->members) {
        if (m.username == "alice")
            m.disconnected_at = std::chrono::steady_clock::now() - std::chrono::hours(1);
    }

    f.timers.Fire("lobby_eviction");

    // Same stale-host characterization as the HandleLeave path above: the
    // duplicated succession loop in the eviction callback also finds no
    // connected non-bot candidate and leaves lobby.host untouched.
    lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->host == "alice");
}

// ---------------------------------------------------------------------------
// Additional eviction-timer coverage
// ---------------------------------------------------------------------------

TEST_CASE("eviction: member within the grace window is not evicted") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.lobby.OnClose(f.alice_sock, &f.alice_sd);

    // disconnected_at defaults to "just now", well within the grace window.
    f.timers.Fire("lobby_eviction");

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    bool alice_present = false;
    for (const auto& m : lp->members)
        if (m.username == "alice") { alice_present = true; break; }
    CHECK(alice_present);
}

TEST_CASE("eviction: broadcasts lobby_updated after evicting a member") {
    LobbyFixture f;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    f.lobby.OnClose(f.bob_sock, &f.bob_sd);

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    for (auto& m : lp->members) {
        if (m.username == "bob")
            m.disconnected_at = std::chrono::steady_clock::now() - std::chrono::hours(1);
    }

    f.bus.Clear();
    f.timers.Fire("lobby_eviction");

    std::string topic = "lobby_" + code;
    bool published = false;
    for (const auto& [t, payload] : f.bus.published)
        if (t == topic) { published = true; break; }
    CHECK(published);
}

// ---------------------------------------------------------------------------
// Join-hijack: a joiner takes over an available bot slot instead of adding
// a brand-new member.
// ---------------------------------------------------------------------------

TEST_CASE("join: hijacks an available bot slot instead of adding a new member") {
    LobbyFixture f;
    std::string code = f.alice_creates();

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    lp->members.emplace_back("BotBuddy", nullptr, true, true);
    std::size_t members_before = lp->members.size();

    PerSocketData charlie_sd; charlie_sd.username = "charlie";
    auto* charlie = fake_sock(charlie_sd);
    f.router.Dispatch(make_ctx(charlie, &charlie_sd), join_msg(code));

    lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->members.size() == members_before);

    bool charlie_present = false;
    bool bot_present = false;
    for (const auto& m : lp->members) {
        if (m.username == "charlie") charlie_present = true;
        if (m.username == "BotBuddy") bot_present = true;
    }
    CHECK(charlie_present);
    CHECK_FALSE(bot_present);
}

// ---------------------------------------------------------------------------
// Deck catalog (GET /api/decks) + deck-snapshot selection.
// ---------------------------------------------------------------------------

TEST_CASE("deck catalog: lists the classic deck with namespace and mods") {
    auto mods = ShippedMods();
    REQUIRE(!mods.empty());

    json catalog = LobbyController::DeckCatalogJson(mods);
    REQUIRE(catalog.contains("decks"));
    REQUIRE(catalog["decks"].is_array());

    const json* classic = nullptr;
    for (const auto& deck : catalog["decks"]) {
        if (deck.value("id", "") == "classic" &&
            deck.value("namespace", "") == "vanilla") {
            classic = &deck;
        }
    }
    REQUIRE(classic != nullptr);
    CHECK(classic->value("name", "") == "Classic");
    CHECK((*classic)["mods"] == json::array({"vanilla"}));

    // Every entry carries exactly the endpoint's four documented fields.
    for (const auto& deck : catalog["decks"]) {
        CHECK(deck.contains("id"));
        CHECK(deck.contains("name"));
        CHECK(deck.contains("namespace"));
        CHECK(deck.contains("mods"));
        CHECK(deck["mods"].is_array());
        CHECK(deck.contains("valid"));
    }
}

TEST_CASE("deck catalog: marks a deck with an unavailable mod invalid") {
    auto mods = ShippedMods();
    REQUIRE(!mods.empty());

    // INFO: fabricate a deck requiring a mod that is not loaded.
    match::modload::DeckDef broken;
    broken.id = "needs_ghost";
    broken.namespace_id = mods[0].manifest.id;
    broken.deck_id = broken.namespace_id + ":" + broken.id;
    broken.name = "Needs Ghost";
    broken.mods = {"vanilla", "ghost_mod"};
    mods[0].decks.push_back(broken);

    json catalog = LobbyController::DeckCatalogJson(mods);
    bool saw_valid = false;
    bool saw_invalid = false;
    for (const auto& deck : catalog["decks"]) {
        if (deck.value("id", "") == "classic") {
            CHECK(deck.value("valid", false));
            saw_valid = true;
        }
        if (deck.value("id", "") == "needs_ghost") {
            CHECK_FALSE(deck.value("valid", true));
            saw_invalid = true;
        }
    }
    CHECK(saw_valid);
    CHECK(saw_invalid);
}

TEST_CASE("mods report: surfaces a broken mod with its errors") {
    match::modload::LoadResult loaded;
    match::modload::ModReport report;
    report.mod_id = "broken";
    report.folder = "/mods/broken";
    report.ok = false;
    report.errors.push_back({"asset.missing", "assets",
                             "/mods/broken/assets/b/index.json",
                             "asset file 'art.png' is missing"});
    report.warnings.push_back({"asset.unreferenced", "assets", "x",
                               "bundle is unreferenced"});
    loaded.reports.push_back(report);

    json out = LobbyController::ModsReportJson(loaded);
    REQUIRE(out["mods"].is_array());
    REQUIRE(out["mods"].size() == 1);
    CHECK(out["mods"][0]["id"] == "broken");
    CHECK(out["mods"][0]["ok"] == false);
    REQUIRE(out["mods"][0]["errors"].size() == 1);
    CHECK(out["mods"][0]["errors"][0]["check"] == "asset.missing");
    REQUIRE(out["mods"][0]["warnings"].size() == 1);
    CHECK(out["mods"][0]["warnings"][0]["check"] == "asset.unreferenced");
}

TEST_CASE("deck snapshot: selection loads mods, cards and settings at once") {
    auto mods = ShippedMods();
    REQUIRE(!mods.empty());

    LobbySettings settings;
    settings.active_mods = {"seven_zero"};
    REQUIRE(LobbyController::ApplyDeckSnapshot(
        settings, mods, "vanilla:classic"));

    CHECK(settings.deck.value("id", "") == "classic");
    CHECK(settings.deck.value("name", "") == "Classic");
    CHECK(settings.deck.value("namespace", "") == "vanilla");
    CHECK(settings.deck["mods"] == json::array({"vanilla"}));
    CHECK(settings.deck["cards"]["vanilla:wild"] == 4);
    CHECK(settings.deck["cards"]["vanilla:red_0"] == 1);
    CHECK(settings.deck["settings"]["count_zeros"] == 1);

    // Additive: the legacy scalar settings are untouched by deck selection.
    CHECK(settings.active_mods == std::vector<std::string>{"seven_zero"});
}

TEST_CASE("deck snapshot: unknown deck id leaves settings untouched") {
    auto mods = ShippedMods();
    REQUIRE(!mods.empty());

    LobbySettings settings;
    CHECK_FALSE(LobbyController::ApplyDeckSnapshot(
        settings, mods, "ghost:missing"));
    CHECK(settings.deck.empty());
    CHECK(settings.active_mods.empty());
}

TEST_CASE("settings: host selecting a deck loads the whole snapshot") {
    LobbyFixture f{ProjectModsRoot()};
    std::string code = f.alice_creates();
    f.bus.Clear();

    f.router.Dispatch(
        f.actx(),
        json{{"action", ws::ClientAction::kLobbyUpdateSettings},
             {"request_id", "req-deck"},
             {"deck_id", "vanilla:classic"}});

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->settings.deck.value("id", "") == "classic");
    CHECK(lp->settings.deck["mods"] == json::array({"vanilla"}));
    CHECK(lp->settings.deck["cards"]["vanilla:blue_3"] == 2);
    CHECK(lp->settings.deck["settings"]["count_zeros"] == 1);

    auto resp = json::parse(f.bus.FramesFor(f.alice_sock).back().payload);
    CHECK(resp.value("action", "") != "error");
}

TEST_CASE("settings: unknown deck id returns an error and keeps freestyle") {
    LobbyFixture f{ProjectModsRoot()};
    std::string code = f.alice_creates();
    f.bus.Clear();

    f.router.Dispatch(
        f.actx(),
        json{{"action", ws::ClientAction::kLobbyUpdateSettings},
             {"request_id", "req-deck-bad"},
             {"deck_id", "ghost:missing"}});

    auto resp = json::parse(f.bus.FramesFor(f.alice_sock).back().payload);
    CHECK(resp.value("action", "") == "error");
    CHECK(resp.value("code", "") == "invalid_payload");

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->settings.deck.empty());
}

TEST_CASE("settings: freestyle edits still apply after a deck is selected") {
    LobbyFixture f{ProjectModsRoot()};
    std::string code = f.alice_creates();

    f.router.Dispatch(
        f.actx(),
        json{{"action", ws::ClientAction::kLobbyUpdateSettings},
             {"request_id", "req-deck"},
             {"deck_id", "vanilla:classic"}});
    f.bus.Clear();
    f.router.Dispatch(
        f.actx(),
        json{{"action", ws::ClientAction::kLobbyUpdateSettings},
             {"request_id", "req-free"},
             {"starting_cards", 5},
             {"ranked", false}});

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp);
    CHECK(lp->settings.starting_cards == 5);
    CHECK_FALSE(lp->settings.ranked);
    // The loaded snapshot survives later freestyle scalar edits.
    CHECK(lp->settings.deck.value("id", "") == "classic");
}

} // TEST_SUITE

// ---------------------------------------------------------------------------
// match-end persistence + the stats gate.
//
// The gate comparator itself is covered by stats_gate_test.cpp; these cases
// verify the *wiring*: a real assembled match's mod set / deck multiset must
// decide whether the per-player player_stats aggregate is updated, while the
// matches / match_participants rows are written for every match.
// ---------------------------------------------------------------------------

namespace {

/* INFO: force a seat's hand to exactly `cards` (scripted human win),
 *       mirroring match_controller_test.cpp. */
void ForceHand(match::engine::MatchInstance& engine,
               match::ecs::Entity player,
               const std::vector<match::ecs::Entity>& cards) {
    match::ecs::Hand* hand = engine.Store().Get<match::ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<match::ecs::Entity> existing = hand->cards;
    for (match::ecs::Entity card : existing) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            match::ecs::ZoneRef{match::ecs::ZoneKind::kDrawPile,
                                match::ecs::Entity{}});
    }
    for (match::ecs::Entity card : cards) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            match::ecs::ZoneRef{match::ecs::ZoneKind::kHand, player});
    }
}

std::vector<match::ecs::Entity> CardsByKind(
    const match::engine::MatchInstance& engine, const std::string& kind) {
    std::vector<match::ecs::Entity> out;
    for (match::ecs::Entity card : engine.Registries().cards) {
        const match::ecs::CardIdentity* identity =
            engine.Store().Get<match::ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind) {
            out.push_back(card);
        }
    }
    return out;
}

uint32_t BitsOf(const match::engine::MatchInstance& engine,
                match::ecs::Entity card) {
    const std::optional<match::ecs::CompactCardV2> id =
        engine.Registries().CardId(card);
    REQUIRE(id.has_value());
    return id->bits;
}

/** @brief Insert a minimal `users` row so the stats gate can apply. */
void InsertUser(const std::string& username) {
    REQUIRE(Database::Get()
                .Exec("INSERT OR IGNORE INTO users "
                      "(username, pass_hash, salt, email) "
                      "VALUES (?, 'h', 's', ?);",
                      {username, username + "@wp13fix2.test"})
                .has_value());
}

/** @brief Remove the test usernames' DB rows so cases stay independent. */
void CleanupUsers(const std::vector<std::string>& users) {
    for (const std::string& user : users) {
        (void)Database::Get().Exec(
            "DELETE FROM player_stats WHERE username = ?;", {user});
        (void)Database::Get().Exec("DELETE FROM users WHERE username = ?;",
                                   {user});
    }
}

/** @brief Ready both humans and start the match. */
void ReadyAndStart(LobbyFixture& f) {
    f.router.Dispatch(f.actx(), toggle_ready_msg());
    f.router.Dispatch(f.bctx(), toggle_ready_msg());
    f.bus.Clear();
    f.router.Dispatch(f.actx(), start_msg());
}

/**
 * @brief Drives the live session to a scripted Alice win, then persists it.
 *
 * Alice is forced down to a single wild, plays it and answers the colour
 * prompt, emptying her hand. `NotifyMatchOver` is then invoked exactly as the
 * MatchController teardown would.
 *
 * @param f            Fixture whose lobby holds the live session.
 * @param alice        Alice's username in this fixture.
 * @param out_match_id Receives `lobby.match_id` before it is cleared.
 */
void FinishWithAliceWin(LobbyFixture& f, const std::string& alice,
                        std::string& out_match_id) {
    Lobby* lobby = f.lobby.GetLobbyByCode(f.alice_sd.lobby_code);
    REQUIRE(lobby != nullptr);
    REQUIRE(lobby->session != nullptr);
    out_match_id = lobby->match_id;
    REQUIRE(!out_match_id.empty());

    match::engine::MatchInstance& engine = lobby->session->Engine();
    const std::vector<match::ecs::Entity> wilds =
        CardsByKind(engine, "vanilla:wild");
    REQUIRE(wilds.size() >= 1);
    const match::ecs::Entity alice_entity = *engine.FindPlayer(alice);
    ForceHand(engine, alice_entity, {wilds[0]});

    REQUIRE(lobby->session->PlayCard(alice, BitsOf(engine, wilds[0])));
    REQUIRE(lobby->session->SubmitInput(alice, "choose_color", "red"));
    REQUIRE(engine.IsMatchOver());
    REQUIRE(engine.GetWinner() == alice);

    f.lobby.NotifyMatchOver(lobby->id);
    CHECK(lobby->session == nullptr);
}

}  // namespace

TEST_SUITE("LobbyController::MatchEnd") {

TEST_CASE("match end: vanilla classic updates player_stats and writes rows") {
    REQUIRE(Database::Get().RunMigrations().has_value());
    const std::string alice = "wp13g_alice";
    const std::string bob = "wp13g_bob";
    CleanupUsers({alice, bob});

    LobbyFixture f{ProjectModsRoot()};
    f.alice_sd.username = alice;
    f.bob_sd.username = bob;
    std::string code = f.alice_creates();
    f.bob_joins(code);
    InsertUser(alice);
    InsertUser(bob);
    ReadyAndStart(f);

    std::string match_id;
    FinishWithAliceWin(f, alice, match_id);

    auto alice_stats = Database::Get().QueryOne(
        "SELECT total_wins, total_losses FROM player_stats "
        "WHERE username = ?;", {alice});
    REQUIRE(alice_stats.has_value());
    REQUIRE(alice_stats->has_value());
    CHECK(alice_stats->value().Get<int>("total_wins") == 1);
    CHECK(alice_stats->value().Get<int>("total_losses") == 0);

    auto bob_stats = Database::Get().QueryOne(
        "SELECT total_wins, total_losses FROM player_stats "
        "WHERE username = ?;", {bob});
    REQUIRE(bob_stats.has_value());
    REQUIRE(bob_stats->has_value());
    CHECK(bob_stats->value().Get<int>("total_wins") == 0);
    CHECK(bob_stats->value().Get<int>("total_losses") == 1);

    auto match_row = Database::Get().QueryOne(
        "SELECT id, winner_username FROM matches ORDER BY id DESC LIMIT 1;",
        {});
    REQUIRE(match_row.has_value());
    REQUIRE(match_row->has_value());
    CHECK(match_row->value().Get<std::string>("winner_username") == alice);
    const int match_row_id = match_row->value().Get<int>("id");

    auto participants = Database::Get().Query(
        "SELECT COUNT(*) AS n FROM match_participants WHERE match_id = ?;",
        {match_row_id});
    REQUIRE(participants.has_value());
    REQUIRE(!participants->empty());
    CHECK(participants->front().Get<int>("n") == 2);

    auto ledger = Database::Get().QueryOne(
        "SELECT placement, result FROM match_history "
        "WHERE match_id = ? AND username = ?;", {match_id, alice});
    REQUIRE(ledger.has_value());
    REQUIRE(ledger->has_value());
    CHECK(ledger->value().Get<int>("placement") == 1);
    CHECK(ledger->value().Get<std::string>("result") == "win");

    CleanupUsers({alice, bob});
}

TEST_CASE("match end: extra mod keeps player_stats untouched but writes rows") {
    REQUIRE(Database::Get().RunMigrations().has_value());
    const std::string alice = "wp13m_alice";
    const std::string bob = "wp13m_bob";
    CleanupUsers({alice, bob});

    LobbyFixture f{ProjectModsRoot()};
    f.alice_sd.username = alice;
    f.bob_sd.username = bob;
    std::string code = f.alice_creates();

    // INFO: a second, real loaded mod flips the gate while the deck
    //       multiset stays the vanilla classic one.
    Lobby* lobby = f.lobby.GetLobbyByCode(code);
    REQUIRE(lobby != nullptr);
    lobby->settings.active_mods = {"vanilla", "progressive"};

    f.bob_joins(code);
    InsertUser(alice);
    InsertUser(bob);
    ReadyAndStart(f);

    std::string match_id;
    FinishWithAliceWin(f, alice, match_id);

    CHECK_FALSE(Database::Get()
                    .QueryOne("SELECT 1 FROM player_stats WHERE username = ?;",
                              {alice})
                    .value()
                    .has_value());
    CHECK_FALSE(Database::Get()
                    .QueryOne("SELECT 1 FROM player_stats WHERE username = ?;",
                              {bob})
                    .value()
                    .has_value());

    auto match_row = Database::Get().QueryOne(
        "SELECT id, winner_username FROM matches ORDER BY id DESC LIMIT 1;",
        {});
    REQUIRE(match_row.has_value());
    REQUIRE(match_row->has_value());
    CHECK(match_row->value().Get<std::string>("winner_username") == alice);
    const int match_row_id = match_row->value().Get<int>("id");

    auto participants = Database::Get().Query(
        "SELECT COUNT(*) AS n FROM match_participants WHERE match_id = ?;",
        {match_row_id});
    REQUIRE(participants.has_value());
    REQUIRE(!participants->empty());
    CHECK(participants->front().Get<int>("n") == 2);

    CleanupUsers({alice, bob});
}

TEST_CASE("match end: changed deck multiset keeps player_stats untouched") {
    REQUIRE(Database::Get().RunMigrations().has_value());
    const std::string alice = "wp13d_alice";
    const std::string bob = "wp13d_bob";
    CleanupUsers({alice, bob});

    LobbyFixture f{ProjectModsRoot()};
    f.alice_sd.username = alice;
    f.bob_sd.username = bob;
    std::string code = f.alice_creates();

    // INFO: a real assembled classic deck with one kind's multiplicity
    //       changed (red_0 x2 instead of x1) still assembles, but the gate
    //       must reject it.
    Lobby* lobby = f.lobby.GetLobbyByCode(code);
    REQUIRE(lobby != nullptr);
    json deck = json{{"id", "classic"},
                     {"name", "Classic"},
                     {"namespace", "vanilla"},
                     {"mods", json::array({"vanilla"})},
                     {"cards", json::object()}};
    for (const auto& [kind, count] :
         match::server::VanillaClassicDeckCards()) {
        deck["cards"][kind] = count;
    }
    deck["cards"]["vanilla:red_0"] = 2;
    lobby->settings.deck = std::move(deck);

    f.bob_joins(code);
    InsertUser(alice);
    InsertUser(bob);
    ReadyAndStart(f);

    std::string match_id;
    FinishWithAliceWin(f, alice, match_id);

    CHECK_FALSE(Database::Get()
                    .QueryOne("SELECT 1 FROM player_stats WHERE username = ?;",
                              {alice})
                    .value()
                    .has_value());
    CHECK_FALSE(Database::Get()
                    .QueryOne("SELECT 1 FROM player_stats WHERE username = ?;",
                              {bob})
                    .value()
                    .has_value());

    auto match_row = Database::Get().QueryOne(
        "SELECT winner_username FROM matches ORDER BY id DESC LIMIT 1;", {});
    REQUIRE(match_row.has_value());
    REQUIRE(match_row->has_value());
    CHECK(match_row->value().Get<std::string>("winner_username") == alice);

    CleanupUsers({alice, bob});
}

// ---------------------------------------------------------------------------
// Review fix (Important): an explicit leave or kick during the loading barrier
// must count the seat as loaded, so the survivors don't wait out the `ready_`
// timer. Before the fix, RemoveMember never touched the barrier and a leaver
// stayed pending until the 15 s timeout. Uses a real LobbyController wired to a
// real MatchController (the production hook owner).
// ---------------------------------------------------------------------------
TEST_SUITE("LobbyController::ReadyBarrier") {
TEST_CASE("leave: a seated player leaving during the ready barrier opens it") {
    LobbyFixture f{ProjectModsRoot()};
    MatchController match(f.router, f.bus, f.timers, f.lobby);

    std::string code = f.alice_creates();
    f.bob_joins(code);
    ReadyAndStart(f);

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp != nullptr);
    REQUIRE(lp->session != nullptr);
    REQUIRE_FALSE(lp->session->ReadyBarrierOpen());

    // INFO: bob reports loaded; only the leaver (alice) is still pending, so
    //       the removal must complete and open the barrier.
    f.router.Dispatch(f.bctx(),
                      json{{"action", ws::ClientAction::kMatchClientReady}});
    REQUIRE_FALSE(lp->session->ReadyBarrierComplete());
    f.bus.Clear();

    f.router.Dispatch(f.actx(), leave_msg());

    CHECK(lp->session->ReadyBarrierOpen());
    CHECK_FALSE(f.timers.Has("ready_1"));

    bool bob_saw_begin = false;
    for (const SentFrame& frame : f.bus.sent) {
        if (frame.to != f.bob_sock) continue;
        const json packet = json::parse(frame.payload);
        if (packet.value("action", std::string()) == "match_event"
            && packet.value("type", std::string()) == "match_begin") {
            bob_saw_begin = true;
        }
    }
    CHECK(bob_saw_begin);
}

TEST_CASE("leave: the dropped-from-engine branch also opens the barrier when last") {
    LobbyFixture f{ProjectModsRoot()};
    MatchController match(f.router, f.bus, f.timers, f.lobby);

    std::string code = f.alice_creates();
    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp != nullptr);
    // INFO: force the `kPlayerDroppedFromEngine` outcome instead of bot
    //       replacement, so this exercises the branch where the barrier entry
    //       is still pending when RemoveMember runs.
    lp->settings.allow_bot_replacement = false;

    f.bob_joins(code);
    // INFO: a third human keeps the lobby above the 2-member abort floor after
    //       the dropped seat is erased (`CheckMatchIntegrity`).
    PerSocketData carol_sd;
    carol_sd.username = "carol";
    AppWebSocket* carol_sock = fake_sock(carol_sd);
    WsContext cctx = make_ctx(carol_sock, &carol_sd);
    f.router.Dispatch(cctx, join_msg(code));

    f.router.Dispatch(f.actx(), toggle_ready_msg());
    f.router.Dispatch(f.bctx(), toggle_ready_msg());
    f.router.Dispatch(cctx, toggle_ready_msg());
    f.bus.Clear();
    f.router.Dispatch(f.actx(), start_msg());

    REQUIRE(lp->session != nullptr);
    REQUIRE_FALSE(lp->session->ReadyBarrierOpen());
    // INFO: bob + carol report loaded; only the leaver (alice) stays pending.
    f.router.Dispatch(f.bctx(),
                      json{{"action", ws::ClientAction::kMatchClientReady}});
    f.router.Dispatch(cctx, json{{"action", ws::ClientAction::kMatchClientReady}});
    REQUIRE_FALSE(lp->session->ReadyBarrierComplete());
    f.bus.Clear();

    f.router.Dispatch(f.actx(), leave_msg());

    REQUIRE(lp->session != nullptr);
    CHECK(lp->session->ReadyBarrierOpen());
    CHECK_FALSE(f.timers.Has("ready_1"));
}

TEST_CASE("reconnect: a closed barrier sends players_ready, not match_begin, and is not re-armed") {
    LobbyFixture f{ProjectModsRoot()};
    MatchController match(f.router, f.bus, f.timers, f.lobby);

    std::string code = f.alice_creates();
    f.bob_joins(code);
    ReadyAndStart(f);

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp != nullptr);
    REQUIRE(lp->session != nullptr);
    REQUIRE_FALSE(lp->session->ReadyBarrierOpen());
    REQUIRE(f.timers.Has("ready_1"));

    // INFO: bob reports loaded so the pending set has alice left; a re-arm
    //       would reset the seat-ready set and drop the count back to zero.
    f.router.Dispatch(f.bctx(),
                      json{{"action", ws::ClientAction::kMatchClientReady}});
    REQUIRE_FALSE(lp->session->ReadyBarrierComplete());
    f.bus.Clear();

    // INFO: the real reconnect entry point — LobbyController::OnOpen drives
    //       SendMatchStateToSocket -> MatchSession::SendSnapshot.
    f.lobby.OnOpen(f.alice_sock, &f.alice_sd);

    bool alice_saw_players_ready = false;
    bool alice_saw_match_begin = false;
    int ready_seen = -1;
    for (const SentFrame& frame : f.bus.sent) {
        if (frame.to != f.alice_sock) continue;
        const json packet = json::parse(frame.payload);
        if (packet.value("action", std::string()) != "match_event") continue;
        const std::string type = packet.value("type", std::string());
        if (type == "players_ready") {
            alice_saw_players_ready = true;
            ready_seen = packet["payload"].value("ready", -1);
        }
        if (type == "match_begin") alice_saw_match_begin = true;
    }
    CHECK(alice_saw_players_ready);
    CHECK_FALSE(alice_saw_match_begin);
    // INFO: the seat-ready set survived the reconnect (bob is still the only
    //       ready seat), so BeginReadyBarrier did not run a second time.
    CHECK(ready_seen == 1);
    CHECK(f.timers.Has("ready_1"));
    CHECK_FALSE(lp->session->ReadyBarrierComplete());
}

TEST_CASE("reconnect: an open barrier sends match_begin") {
    LobbyFixture f{ProjectModsRoot()};
    MatchController match(f.router, f.bus, f.timers, f.lobby);

    std::string code = f.alice_creates();
    f.bob_joins(code);
    ReadyAndStart(f);

    Lobby* lp = f.lobby.GetLobbyByCode(code);
    REQUIRE(lp != nullptr);
    REQUIRE(lp->session != nullptr);

    // INFO: expire the barrier deadline the way the 15 s cap would.
    f.timers.Fire("ready_1");
    REQUIRE(lp->session->ReadyBarrierOpen());
    f.bus.Clear();

    f.lobby.OnOpen(f.alice_sock, &f.alice_sd);

    bool alice_saw_match_begin = false;
    bool alice_saw_players_ready = false;
    for (const SentFrame& frame : f.bus.sent) {
        if (frame.to != f.alice_sock) continue;
        const json packet = json::parse(frame.payload);
        if (packet.value("action", std::string()) != "match_event") continue;
        const std::string type = packet.value("type", std::string());
        if (type == "match_begin") alice_saw_match_begin = true;
        if (type == "players_ready") alice_saw_players_ready = true;
    }
    CHECK(alice_saw_match_begin);
    CHECK_FALSE(alice_saw_players_ready);
}
}  // TEST_SUITE("LobbyController::ReadyBarrier")

} // TEST_SUITE

TEST_SUITE("LobbyController::Hardening") {

static std::string LastErrorCode(FakeBroadcaster& bus, AppWebSocket* sock) {
    auto frames = bus.FramesFor(sock);
    REQUIRE(!frames.empty());
    return json::parse(frames.back().payload).value("code", "");
}

TEST_CASE("payloads: ParsePayload enforces the contract limits") {
    CHECK_FALSE(ws::ParsePayload<ws::LobbyCreatePayload>(
                    json{{"name", std::string(51, 'a')}}).has_value());
    CHECK_FALSE(ws::ParsePayload<ws::LobbyCreatePayload>(json{{"name", 5}}).has_value());
    CHECK(ws::ParsePayload<ws::LobbyCreatePayload>(json{{"name", "ok"}}).has_value());
}

}  // TEST_SUITE("LobbyController::Hardening")
