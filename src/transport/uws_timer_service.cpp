#include <transport/uws_timer_service.hpp>

#include <logger.hpp>
#include <utility>

namespace {

// INFO: Timer callbacks run engine and lobby code outside any WS handler, so
//       nothing above them catches. An escaped exception would reach
//       std::terminate and take every lobby down; log it and keep the loop.
void RunGuarded(const std::string& key, const std::function<void()>& callback) {
    try {
        callback();
    } catch (const std::exception& e) {
        Logger::Error("[Timer] Exception in callback '", key, "': ", e.what());
    } catch (...) {
        Logger::Error("[Timer] Unknown exception in callback '", key, "'");
    }
}

}  // namespace

UwsTimerService::~UwsTimerService() {
    for (auto& [key, data] : timers_) {
        us_timer_close(data->timer);
    }
}

void UwsTimerService::FireTimer(TimerData* d) {
    if (d->repeat) {
        RunGuarded(d->key, d->callback);
        return;
    }
    // INFO: one-shot: park the handle idle BEFORE the callback so the next
    //       Schedule() for this key re-arms it with no us_create/us_close.
    //       A Cancel() from inside the callback still closes + erases, so
    //       `d` must not be touched after RunGuarded (it may be freed).
    std::function<void()> cb = std::move(d->callback);
    std::string key = d->key;
    d->armed = false;
    if (cb) RunGuarded(key, cb);
}

void UwsTimerService::Schedule(const std::string& key, int ms, bool repeat,
                                std::function<void()> cb) {
    // INFO: re-arm path (backend-perf Task 9): the turn timer is scheduled
    //       once per move on a stable per-lobby key, so reuse the live
    //       handle via us_timer_set (which restarts the countdown) instead
    //       of paying us_close + us_create per move.
    if (auto it = timers_.find(key); it != timers_.end()) {
        TimerData* data = it->second.get();
        data->callback = std::move(cb);
        data->repeat = repeat;
        data->armed = true;
        us_timer_set(data->timer, [](us_timer_t* t) {
            auto* d = *reinterpret_cast<TimerData**>(us_timer_ext(t));
            FireTimer(d);
        }, ms, repeat ? ms : 0);
        return;
    }

    auto* loop  = reinterpret_cast<us_loop_t*>(uWS::Loop::get());
    auto* timer = us_create_timer(loop, 0, sizeof(TimerData*));
    auto  data  = std::make_unique<TimerData>(key, std::move(cb), this, repeat, timer);
    *reinterpret_cast<TimerData**>(us_timer_ext(timer)) = data.get();

    us_timer_set(timer, [](us_timer_t* t) {
        auto* d = *reinterpret_cast<TimerData**>(us_timer_ext(t));
        FireTimer(d);
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
