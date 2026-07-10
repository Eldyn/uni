#include <doctest/doctest.h>
#include <services/friend_service.hpp>
#include <database.hpp>

namespace {

// friend_test_* usernames avoid colliding with rows other test files insert.
void CleanupTestRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM friends WHERE user_a LIKE 'friend_test_%' OR user_b LIKE 'friend_test_%';");
    REQUIRE(result.has_value());
}

struct FriendFixture {
    FriendFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupTestRows();
    }
    ~FriendFixture() { CleanupTestRows(); }
};

}  // namespace

TEST_SUITE("FriendService") {

TEST_CASE("SendRequest: creates a pending request visible to both sides") {
    FriendFixture f;
    FriendService svc;

    auto result = svc.SendRequest("friend_test_alice", "friend_test_bob");
    REQUIRE(result.has_value());

    auto outgoing = svc.GetOutgoingRequests("friend_test_alice");
    REQUIRE(outgoing.has_value());
    CHECK(outgoing->size() == 1);
    CHECK(outgoing->at(0) == "friend_test_bob");

    auto incoming = svc.GetIncomingRequests("friend_test_bob");
    REQUIRE(incoming.has_value());
    CHECK(incoming->size() == 1);
    CHECK(incoming->at(0) == "friend_test_alice");
}

TEST_CASE("SendRequest: rejects sending a request to yourself") {
    FriendFixture f;
    FriendService svc;

    auto result = svc.SendRequest("friend_test_alice", "friend_test_alice");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Error::Code::kBadRequest);
}

TEST_CASE("SendRequest: rejects a duplicate pending request") {
    FriendFixture f;
    FriendService svc;

    REQUIRE(svc.SendRequest("friend_test_alice", "friend_test_bob").has_value());
    auto result = svc.SendRequest("friend_test_alice", "friend_test_bob");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Error::Code::kConflict);
}

TEST_CASE("SendRequest: rejects a request when already friends") {
    FriendFixture f;
    FriendService svc;

    REQUIRE(svc.SendRequest("friend_test_alice", "friend_test_bob").has_value());
    REQUIRE(svc.RespondToRequest("friend_test_bob", "friend_test_alice", true).has_value());

    auto result = svc.SendRequest("friend_test_alice", "friend_test_bob");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Error::Code::kConflict);
}

TEST_CASE("RespondToRequest: accepting moves the pair into both friend lists") {
    FriendFixture f;
    FriendService svc;

    REQUIRE(svc.SendRequest("friend_test_alice", "friend_test_bob").has_value());
    REQUIRE(svc.RespondToRequest("friend_test_bob", "friend_test_alice", true).has_value());

    auto alice_friends = svc.GetFriends("friend_test_alice");
    REQUIRE(alice_friends.has_value());
    CHECK(alice_friends->size() == 1);
    CHECK(alice_friends->at(0) == "friend_test_bob");

    auto bob_friends = svc.GetFriends("friend_test_bob");
    REQUIRE(bob_friends.has_value());
    CHECK(bob_friends->size() == 1);
    CHECK(bob_friends->at(0) == "friend_test_alice");

    CHECK(svc.GetIncomingRequests("friend_test_bob")->empty());
    CHECK(svc.GetOutgoingRequests("friend_test_alice")->empty());
}

TEST_CASE("RespondToRequest: declining removes the request without creating a friendship") {
    FriendFixture f;
    FriendService svc;

    REQUIRE(svc.SendRequest("friend_test_alice", "friend_test_bob").has_value());
    REQUIRE(svc.RespondToRequest("friend_test_bob", "friend_test_alice", false).has_value());

    CHECK(svc.GetFriends("friend_test_alice")->empty());
    CHECK(svc.GetFriends("friend_test_bob")->empty());
    CHECK(svc.GetIncomingRequests("friend_test_bob")->empty());
}

TEST_CASE("RespondToRequest: rejects responding to a request that doesn't exist") {
    FriendFixture f;
    FriendService svc;

    auto result = svc.RespondToRequest("friend_test_bob", "friend_test_alice", true);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Error::Code::kNotFound);
}

TEST_CASE("RemoveFriend: removes an accepted friendship from both sides") {
    FriendFixture f;
    FriendService svc;

    REQUIRE(svc.SendRequest("friend_test_alice", "friend_test_bob").has_value());
    REQUIRE(svc.RespondToRequest("friend_test_bob", "friend_test_alice", true).has_value());

    auto result = svc.RemoveFriend("friend_test_alice", "friend_test_bob");
    REQUIRE(result.has_value());

    CHECK(svc.GetFriends("friend_test_alice")->empty());
    CHECK(svc.GetFriends("friend_test_bob")->empty());
}

TEST_CASE("RemoveFriend: rejects removing a friendship that doesn't exist") {
    FriendFixture f;
    FriendService svc;

    auto result = svc.RemoveFriend("friend_test_alice", "friend_test_bob");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Error::Code::kNotFound);
}

}  // TEST_SUITE
