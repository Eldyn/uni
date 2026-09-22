#pragma once
#include <transport/itimer_service.hpp>
#include <functional>
#include <string>
#include <map>

/**
 * @class FakeTimerService
 * @brief Test double for ITimerService.
 *
 * Stores callbacks by key; timers never fire automatically.
 * Tests call Fire(key) or FireAll() to trigger them deterministically.
 */
class FakeTimerService : public ITimerService {
public:
    void Schedule(const std::string& key, int, bool,
                  std::function<void()> cb) override {
        callbacks_[key] = std::move(cb);
    }

    void Cancel(const std::string& key) override {
        callbacks_.erase(key);
    }

    /** @brief Fire the callback registered under `key`, if any. */
    void Fire(const std::string& key) {
        auto it = callbacks_.find(key);
        if (it == callbacks_.end()) return;
        // INFO: copy before invoking: a callback is allowed to re-arm its own
        //       key (Schedule replaces the slot), which would otherwise destroy
        //       the std::function while it is still executing.
        std::function<void()> cb = it->second;
        cb();
    }

    /** @brief Fire all registered callbacks in insertion order. */
    void FireAll() {
        for (auto& [k, cb] : callbacks_) cb();
    }

    bool Has(const std::string& key) const {
        return callbacks_.count(key) > 0;
    }

private:
    std::map<std::string, std::function<void()>> callbacks_;
};
