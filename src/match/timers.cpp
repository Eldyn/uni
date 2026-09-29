#include <match/timers.hpp>

#include <common/env.hpp>
#include <logger.hpp>

#include <algorithm>
#include <string>
#include <utility>

/**
 * @file timers.cpp
 * @brief Disjoint turn/window timer bodies.
 */

namespace match {

std::string_view WindowModeToken(WindowMode mode) {
    switch (mode) {
        case WindowMode::kFixed:
            return "fixed";
        case WindowMode::kHalfTurn:
            return "half_turn";
    }
    return "fixed";
}

WindowConfig WindowConfig::FromEnv() {
    WindowConfig config;
    const int configured_ms = Env::GetInt("UNI_WINDOW_MS", 7000);
    if (configured_ms >= 0) {
        config.window_ms = configured_ms;
    } else {
        // INFO: fail-safe. A negative length would arm an already-expired
        //       window and instantly fire the default route.
        Logger::Warn("[Timers] UNI_WINDOW_MS is negative; using 7000");
    }

    const std::string mode = Env::Get("UNI_WINDOW_MODE", "fixed");
    if (mode == "half_turn") {
        config.mode = WindowMode::kHalfTurn;
    } else if (mode != "fixed" && !mode.empty()) {
        Logger::Warn("[Timers] unknown UNI_WINDOW_MODE '" + mode +
                     "'; using fixed");
    }
    return config;
}

TurnTimer::TurnTimer(NowMs clock) : clock_(std::move(clock)) {}

bool TurnTimer::Arm(ecs::TurnState& turn, int64_t duration_ms) {
    if (duration_ms < 0) return false;
    turn.turn_deadline_ms = clock_() + duration_ms;
    suspended_ = false;
    has_remaining_ = false;
    remaining_ms_ = 0;
    return true;
}

bool TurnTimer::Arm(ecs::TurnState& turn, const ecs::DurationSpec& duration) {
    if (duration.unit != ecs::DurationUnit::kMs) return false;
    return Arm(turn, duration.value);
}

bool TurnTimer::Tick(const ecs::TurnState& turn) const {
    if (suspended_) return false;
    return turn.turn_deadline_ms != 0 && clock_() >= turn.turn_deadline_ms;
}

int64_t TurnTimer::Suspend(ecs::TurnState& turn) {
    if (suspended_) return RemainingMs();
    if (turn.turn_deadline_ms == 0) {
        // INFO: no active turn: nothing to preserve, but still mark
        //       suspended so `Tick` stays quiet until Resume.
        suspended_ = true;
        has_remaining_ = false;
        remaining_ms_ = 0;
        return 0;
    }
    // INFO: may be <= 0 if the deadline already passed at suspend time.
    //       Resume restores it verbatim so expiry is not silently lost.
    remaining_ms_ = turn.turn_deadline_ms - clock_();
    has_remaining_ = true;
    suspended_ = true;
    turn.turn_deadline_ms = 0;
    return remaining_ms_;
}

bool TurnTimer::Resume(ecs::TurnState& turn) {
    if (!suspended_) return false;
    bool restored = false;
    if (has_remaining_) {
        turn.turn_deadline_ms = clock_() + remaining_ms_;
        restored = true;
    }
    suspended_ = false;
    has_remaining_ = false;
    remaining_ms_ = 0;
    return restored;
}

void TurnTimer::Reset() {
    suspended_ = false;
    has_remaining_ = false;
    remaining_ms_ = 0;
}

WindowTimer::WindowTimer(NowMs clock)
    : config_(WindowConfig::FromEnv()), clock_(std::move(clock)) {}

WindowTimer::WindowTimer(WindowConfig config, NowMs clock)
    : config_(config), clock_(std::move(clock)) {}

int64_t WindowTimer::ComputeDurationMs(
    int64_t remaining_turn_ms, bool has_remaining,
    std::optional<int64_t> override_ms) const {
    const int64_t base = override_ms.value_or(config_.window_ms);
    if (config_.mode != WindowMode::kHalfTurn) return base;
    if (!has_remaining) return base;
    int64_t half = remaining_turn_ms / 2;
    if (half < 0) half = 0;
    return std::min<int64_t>(base, half);
}

WindowDuration WindowTimer::Open(ecs::WindowState& window,
                                 int64_t remaining_turn_ms,
                                 bool has_remaining,
                                 std::optional<int64_t> override_ms) const {
    WindowDuration result;
    result.window_ms = config_.window_ms;
    result.mode = config_.mode;
    result.remaining_turn_ms = remaining_turn_ms;
    result.turn_remaining_known = has_remaining;
    result.duration_ms =
        ComputeDurationMs(remaining_turn_ms, has_remaining, override_ms);

    window.responses.clear();
    window.deadline_ms = clock_() + result.duration_ms;
    window.open = true;
    return result;
}

bool WindowTimer::Tick(const ecs::WindowState& window) const {
    return window.open && window.deadline_ms != 0 &&
           clock_() >= window.deadline_ms;
}

bool WindowTimer::AllResponded(const ecs::WindowState& window) const {
    if (window.responders.empty()) return false;
    for (const ecs::Entity& responder : window.responders) {
        bool replied = false;
        for (const ecs::WindowResponse& response : window.responses) {
            if (response.responder == responder) {
                replied = true;
                break;
            }
        }
        if (!replied) return false;
    }
    return true;
}

void WindowTimer::Close(ecs::WindowState& window) const {
    window.open = false;
    window.deadline_ms = 0;
}

MatchTimers::MatchTimers(NowMs clock)
    : clock_(std::move(clock)), turn_(clock_), window_(clock_) {}

MatchTimers::MatchTimers(WindowConfig config, NowMs clock)
    : clock_(std::move(clock)), turn_(clock_), window_(config, clock_) {}

WindowDuration MatchTimers::OpenWindow(ecs::EntityStore& store,
                                       ecs::Entity match,
                                       ecs::Entity current_player,
                                       std::optional<int64_t> override_ms) {
    ecs::WindowState* window = store.Get<ecs::WindowState>(match);
    if (window == nullptr) return WindowDuration{};

    int64_t remaining = 0;
    bool has_remaining = false;
    ecs::TurnState* turn = store.Get<ecs::TurnState>(current_player);
    if (turn != nullptr) {
        remaining = turn_.Suspend(*turn);
        has_remaining = turn_.HasRemaining();
    }
    return window_.Open(*window, remaining, has_remaining, override_ms);
}

bool MatchTimers::CloseWindow(ecs::EntityStore& store, ecs::Entity match,
                              ecs::Entity current_player) {
    ecs::WindowState* window = store.Get<ecs::WindowState>(match);
    if (window != nullptr) window_.Close(*window);

    ecs::TurnState* turn = store.Get<ecs::TurnState>(current_player);
    return turn != nullptr && turn_.Resume(*turn);
}

MatchTimerTick MatchTimers::Tick(ecs::EntityStore& store, ecs::Entity match,
                                 ecs::Entity current_player) {
    MatchTimerTick result;
    result.match = match;

    ecs::WindowState* window = store.Get<ecs::WindowState>(match);
    ecs::TurnState* turn = store.Get<ecs::TurnState>(current_player);

    if (window != nullptr && window->open) {
        // INFO: disjointness — while the window runs, only the window timer
        //       is consulted; the suspended turn timer is not checked.
        if (window_.Tick(*window)) {
            result.window_timeout = true;
            result.default_route = window->default_route;
            window->open = false;
            window->deadline_ms = 0;
            result.turn_resumed = turn != nullptr && turn_.Resume(*turn);
        } else if (window_.AllResponded(*window)) {
            result.window_early_closed = true;
            window->open = false;
            window->deadline_ms = 0;
            result.turn_resumed = turn != nullptr && turn_.Resume(*turn);
        }
        return result;
    }

    if (turn != nullptr && turn_.Tick(*turn)) {
        result.turn_expired = true;
    }
    return result;
}

}  // namespace match
