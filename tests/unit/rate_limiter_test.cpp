#include <doctest/doctest.h>
#include "common/rate_limiter.hpp"

#include <chrono>
#include <string>

using Clock = RateLimiter::Clock;
using namespace std::chrono_literals;

TEST_CASE("burst is capped at capacity") {
    RateLimiter rl(3, 1);  // 3-token burst, 1 token/sec
    const auto t0 = Clock::now();

    CHECK(rl.AllowAt("ip", t0));
    CHECK(rl.AllowAt("ip", t0));
    CHECK(rl.AllowAt("ip", t0));
    CHECK_FALSE(rl.AllowAt("ip", t0));  // bucket empty
}

TEST_CASE("tokens refill over time") {
    RateLimiter rl(1, 2);  // 1-token burst, 2 tokens/sec → one token every 0.5s
    const auto t0 = Clock::now();

    CHECK(rl.AllowAt("ip", t0));
    CHECK_FALSE(rl.AllowAt("ip", t0));
    CHECK(rl.AllowAt("ip", t0 + 500ms));  // refilled
}

TEST_CASE("keys are limited independently") {
    RateLimiter rl(1, 1);
    const auto t0 = Clock::now();

    CHECK(rl.AllowAt("a", t0));
    CHECK(rl.AllowAt("b", t0));       // different key, own bucket
    CHECK_FALSE(rl.AllowAt("a", t0)); // "a" already spent
}

TEST_CASE("refill never exceeds capacity") {
    RateLimiter rl(2, 100);  // huge refill rate
    const auto t0 = Clock::now();

    // After a long idle the bucket is full, not overfilled: only `capacity`
    // requests succeed back-to-back.
    CHECK(rl.AllowAt("ip", t0 + 10s));
    CHECK(rl.AllowAt("ip", t0 + 10s));
    CHECK_FALSE(rl.AllowAt("ip", t0 + 10s));
}

TEST_CASE("eviction drops idle buckets") {
    RateLimiter rl(1, 1);
    const auto t0 = Clock::now();

    rl.AllowAt("a", t0);
    CHECK(rl.Size() == 1);

    rl.EvictBefore(t0 + 400s, 300s);  // idle 400s > 300s threshold
    CHECK(rl.Size() == 0);
}

TEST_CASE("RateLimiter caps tracked buckets") {
    RateLimiter rl(1.0, 1.0);
    for (std::size_t i = 0; i < RateLimiter::kMaxBuckets + 1000; ++i) {
        rl.AllowAt("ip-" + std::to_string(i), RateLimiter::Clock::now());
    }
    CHECK(rl.Size() <= RateLimiter::kMaxBuckets);
}
