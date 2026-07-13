#include <doctest/doctest.h>
#include <services/chat_service.hpp>
#include <database.hpp>
#include <optional>

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

TEST_CASE("AllowSend: allows up to the burst capacity then rate limits") {
    ChatService svc(Database::Get(), 3, 1);

    CHECK(svc.AllowSend("chat_dm_test_flood"));
    CHECK(svc.AllowSend("chat_dm_test_flood"));
    CHECK(svc.AllowSend("chat_dm_test_flood"));
    CHECK_FALSE(svc.AllowSend("chat_dm_test_flood"));
}

TEST_CASE("AllowSend: tracks each username's bucket independently") {
    ChatService svc(Database::Get(), 1, 1);

    CHECK(svc.AllowSend("chat_dm_test_alice"));
    CHECK_FALSE(svc.AllowSend("chat_dm_test_alice"));
    CHECK(svc.AllowSend("chat_dm_test_bob"));
}

TEST_CASE("GetGlobalHistoryPage: first shard returns the newest `limit` messages, oldest first") {
    ChatService svc;
    for (int i = 0; i < 5; ++i) svc.PostGlobalMessage("user", "msg" + std::to_string(i));

    auto page = svc.GetGlobalHistoryPage(std::nullopt, 3);

    REQUIRE(page.messages.size() == 3);
    CHECK(page.messages[0].message == "msg2");
    CHECK(page.messages[1].message == "msg3");
    CHECK(page.messages[2].message == "msg4");
    CHECK(page.has_more == true);
}

TEST_CASE("GetGlobalHistoryPage: has_more is false once the oldest message is included") {
    ChatService svc;
    for (int i = 0; i < 3; ++i) svc.PostGlobalMessage("user", "msg" + std::to_string(i));

    auto page = svc.GetGlobalHistoryPage(std::nullopt, 10);

    REQUIRE(page.messages.size() == 3);
    CHECK(page.has_more == false);
}

TEST_CASE("GetGlobalHistoryPage: before_id walks further back a shard at a time") {
    ChatService svc;
    for (int i = 0; i < 5; ++i) svc.PostGlobalMessage("user", "msg" + std::to_string(i));

    auto first = svc.GetGlobalHistoryPage(std::nullopt, 2);
    REQUIRE(first.messages.size() == 2);
    CHECK(first.has_more == true);

    auto second = svc.GetGlobalHistoryPage(first.messages.front().id, 2);
    REQUIRE(second.messages.size() == 2);
    CHECK(second.messages[0].message == "msg1");
    CHECK(second.messages[1].message == "msg2");
    CHECK(second.has_more == true);

    auto third = svc.GetGlobalHistoryPage(second.messages.front().id, 2);
    REQUIRE(third.messages.size() == 1);
    CHECK(third.messages[0].message == "msg0");
    CHECK(third.has_more == false);
}

TEST_CASE("GetGlobalHistoryPage: an empty history returns an empty page, not an error") {
    ChatService svc;
    auto page = svc.GetGlobalHistoryPage(std::nullopt, 20);
    CHECK(page.messages.empty());
    CHECK(page.has_more == false);
}

TEST_CASE("GetGlobalHistoryPage: a negative limit returns the entire matching window unclamped") {
    ChatService svc;
    for (int i = 0; i < 30; ++i) svc.PostGlobalMessage("user", "msg" + std::to_string(i));

    auto page = svc.GetGlobalHistoryPage(std::nullopt, -1);

    CHECK(page.messages.size() == 30);
    CHECK(page.has_more == false);
}

TEST_CASE("GetGlobalHistoryPage: a limit above kMaxShardSize is clamped") {
    ChatService svc;
    for (int i = 0; i < ChatService::kMaxShardSize + 10; ++i) {
        svc.PostGlobalMessage("user", "msg" + std::to_string(i));
    }

    auto page = svc.GetGlobalHistoryPage(std::nullopt, ChatService::kMaxShardSize + 10);

    CHECK(page.messages.size() == static_cast<std::size_t>(ChatService::kMaxShardSize));
    CHECK(page.has_more == true);
}

TEST_CASE("GetDirectHistoryPage: first shard returns the newest `limit` DMs, oldest first") {
    ChatDmFixture f;
    ChatService svc;

    for (int i = 0; i < 5; ++i) {
        REQUIRE(svc.SendDirectMessage("chat_dm_test_alice", "chat_dm_test_bob",
                                      "msg" + std::to_string(i)).has_value());
    }

    auto page = svc.GetDirectHistoryPage("chat_dm_test_alice", "chat_dm_test_bob", std::nullopt, 3);

    REQUIRE(page.has_value());
    REQUIRE(page->messages.size() == 3);
    CHECK(page->messages[0].message == "msg2");
    CHECK(page->messages[2].message == "msg4");
    CHECK(page->has_more == true);
}

TEST_CASE("GetDirectHistoryPage: before_id walks further back a shard at a time") {
    ChatDmFixture f;
    ChatService svc;

    for (int i = 0; i < 5; ++i) {
        REQUIRE(svc.SendDirectMessage("chat_dm_test_alice", "chat_dm_test_bob",
                                      "msg" + std::to_string(i)).has_value());
    }

    auto first = svc.GetDirectHistoryPage("chat_dm_test_alice", "chat_dm_test_bob", std::nullopt, 2);
    REQUIRE(first.has_value());
    REQUIRE(first->messages.size() == 2);
    CHECK(first->has_more == true);

    auto second = svc.GetDirectHistoryPage("chat_dm_test_alice", "chat_dm_test_bob",
                                           first->messages.front().id, 2);
    REQUIRE(second.has_value());
    REQUIRE(second->messages.size() == 2);
    CHECK(second->messages[0].message == "msg1");
    CHECK(second->messages[1].message == "msg2");
    CHECK(second->has_more == true);
}

TEST_CASE("GetDirectHistoryPage: no messages returns an empty, has_more:false page") {
    ChatDmFixture f;
    ChatService svc;

    auto page = svc.GetDirectHistoryPage("chat_dm_test_ghost1", "chat_dm_test_ghost2", std::nullopt, 20);

    REQUIRE(page.has_value());
    CHECK(page->messages.empty());
    CHECK(page->has_more == false);
}

TEST_CASE("GetDirectHistoryPage: a negative limit is not honoured — falls back to the default shard size") {
    ChatDmFixture f;
    constexpr int kShardSize = 20;
    ChatService svc(Database::Get(), -1, -1, kShardSize);

    for (int i = 0; i < kShardSize + 5; ++i) {
        REQUIRE(svc.SendDirectMessage("chat_dm_test_alice", "chat_dm_test_bob",
                                      "msg" + std::to_string(i)).has_value());
    }

    auto page = svc.GetDirectHistoryPage("chat_dm_test_alice", "chat_dm_test_bob", std::nullopt, -1);

    REQUIRE(page.has_value());
    CHECK(page->messages.size() == static_cast<std::size_t>(kShardSize));
    CHECK(page->has_more == true);
}

TEST_CASE("ChatService: default shard size falls back to CHAT_HISTORY_SHARD_SIZE (50) when unset") {
    ChatService svc;

    for (int i = 0; i < 60; ++i) svc.PostGlobalMessage("alice", "msg" + std::to_string(i));

    auto page = svc.GetGlobalHistoryPage(std::nullopt, 0);

    REQUIRE(page.messages.size() == 50);
    CHECK(page.has_more == true);
}

TEST_CASE("PostLobbyMessage/GetLobbyHistoryPage: appends and shards per lobby code") {
    ChatService svc(Database::Get(), -1, -1, 20);

    svc.PostLobbyMessage("LOBBY1", "alice", "hi");
    svc.PostLobbyMessage("LOBBY1", "bob", "hey alice");
    svc.PostLobbyMessage("LOBBY2", "carol", "unrelated lobby");

    auto page = svc.GetLobbyHistoryPage("LOBBY1", std::nullopt, -1);
    REQUIRE(page.messages.size() == 2);
    CHECK(page.messages[0].message == "hi");
    CHECK(page.messages[1].message == "hey alice");
    CHECK(page.has_more == false);
}

TEST_CASE("GetLobbyHistoryPage: unknown lobby code returns an empty page") {
    ChatService svc;

    auto page = svc.GetLobbyHistoryPage("chat_lobby_test_ghost", std::nullopt, 20);

    CHECK(page.messages.empty());
    CHECK(page.has_more == false);
}

TEST_CASE("GetLobbyHistoryPage: before_id cursor pages older shards oldest-first") {
    ChatService svc(Database::Get(), -1, -1, 2);

    svc.PostLobbyMessage("LOBBY3", "alice", "msg0");
    svc.PostLobbyMessage("LOBBY3", "alice", "msg1");
    svc.PostLobbyMessage("LOBBY3", "alice", "msg2");

    auto first = svc.GetLobbyHistoryPage("LOBBY3", std::nullopt, 0);
    REQUIRE(first.messages.size() == 2);
    CHECK(first.messages[0].message == "msg1");
    CHECK(first.messages[1].message == "msg2");
    CHECK(first.has_more == true);

    auto second = svc.GetLobbyHistoryPage("LOBBY3", first.messages.front().id, 0);
    REQUIRE(second.messages.size() == 1);
    CHECK(second.messages[0].message == "msg0");
    CHECK(second.has_more == false);
}

TEST_CASE("ClearLobbyHistory: drops the lobby's history and id counter") {
    ChatService svc;

    svc.PostLobbyMessage("chat_lobby_test_clear", "alice", "hi");
    svc.ClearLobbyHistory("chat_lobby_test_clear");

    auto page = svc.GetLobbyHistoryPage("chat_lobby_test_clear", std::nullopt, 20);
    CHECK(page.messages.empty());

    // A fresh message after clearing restarts ids from 1, confirming the
    // id counter (not just the message deque) was dropped.
    svc.PostLobbyMessage("chat_lobby_test_clear", "alice", "hi again");
    auto after = svc.GetLobbyHistoryPage("chat_lobby_test_clear", std::nullopt, 20);
    REQUIRE(after.messages.size() == 1);
    CHECK(after.messages[0].id == 1);
}

}  // TEST_SUITE
