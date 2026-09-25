#include "common/rate_limiter.hpp"

#include <algorithm>

RateLimiter::RateLimiter(double capacity, double refill_per_sec)
    : capacity_(capacity), refill_per_sec_(refill_per_sec) {}

bool RateLimiter::AllowAt(const std::string& key, Clock::time_point now) {
    // A fresh key starts with a full bucket so a first request is always allowed.
    auto [it, inserted] = buckets_.try_emplace(key, Bucket{capacity_, now});
    Bucket& bucket = it->second;

    if (!inserted) {
        const double elapsed = std::chrono::duration<double>(now - bucket.last).count();
        bucket.tokens = std::min(capacity_, bucket.tokens + elapsed * refill_per_sec_);
    }
    bucket.last = now;

    const bool allowed = bucket.tokens >= 1.0;
    if (allowed) {
        bucket.tokens -= 1.0;
    }

    // INFO: Bound memory under key flooding. Drop idle buckets first, then
    //       hard-trim arbitrary entries oldest-held (unordered_map iteration
    //       order is unspecified but bounded here). Done after the token
    //       decision so erasing the just-inserted bucket cannot dangle
    //       `bucket`. The idle sweep is throttled to once a minute: an
    //       unconditional O(n) scan on every over-cap insert would itself be
    //       a DoS amplifier. Hard-trim runs every time, so the cap holds.
    if (buckets_.size() > kMaxBuckets) {
        if (now - last_evict_ >= std::chrono::seconds(60)) {
            last_evict_ = now;
            EvictBefore(now, std::chrono::seconds(60));
        }
        while (buckets_.size() > kMaxBuckets && !buckets_.empty()) {
            buckets_.erase(buckets_.begin());
        }
    }
    return allowed;
}

void RateLimiter::EvictBefore(Clock::time_point now, std::chrono::seconds max_idle) {
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        if (now - it->second.last > max_idle) {
            it = buckets_.erase(it);
        } else {
            ++it;
        }
    }
}
