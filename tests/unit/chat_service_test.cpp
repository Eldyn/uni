#include <doctest/doctest.h>
#include <services/chat_service.hpp>

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

}  // TEST_SUITE
