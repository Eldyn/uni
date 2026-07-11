#include <doctest/doctest.h>
#include <controllers/chat_controller.hpp>
#include <action_router.hpp>
#include <database.hpp>
#include "support/fake_broadcaster.hpp"
#include "support/fake_presence_store.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

// chat_ctrl_test_* usernames avoid colliding with rows other test files insert.
void CleanupDmTestRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM chat_dms WHERE sender LIKE 'chat_ctrl_test_%' "
        "OR recipient LIKE 'chat_ctrl_test_%';");
    REQUIRE(result.has_value());
}

static AppWebSocket* fake_sock(PerSocketData& sd) {
    return reinterpret_cast<AppWebSocket*>(&sd);
}

struct ChatControllerFixture {
    ActionRouter      router;
    FakeBroadcaster   broadcaster;
    FakePresenceStore presence;
    PerSocketData     sd;
    AppWebSocket*     sock;
    ChatController    controller;

    ChatControllerFixture()
        : sock(fake_sock(sd)), controller(router, broadcaster, presence) {
        sd.username = "chat_ctrl_test_alice";
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupDmTestRows();
    }

    ~ChatControllerFixture() { CleanupDmTestRows(); }

    WsContext ctx() { return WsContext{sock, &sd, uWS::OpCode::TEXT}; }

    void Dispatch(json msg) { router.Dispatch(ctx(), msg); }
};

}  // namespace

TEST_SUITE("ChatController — chat_history_request") {

TEST_CASE("no prior DMs returns chat_history with an empty messages array") {
    ChatControllerFixture f;

    f.Dispatch({{"action", "chat_history_request"}, {"target", "chat_ctrl_test_bob"}});

    auto frames = f.broadcaster.FramesFor(f.sock);
    REQUIRE(frames.size() == 1);
    auto resp = json::parse(frames[0].payload);
    CHECK(resp["action"] == "chat_history");
    CHECK(resp["target"] == "chat_ctrl_test_bob");
    CHECK(resp["messages"].empty());
}

TEST_CASE("returns prior DM history for the pair, oldest message first") {
    ChatControllerFixture f;

    f.Dispatch({{"action", "chat_send"},
                {"channel", "dm"},
                {"target", "chat_ctrl_test_bob"},
                {"message", "hi bob"}});
    f.broadcaster.Clear();

    f.Dispatch({{"action", "chat_history_request"}, {"target", "chat_ctrl_test_bob"}});

    auto frames = f.broadcaster.FramesFor(f.sock);
    REQUIRE(frames.size() == 1);
    auto resp = json::parse(frames[0].payload);
    CHECK(resp["action"] == "chat_history");
    REQUIRE(resp["messages"].size() == 1);
    CHECK(resp["messages"][0]["username"] == "chat_ctrl_test_alice");
    CHECK(resp["messages"][0]["message"] == "hi bob");
}

TEST_CASE("missing target field yields an invalid_payload error, not a crash") {
    ChatControllerFixture f;

    f.Dispatch({{"action", "chat_history_request"}});

    auto frames = f.broadcaster.FramesFor(f.sock);
    REQUIRE(frames.size() == 1);
    auto resp = json::parse(frames[0].payload);
    CHECK(resp["action"] == "error");
    CHECK(resp["code"] == "invalid_payload");
}

}  // TEST_SUITE
