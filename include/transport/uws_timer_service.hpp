#pragma once
#include <transport/itimer_service.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <App.h>

/**
 * @class UwsTimerService
 * @brief Production ITimerService backed by libuv timers via uWebSockets.
 *
 * Manages a keyed map of us_timer_t* instances. Repeated Schedule() calls
 * with the same key cancel the prior timer before arming the new one.
 * The service must be constructed after the uWS event loop is running
 * (i.e. after the WebServer is constructed).
 */
class UwsTimerService : public ITimerService {
public:
    UwsTimerService() = default;
    ~UwsTimerService();

    void Schedule(const std::string& key, int ms, bool repeat,
                  std::function<void()> cb) override;

    void Cancel(const std::string& key) override;

private:
    struct TimerData {
        std::string            key;
        std::function<void()>  callback;
        UwsTimerService*       service;
        bool                   repeat;
        us_timer_t*            timer;
        // INFO: one-shot handles stay in the map idle after firing so the
        //       next Schedule() for the same key re-arms the same handle
        //       instead of us_create/us_close per turn (backend-perf Task 9).
        //       Timer keys are stable per lobby ("turn_<id>", "window_<id>",
        //       "ready_<id>"), so the map stays bounded; Cancel() closes.
        bool                   armed = true;
    };

    std::unordered_map<std::string, std::unique_ptr<TimerData>> timers_;

    void CancelLocked(const std::string& key);
    /** @brief libuv fire callback: repeat runs in place, one-shot parks idle. */
    static void FireTimer(TimerData* data);
};
