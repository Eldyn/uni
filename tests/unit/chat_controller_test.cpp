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

    // Global history push defaults to off here — irrelevant to these tests,
    // and OnOpen() is never called in this fixture anyway.
    ChatControllerFixture()
        : sock(fake_sock(sd)), controller(router, broadcaster, presence, 0) {
        sd.username = "chat_ctrl_test_alice";
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupDmTestRows();
    }

    ~ChatControllerFixture() { CleanupDmTestRows(); }

    WsContext ctx() { return WsContext{sock, &sd, uWS::OpCode::TEXT}; }

    void Dispatch(json msg) { router.Dispatch(ctx(), msg); }
};

/**
 * @struct ChatHistoryOnJoinFixture
 * @brief Like ChatControllerFixture, but with an explicit history
 * on-join/limit override and a second socket that joins after the first
 * has posted some global messages.
 */
struct ChatHistoryOnJoinFixture {
    ActionRouter      router;
    FakeBroadcaster   broadcaster;
    FakePresenceStore presence;
    PerSocketData     sender_sd;
    AppWebSocket*     sender_sock;
    PerSocketData     joiner_sd;
    AppWebSocket*     joiner_sock;
    ChatController    controller;

    ChatHistoryOnJoinFixture(int on_join, int limit)
        : sender_sock(fake_sock(sender_sd)), joiner_sock(fake_sock(joiner_sd)),
          controller(router, broadcaster, presence, on_join, limit) {
        sender_sd.username = "chat_ctrl_test_alice";
        joiner_sd.username = "chat_ctrl_test_joiner";
    }

    WsContext SenderCtx() { return WsContext{sender_sock, &sender_sd, uWS::OpCode::TEXT}; }

    void PostGlobal(const std::string& message) {
        router.Dispatch(SenderCtx(),
                        {{"action", "chat_send"}, {"channel", "global"}, {"message", message}});
    }
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

TEST_SUITE("ChatController — global history on join") {

TEST_CASE("disabled: a joining socket receives no chat_history push") {
    ChatHistoryOnJoinFixture f(/*on_join=*/0, /*limit=*/64);
    f.PostGlobal("hello");

    f.controller.OnOpen(f.joiner_sock, &f.joiner_sd);

    CHECK(f.broadcaster.FramesFor(f.joiner_sock).empty());
}

TEST_CASE("enabled: a joining socket receives prior global messages, oldest first") {
    ChatHistoryOnJoinFixture f(/*on_join=*/1, /*limit=*/64);
    f.PostGlobal("first");
    f.PostGlobal("second");

    f.controller.OnOpen(f.joiner_sock, &f.joiner_sd);

    auto frames = f.broadcaster.FramesFor(f.joiner_sock);
    REQUIRE(frames.size() == 1);
    auto resp = json::parse(frames[0].payload);
    CHECK(resp["action"] == "chat_history");
    CHECK(resp["channel"] == "global");
    REQUIRE(resp["messages"].size() == 2);
    CHECK(resp["messages"][0]["message"] == "first");
    CHECK(resp["messages"][1]["message"] == "second");
}

TEST_CASE("enabled with no prior messages still sends an empty chat_history, not silence") {
    ChatHistoryOnJoinFixture f(/*on_join=*/1, /*limit=*/64);

    f.controller.OnOpen(f.joiner_sock, &f.joiner_sd);

    auto frames = f.broadcaster.FramesFor(f.joiner_sock);
    REQUIRE(frames.size() == 1);
    auto resp = json::parse(frames[0].payload);
    CHECK(resp["messages"].empty());
}

TEST_CASE("limit caps the push to the newest N messages") {
    ChatHistoryOnJoinFixture f(/*on_join=*/1, /*limit=*/1);
    f.PostGlobal("first");
    f.PostGlobal("second");

    f.controller.OnOpen(f.joiner_sock, &f.joiner_sd);

    auto frames = f.broadcaster.FramesFor(f.joiner_sock);
    REQUIRE(frames.size() == 1);
    auto resp = json::parse(frames[0].payload);
    REQUIRE(resp["messages"].size() == 1);
    CHECK(resp["messages"][0]["message"] == "second");
}

TEST_CASE("limit of -1 sends the entire in-memory buffer") {
    ChatHistoryOnJoinFixture f(/*on_join=*/1, /*limit=*/-1);
    f.PostGlobal("first");
    f.PostGlobal("second");
    f.PostGlobal("third");

    f.controller.OnOpen(f.joiner_sock, &f.joiner_sd);

    auto frames = f.broadcaster.FramesFor(f.joiner_sock);
    REQUIRE(frames.size() == 1);
    auto resp = json::parse(frames[0].payload);
    REQUIRE(resp["messages"].size() == 3);
}

}  // TEST_SUITE
