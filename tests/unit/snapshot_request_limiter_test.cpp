#include <doctest/doctest.h>

#include <match/server/snapshot_request_limiter.hpp>

#include <cstdlib>

/**
 * @file snapshot_request_limiter_test.cpp
 * @brief Rate limit for the `match_snapshot_request` message.
 */

using match::server::SnapshotRequestLimiter;

TEST_CASE("snapshot limiter: a flood inside the interval is refused") {
    SnapshotRequestLimiter limiter(1000);

    CHECK(limiter.Allow("player0", 0));
    CHECK_FALSE(limiter.Allow("player0", 1));
    CHECK_FALSE(limiter.Allow("player0", 999));
    CHECK(limiter.Allow("player0", 1000));
}

TEST_CASE("snapshot limiter: a refused request does not extend the wait") {
    SnapshotRequestLimiter limiter(1000);

    CHECK(limiter.Allow("player0", 0));
    for (int64_t now_ms = 100; now_ms < 1000; now_ms += 100) {
        CHECK_FALSE(limiter.Allow("player0", now_ms));
    }
    CHECK(limiter.Allow("player0", 1000));
}

TEST_CASE("snapshot limiter: each recipient has its own interval") {
    SnapshotRequestLimiter limiter(1000);

    CHECK(limiter.Allow("player0", 0));
    CHECK(limiter.Allow("player1", 1));
    CHECK_FALSE(limiter.Allow("player1", 2));
}

TEST_CASE("snapshot limiter: the environment sets the interval") {
    setenv(match::server::kSnapshotRequestIntervalEnv, "50", 1);
    SnapshotRequestLimiter limiter = SnapshotRequestLimiter::FromEnv();
    unsetenv(match::server::kSnapshotRequestIntervalEnv);

    CHECK(limiter.Allow("player0", 0));
    CHECK_FALSE(limiter.Allow("player0", 49));
    CHECK(limiter.Allow("player0", 50));
}

TEST_CASE("snapshot limiter: the default interval applies without env") {
    unsetenv(match::server::kSnapshotRequestIntervalEnv);
    SnapshotRequestLimiter limiter = SnapshotRequestLimiter::FromEnv();

    CHECK(limiter.Allow("player0", 0));
    CHECK_FALSE(limiter.Allow(
        "player0", match::server::kDefaultSnapshotRequestIntervalMs - 1));
    CHECK(limiter.Allow("player0",
                        match::server::kDefaultSnapshotRequestIntervalMs));
}
