#pragma once

#include <common/env.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

/**
 * @file snapshot_request_limiter.hpp
 * @brief Rate limit for the client `match_snapshot_request` message.
 */

namespace match::server {

/** Default minimum time between two snapshot requests of one recipient. */
inline constexpr int64_t kDefaultSnapshotRequestIntervalMs = 1000;

/** Environment variable that overrides the minimum interval. */
inline constexpr const char* kSnapshotRequestIntervalEnv =
    "SNAPSHOT_REQUEST_MIN_INTERVAL_MS";

/**
 * @class SnapshotRequestLimiter
 * @brief Allows one snapshot request per recipient in each interval.
 *
 * A request that arrives inside the interval is refused and does not move
 * the interval start, so a flood cannot extend its own refusal.
 */
class SnapshotRequestLimiter {
public:
    explicit SnapshotRequestLimiter(
        int64_t min_interval_ms = kDefaultSnapshotRequestIntervalMs)
        : min_interval_ms_(min_interval_ms) {}

    /** @brief Build a limiter from `SNAPSHOT_REQUEST_MIN_INTERVAL_MS`. */
    static SnapshotRequestLimiter FromEnv() {
        const int interval_ms = Env::GetInt(
            kSnapshotRequestIntervalEnv,
            static_cast<int>(kDefaultSnapshotRequestIntervalMs));
        return SnapshotRequestLimiter(interval_ms < 0 ? 0 : interval_ms);
    }

    void SetMinIntervalMs(int64_t min_interval_ms) {
        min_interval_ms_ = min_interval_ms;
    }

    /**
     * @brief True when `recipient` may request a snapshot at `now_ms`.
     *
     * An allowed request starts a new interval for the recipient.
     */
    bool Allow(const std::string& recipient, int64_t now_ms) {
        const auto last = last_allowed_ms_.find(recipient);
        if (last != last_allowed_ms_.end()
            && now_ms - last->second < min_interval_ms_) {
            return false;
        }
        last_allowed_ms_[recipient] = now_ms;
        return true;
    }

private:
    int64_t min_interval_ms_;
    std::unordered_map<std::string, int64_t> last_allowed_ms_;
};

}  // namespace match::server
