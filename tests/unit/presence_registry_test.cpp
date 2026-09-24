#include <doctest/doctest.h>

#include <transport/presence_registry.hpp>

#include <cstdint>
#include <string>

/**
 * @file presence_registry_test.cpp
 * @brief Tests for the cheap live-connection count backing `/stats/online`.
 *
 * `OnlineCount()` is `sockets_.size()`; these cases pin the open/close
 * bookkeeping (unique usernames, overwrite on reconnect) without touching a
 * real uWS socket — the registry only stores the pointer, never dereferences it.
 */

namespace {

/** @brief Opaque fake socket pointer; the registry stores it but never dereferences. */
AppWebSocket* FakeSocket(std::uintptr_t id) { return reinterpret_cast<AppWebSocket*>(id); }

PerSocketData SocketFor(const std::string& username) {
    PerSocketData sd;
    sd.username = username;
    return sd;
}

}  // namespace

TEST_SUITE("PresenceRegistry") {

TEST_CASE("OnlineCount starts at zero") {
    PresenceRegistry presence;
    CHECK(presence.OnlineCount() == 0);
}

TEST_CASE("OnlineCount tracks distinct connected usernames") {
    PresenceRegistry presence;
    auto sd_a = SocketFor("alice");
    auto sd_b = SocketFor("bob");

    presence.OnOpen(FakeSocket(1), &sd_a);
    CHECK(presence.OnlineCount() == 1);

    presence.OnOpen(FakeSocket(2), &sd_b);
    CHECK(presence.OnlineCount() == 2);

    presence.OnClose(FakeSocket(1), &sd_a);
    CHECK(presence.OnlineCount() == 1);
    CHECK(presence.IsOnline("bob"));
    CHECK_FALSE(presence.IsOnline("alice"));

    presence.OnClose(FakeSocket(2), &sd_b);
    CHECK(presence.OnlineCount() == 0);
}

TEST_CASE("Reconnect keeps one entry per username") {
    PresenceRegistry presence;
    auto sd = SocketFor("carol");

    presence.OnOpen(FakeSocket(1), &sd);
    presence.OnOpen(FakeSocket(2), &sd);  // same user, fresh socket
    CHECK(presence.OnlineCount() == 1);
    CHECK(presence.GetSocket("carol") == FakeSocket(2));

    // The stale socket's close must not evict the live rebind.
    presence.OnClose(FakeSocket(1), &sd);
    CHECK(presence.OnlineCount() == 1);

    presence.OnClose(FakeSocket(2), &sd);
    CHECK(presence.OnlineCount() == 0);
}

}  // TEST_SUITE
