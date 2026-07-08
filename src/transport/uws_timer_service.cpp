#include <transport/uws_timer_service.hpp>

UwsTimerService::~UwsTimerService() {
    for (auto& [key, timer] : timers_) {
        TimerData* data = *reinterpret_cast<TimerData**>(us_timer_ext(timer));
        delete data;
        us_timer_close(timer);
    }
}

void UwsTimerService::Schedule(const std::string& key, int ms, bool repeat,
                                std::function<void()> cb) {
    CancelLocked(key);

    auto* loop  = reinterpret_cast<us_loop_t*>(uWS::Loop::get());
    auto* timer = us_create_timer(loop, 0, sizeof(TimerData*));
    auto* data  = new TimerData{key, std::move(cb), this, repeat};
    *reinterpret_cast<TimerData**>(us_timer_ext(timer)) = data;

    us_timer_set(timer, [](us_timer_t* t) {
        auto* d = *reinterpret_cast<TimerData**>(us_timer_ext(t));

        if (d->repeat) {
            d->callback();
            return;
        }

        // INFO: One-shot: erase from map before the callback so a Cancel()
        //       called from within the callback is a no-op.
        d->service->timers_.erase(d->key);
        auto cb = std::move(d->callback);
        delete d;
        us_timer_close(t);
        if (cb) cb();
    }, ms, repeat ? ms : 0);

    timers_[key] = timer;
}

void UwsTimerService::Cancel(const std::string& key) {
    CancelLocked(key);
}

void UwsTimerService::CancelLocked(const std::string& key) {
    auto it = timers_.find(key);
    if (it == timers_.end()) return;

    auto* timer = it->second;
    auto* data  = *reinterpret_cast<TimerData**>(us_timer_ext(timer));
    delete data;
    us_timer_close(timer);
    timers_.erase(it);
}
