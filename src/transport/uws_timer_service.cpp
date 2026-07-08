#include <transport/uws_timer_service.hpp>

#include <utility>

UwsTimerService::~UwsTimerService() {
    for (auto& [key, data] : timers_) {
        us_timer_close(data->timer);
    }
}

void UwsTimerService::Schedule(const std::string& key, int ms, bool repeat,
                                std::function<void()> cb) {
    CancelLocked(key);

    auto* loop  = reinterpret_cast<us_loop_t*>(uWS::Loop::get());
    auto* timer = us_create_timer(loop, 0, sizeof(TimerData*));
    auto  data  = std::make_unique<TimerData>(key, std::move(cb), this, repeat, timer);
    *reinterpret_cast<TimerData**>(us_timer_ext(timer)) = data.get();

    us_timer_set(timer, [](us_timer_t* t) {
        auto* d = *reinterpret_cast<TimerData**>(us_timer_ext(t));

        if (d->repeat) {
            d->callback();
            return;
        }

        // INFO: One-shot: erase from map before the callback so a Cancel()
        //       called from within the callback is a no-op. Ownership is
        //       moved out first so TimerData outlives the erase and stays
        //       valid until the callback returns.
        std::unique_ptr<TimerData> owned;
        auto it = d->service->timers_.find(d->key);
        if (it != d->service->timers_.end()) {
            owned = std::move(it->second);
            d->service->timers_.erase(it);
        }
        auto cb = std::move(d->callback);
        us_timer_close(t);
        if (cb) cb();
    }, ms, repeat ? ms : 0);

    timers_[key] = std::move(data);
}

void UwsTimerService::Cancel(const std::string& key) {
    CancelLocked(key);
}

void UwsTimerService::CancelLocked(const std::string& key) {
    auto it = timers_.find(key);
    if (it == timers_.end()) return;

    us_timer_close(it->second->timer);
    timers_.erase(it);
}
