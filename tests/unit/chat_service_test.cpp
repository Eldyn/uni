#include <doctest/doctest.h>
#include <services/chat_service.hpp>
#include <database.hpp>

namespace {

// chat_dm_test_* usernames avoid colliding with rows other test files insert.
void CleanupDmTestRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM chat_dms WHERE sender LIKE 'chat_dm_test_%' "
        "OR recipient LIKE 'chat_dm_test_%';");
    REQUIRE(result.has_value());
}

struct ChatDmFixture {
    ChatDmFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupDmTestRows();
    }
    ~ChatDmFixture() { CleanupDmTestRows(); }
};

}  // namespace

TEST_SUITE("ChatService") {

TEST_CASE("PostGlobalMessage: appends to history in order") {
    ChatService svc;

    svc.PostGlobalMessage("alice", "hi");
    svc.PostGlobalMessage("bob", "hey alice");

    auto history = svc.GetGlobalHistory();
    REQUIRE(history.size() == 2);
    CHECK(history[0].username == "alice");
    CHECK(history[0].message == "hi");
    CHECK(history[1].username == "bob");
    CHECK(history[1].message == "hey alice");
}

TEST_CASE("PostGlobalMessage: caps history at 200 messages, dropping the oldest") {
    ChatService svc;

    for (int i = 0; i < 205; ++i) {
        svc.PostGlobalMessage("user", "msg" + std::to_string(i));
    }

    auto history = svc.GetGlobalHistory();
    REQUIRE(history.size() == 200);
    CHECK(history.front().message == "msg5");
    CHECK(history.back().message == "msg204");
}

TEST_CASE("GetGlobalHistory: empty service returns an empty history") {
    ChatService svc;
    CHECK(svc.GetGlobalHistory().empty());
}

TEST_CASE("SendDirectMessage: round-trips through encryption for both participants") {
    ChatDmFixture f;
    ChatService svc;

    REQUIRE(svc.SendDirectMessage("chat_dm_test_alice", "chat_dm_test_bob", "hi bob").has_value());
    REQUIRE(svc.SendDirectMessage("chat_dm_test_bob", "chat_dm_test_alice", "hi alice").has_value());

    auto from_alice = svc.GetDirectHistory("chat_dm_test_alice", "chat_dm_test_bob");
    REQUIRE(from_alice.has_value());
    REQUIRE(from_alice->size() == 2);
    CHECK(from_alice->at(0).username == "chat_dm_test_alice");
    CHECK(from_alice->at(0).message == "hi bob");
    CHECK(from_alice->at(1).username == "chat_dm_test_bob");
    CHECK(from_alice->at(1).message == "hi alice");

    auto from_bob = svc.GetDirectHistory("chat_dm_test_bob", "chat_dm_test_alice");
    REQUIRE(from_bob.has_value());
    CHECK(from_bob->size() == 2);
}

TEST_CASE("GetDirectHistory: no messages returns an empty history, not an error") {
    ChatDmFixture f;
    ChatService svc;

    auto history = svc.GetDirectHistory("chat_dm_test_ghost1", "chat_dm_test_ghost2");
    REQUIRE(history.has_value());
    CHECK(history->empty());
}

TEST_CASE("SendDirectMessage: stores ciphertext, not the plaintext, at rest") {
    ChatDmFixture f;
    ChatService svc;

    REQUIRE(svc.SendDirectMessage("chat_dm_test_alice", "chat_dm_test_bob",
                                  "a very secret message").has_value());

    auto rows = Database::Get().Query(
        "SELECT ciphertext FROM chat_dms WHERE sender = ?;", {std::string("chat_dm_test_alice")});
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 1);
    CHECK(rows->at(0).Get<std::string>("ciphertext").find("secret") == std::string::npos);
}

TEST_CASE("SendDirectMessage: prunes a pair's history down to kDmHistoryLimit") {
    ChatDmFixture f;
    ChatService svc;

    for (std::size_t i = 0; i < ChatService::kDmHistoryLimit + 5; ++i) {
        REQUIRE(svc.SendDirectMessage("chat_dm_test_alice", "chat_dm_test_bob",
                                      "msg" + std::to_string(i)).has_value());
    }

    auto history = svc.GetDirectHistory("chat_dm_test_alice", "chat_dm_test_bob");
    REQUIRE(history.has_value());
    CHECK(history->size() == ChatService::kDmHistoryLimit);
    CHECK(history->front().message == "msg5");
    CHECK(history->back().message == "msg" + std::to_string(ChatService::kDmHistoryLimit + 4));
}

}  // TEST_SUITE
