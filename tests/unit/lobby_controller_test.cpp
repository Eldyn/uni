#include <doctest/doctest.h>
#include <action_router.hpp>
#include <controllers/lobby_controller.hpp>
#include <common/ws.hpp>
#include <nlohmann/json.hpp>
#include "support/fake_broadcaster.hpp"
#include "support/fake_timer_service.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// AppWebSocket* as an opaque test key — FakeBroadcaster stores but never
// dereferences it. Cast from PerSocketData address to avoid null checks.
static AppWebSocket* fake_sock(PerSocketData& sd) {
    return reinterpret_cast<AppWebSocket*>(&sd);
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

static json list_msg(const std::string& req = "req-4") {
    return {{"action", ws::ClientAction::kLobbyList}, {"request_id", req}};
}

static json start_msg(const std::string& req = "req-5") {
    return {{"action", ws::ClientAction::kLobbyStartMatch}, {"request_id", req}};
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
    LobbyController  lobby{router, bus, timers};

    PerSocketData alice_sd, bob_sd;
    AppWebSocket* alice_sock;
    AppWebSocket* bob_sock;

    LobbyFixture() {
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

} // TEST_SUITE
