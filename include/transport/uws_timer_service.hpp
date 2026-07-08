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
 * @tag TIMER-UWS-001
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
    };

    std::unordered_map<std::string, std::unique_ptr<TimerData>> timers_;

    void CancelLocked(const std::string& key);
};
