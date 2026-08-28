#pragma once
#include <functional>
#include <string>

/**
 * @class ITimerService
 * @brief Abstract interface for scheduling and cancelling keyed timers.
 *
 * Keys are arbitrary strings; repeated `Schedule` with the same key
 * cancels the previous timer before arming the new one.
 */
class ITimerService {
public:
    virtual ~ITimerService() = default;

    /**
     * @brief Arms a timer.
     * @param key    Unique identifier for this timer slot.
     * @param ms     Delay in milliseconds before the first fire.
     * @param repeat If true, the callback fires repeatedly every `ms` ms.
     * @param cb     Callback to invoke on each fire.
     */
    virtual void Schedule(const std::string& key, int ms, bool repeat,
                          std::function<void()> cb) = 0;

    /**
     * @brief Disarms and destroys the timer registered under `key`, if any.
     */
    virtual void Cancel(const std::string& key) = 0;
};
