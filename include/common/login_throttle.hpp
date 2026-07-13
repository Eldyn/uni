#pragma once
#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

/**
 * @file login_throttle.hpp
 * @brief Temporary lockout for repeated failed logins, keyed by an arbitrary
 *        string (e.g. "email|ip").
 */

/**
 * @class LoginThrottle
 * @brief Counts failed login attempts per key and locks the key out once a
 *        threshold is reached within a rolling window.
 *
 * This blunts password brute-forcing and, just as importantly, the PBKDF2 CPU
 * amplification that each verification costs, a locked key is rejected before
 * any hashing happens. Failures decay after one lockout window, and a successful
 * login clears the key via `Reset`.
 *
 * Not thread-safe (single event loop). The time source is injectable (`*At`
 * overloads) for deterministic unit tests.
 * @tag CMN-LT-CLS-001
 */
class LoginThrottle {
public:
    using Clock = std::chrono::steady_clock;

    /**
     * @param max_fails Failures within one window before the key is locked.
     * @param lockout   Lock duration and the rolling failure window.
     */
    LoginThrottle(int max_fails, std::chrono::seconds lockout);

    /** @brief Whether @p key is currently locked out. */
    bool IsLocked(const std::string& key) { return IsLockedAt(key, Clock::now()); }
    bool IsLockedAt(const std::string& key, Clock::time_point now);

    /** @brief Record one failed attempt for @p key. */
    void RecordFailure(const std::string& key) { RecordFailureAt(key, Clock::now()); }
    void RecordFailureAt(const std::string& key, Clock::time_point now);

    /** @brief Clear @p key after a successful login. */
    void Reset(const std::string& key) { entries_.erase(key); }

    /** @brief Drop unlocked entries idle longer than @p max_idle. */
    void Evict(std::chrono::seconds max_idle = std::chrono::seconds(900)) {
        EvictBefore(Clock::now(), max_idle);
    }
    void EvictBefore(Clock::time_point now, std::chrono::seconds max_idle);

    std::size_t Size() const { return entries_.size(); }

private:
    struct Entry {
        int               fails;
        Clock::time_point window_start;
        Clock::time_point locked_until;
    };

    int                  max_fails_;
    std::chrono::seconds lockout_;
    std::unordered_map<std::string, Entry> entries_;
};
