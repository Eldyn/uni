#pragma once
#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

/**
 * @file rate_limiter.hpp
 * @brief Token-bucket rate limiter keyed by an arbitrary string (client IP,
 *        username, connection id, …).
 */

/**
 * @class RateLimiter
 * @brief Per-key token bucket.
 *
 * Each key owns a bucket that holds up to `capacity` tokens and refills at
 * `refill_per_sec`. `Allow` consumes one token, returning false when the bucket
 * is empty. This caps both the sustained rate (refill) and the burst (capacity).
 *
 * Not thread-safe by design: it is meant to run on the single uWebSockets event
 * loop, so it avoids per-call locking. The time source is injectable (`*At`
 * overloads) so the behaviour can be unit-tested deterministically without
 * sleeping.
 */
class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    /** @brief Hard cap on tracked buckets, bounding memory under key flooding. */
    static constexpr std::size_t kMaxBuckets = 200'000;

    /**
     * @param capacity       Maximum burst, i.e. bucket size in tokens.
     * @param refill_per_sec Tokens replenished per second (sustained rate).
     */
    RateLimiter(double capacity, double refill_per_sec);

    /** @brief Consume one token for @p key at the current time. */
    bool Allow(const std::string& key) { return AllowAt(key, Clock::now()); }

    /** @brief Consume one token for @p key at an explicit time (testable core). */
    bool AllowAt(const std::string& key, Clock::time_point now);

    /** @brief Drop buckets idle longer than @p max_idle, bounding memory use. */
    void Evict(std::chrono::seconds max_idle = std::chrono::seconds(300)) {
        EvictBefore(Clock::now(), max_idle);
    }

    /** @brief Eviction core with an explicit reference time (testable). */
    void EvictBefore(Clock::time_point now, std::chrono::seconds max_idle);

    /** @brief Number of tracked keys (metrics / tests). */
    std::size_t Size() const { return buckets_.size(); }

private:
    struct Bucket {
        double            tokens;
        Clock::time_point last;
    };

    double capacity_;
    double refill_per_sec_;
    std::unordered_map<std::string, Bucket> buckets_;
    /** Last time idle buckets were swept, bounding the cap's scan cost. */
    Clock::time_point last_evict_{};
};
