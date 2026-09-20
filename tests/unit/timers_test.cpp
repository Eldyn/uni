#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/duration.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/timers.hpp>

#include <cstdint>
#include <string>

using match::MatchTimerTick;
using match::MatchTimers;
using match::TurnTimer;
using match::WindowConfig;
using match::WindowDuration;
using match::WindowMode;
using match::WindowTimer;
using match::ecs::DurationSpec;
using match::ecs::DurationUnit;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::TurnState;
using match::ecs::WindowResponse;
using match::ecs::WindowState;

namespace {

/**
 * INFO: one store/match/player/other fixture with three injected clocks
 *       sharing a single `now`. Each timer carries the same deterministic
 *       seam, so class-level and coordinator-level cases read identical time.
 */
struct Harness {
    EntityStore store;
    Entity match = store.Create();
    Entity player = store.Create();
    Entity other = store.Create();
    int64_t now = 10000;
    TurnTimer turn_timer{[this]() { return now; }};
    WindowTimer window_timer{WindowConfig{}, [this]() { return now; }};
    MatchTimers timers{WindowConfig{}, [this]() { return now; }};

    TurnState& Turn() {
        TurnState* existing = store.Get<TurnState>(player);
        if (existing != nullptr) return *existing;
        return *store.Add<TurnState>(player, TurnState{});
    }

    WindowState& Window() {
        WindowState* existing = store.Get<WindowState>(match);
        if (existing != nullptr) return *existing;
        return *store.Add<WindowState>(match, WindowState{});
    }
};

/** INFO: RAII restore of an env key so config cases cannot leak state. */
struct EnvGuard {
    std::string key;
    std::string prior;
    EnvGuard(const char* name, std::string previous)
        : key(name), prior(std::move(previous)) {}
    ~EnvGuard() { Env::SetEnv(key, prior); }
};

}  // namespace

TEST_CASE("timers: turn timer counts down and expires at the deadline") {
    Harness h;
    TurnState& turn = h.Turn();
    REQUIRE(h.turn_timer.Arm(turn, 500));
    CHECK(turn.turn_deadline_ms == h.now + 500);

    CHECK(!h.turn_timer.Tick(turn));
    h.now += 499;
    CHECK(!h.turn_timer.Tick(turn));
    h.now += 1;
    CHECK(h.turn_timer.Tick(turn));
}

TEST_CASE("timers: a 0 deadline never expires and Arm rejects bad input") {
    Harness h;
    TurnState turn;  // local, no deadline
    CHECK(!h.turn_timer.Tick(turn));
    CHECK(!h.turn_timer.Arm(turn, -1));
    CHECK(turn.turn_deadline_ms == 0);

    TurnState& real = h.Turn();
    REQUIRE(h.turn_timer.Arm(real, 100));
    CHECK(!h.turn_timer.Arm(real, DurationSpec{DurationUnit::kTurns, 2}));
    CHECK(real.turn_deadline_ms == h.now + 100);
    REQUIRE(h.turn_timer.Arm(real, DurationSpec{DurationUnit::kMs, 250}));
    CHECK(real.turn_deadline_ms == h.now + 250);
}

TEST_CASE("timers: suspend preserves remaining time; resume restores it") {
    Harness h;
    TurnState& turn = h.Turn();
    REQUIRE(h.turn_timer.Arm(turn, 500));

    h.now = 10200;  // 300 ms left
    const int64_t remaining = h.turn_timer.Suspend(turn);
    CHECK(remaining == 300);
    CHECK(h.turn_timer.Suspended());
    CHECK(h.turn_timer.HasRemaining());
    CHECK(h.turn_timer.RemainingMs() == 300);
    CHECK(turn.turn_deadline_ms == 0);

    h.now = 10300;
    CHECK(!h.turn_timer.Tick(turn));  // suspended: the clock is paused

    REQUIRE(h.turn_timer.Resume(turn));
    CHECK(!h.turn_timer.Suspended());
    CHECK(turn.turn_deadline_ms == 10300 + 300);

    h.now = 10599;
    CHECK(!h.turn_timer.Tick(turn));
    h.now = 10600;
    CHECK(h.turn_timer.Tick(turn));
}

TEST_CASE("timers: suspend with no deadline leaves nothing to resume") {
    Harness h;
    TurnState turn;
    CHECK(h.turn_timer.Suspend(turn) == 0);
    CHECK(h.turn_timer.Suspended());
    CHECK(!h.turn_timer.HasRemaining());
    CHECK(!h.turn_timer.Resume(turn));
    CHECK(turn.turn_deadline_ms == 0);
}

TEST_CASE("timers: an already-passed deadline resumes to immediate expiry") {
    Harness h;
    TurnState& turn = h.Turn();
    REQUIRE(h.turn_timer.Arm(turn, 100));

    h.now = 10500;  // 400 ms past the deadline
    CHECK(h.turn_timer.Suspend(turn) == -400);
    h.now = 20000;
    REQUIRE(h.turn_timer.Resume(turn));
    CHECK(turn.turn_deadline_ms == 20000 - 400);
    CHECK(h.turn_timer.Tick(turn));
}

TEST_CASE("timers: fixed window uses UNI_WINDOW_MS and expires on time") {
    Harness h;
    WindowState& window = h.Window();
    const WindowDuration duration = h.window_timer.Open(window, 1000, true);
    CHECK(duration.mode == WindowMode::kFixed);
    CHECK(duration.duration_ms == 7000);
    CHECK(window.open);
    CHECK(window.deadline_ms == h.now + 7000);

    CHECK(!h.window_timer.Tick(window));
    h.now += 6999;
    CHECK(!h.window_timer.Tick(window));
    h.now += 1;
    CHECK(h.window_timer.Tick(window));
}

TEST_CASE("timers: half_turn caps the window at half the remaining turn") {
    Harness h;
    const WindowTimer half{WindowConfig{7000, WindowMode::kHalfTurn},
                           [&h]() { return h.now; }};
    WindowState& window = h.Window();

    WindowDuration duration = half.Open(window, 2000, true);  // half = 1000
    CHECK(duration.mode == WindowMode::kHalfTurn);
    CHECK(duration.duration_ms == 1000);
    CHECK(window.deadline_ms == h.now + 1000);

    duration = half.Open(window, 100000, true);  // half = 50000 > 7000
    CHECK(duration.duration_ms == 7000);

    duration = half.Open(window, 0, false);  // no turn basis -> fixed
    CHECK(duration.duration_ms == 7000);
    CHECK(!duration.turn_remaining_known);
}

TEST_CASE("timers: early close when every responder passed or acted") {
    Harness h;
    WindowState& window = h.Window();
    window.responders = {h.player, h.other};
    h.window_timer.Open(window, 1000, true);

    CHECK(!h.window_timer.AllResponded(window));
    window.responses.push_back(WindowResponse{h.player, Entity{}, true, 1});
    CHECK(!h.window_timer.AllResponded(window));
    window.responses.push_back(WindowResponse{h.other, Entity{}, false, 2});
    CHECK(h.window_timer.AllResponded(window));
}

TEST_CASE("timers: an empty responder set never reports early close") {
    Harness h;
    WindowState& window = h.Window();
    h.window_timer.Open(window, 1000, true);
    CHECK(!h.window_timer.AllResponded(window));
}

TEST_CASE("timers: config reads UNI_WINDOW_MS and UNI_WINDOW_MODE from Env") {
    EnvGuard ms("UNI_WINDOW_MS", Env::Get("UNI_WINDOW_MS", ""));
    EnvGuard mode("UNI_WINDOW_MODE", Env::Get("UNI_WINDOW_MODE", ""));

    Env::SetEnv("UNI_WINDOW_MS", "1234");
    Env::SetEnv("UNI_WINDOW_MODE", "half_turn");
    WindowConfig config = WindowConfig::FromEnv();
    CHECK(config.window_ms == 1234);
    CHECK(config.mode == WindowMode::kHalfTurn);

    Env::SetEnv("UNI_WINDOW_MODE", "bogus");
    CHECK(WindowConfig::FromEnv().mode == WindowMode::kFixed);

    Env::SetEnv("UNI_WINDOW_MS", "-5");
    CHECK(WindowConfig::FromEnv().window_ms == 7000);

    Env::SetEnv("UNI_WINDOW_MS", "not-a-number");
    CHECK(WindowConfig::FromEnv().window_ms == 7000);
}

TEST_CASE("timers: default env config is 7000 ms fixed") {
    EnvGuard ms("UNI_WINDOW_MS", Env::Get("UNI_WINDOW_MS", ""));
    EnvGuard mode("UNI_WINDOW_MODE", Env::Get("UNI_WINDOW_MODE", ""));
    Env::SetEnv("UNI_WINDOW_MS", "");
    Env::SetEnv("UNI_WINDOW_MODE", "");

    const WindowConfig config = WindowConfig::FromEnv();
    CHECK(config.window_ms == 7000);
    CHECK(config.mode == WindowMode::kFixed);
}

TEST_CASE("timers: coordinator opens a window and suspends the turn clock") {
    Harness h;
    TurnState& turn = h.Turn();
    WindowState& window = h.Window();
    REQUIRE(h.timers.Turn().Arm(turn, 1000));

    window.responders = {h.other};
    window.default_route = "route:pass";
    h.now = 10500;  // 500 ms left on the turn

    const WindowDuration duration =
        h.timers.OpenWindow(h.store, h.match, h.player);
    CHECK(duration.duration_ms == 7000);
    CHECK(window.open);
    CHECK(window.deadline_ms == h.now + 7000);
    CHECK(turn.turn_deadline_ms == 0);
    CHECK(h.timers.Turn().Suspended());
}

TEST_CASE("timers: coordinator does not expire the turn while a window is "
          "open (disjointness)") {
    Harness h;
    TurnState& turn = h.Turn();
    WindowState& window = h.Window();
    REQUIRE(h.timers.Turn().Arm(turn, 1000));
    h.now = 10500;
    h.timers.OpenWindow(h.store, h.match, h.player);  // deadline 17500

    h.now = 11000;  // 1 s past the old turn deadline
    MatchTimerTick tick = h.timers.Tick(h.store, h.match, h.player);
    CHECK(!tick.turn_expired);
    CHECK(!tick.window_timeout);
    CHECK(!tick.window_early_closed);
    CHECK(!static_cast<bool>(tick));
    CHECK(window.open);  // still open
}

TEST_CASE("timers: coordinator window timeout reports the default route and "
          "resumes the turn") {
    Harness h;
    TurnState& turn = h.Turn();
    WindowState& window = h.Window();
    REQUIRE(h.timers.Turn().Arm(turn, 1000));
    h.now = 10500;  // 500 ms left
    window.responders = {h.other};
    window.default_route = "route:pass";
    h.timers.OpenWindow(h.store, h.match, h.player);  // deadline 17500

    h.now = 17500;
    const MatchTimerTick tick = h.timers.Tick(h.store, h.match, h.player);
    CHECK(tick.window_timeout);
    CHECK(!tick.window_early_closed);
    CHECK(!tick.turn_expired);
    CHECK(tick.default_route == "route:pass");
    CHECK(static_cast<bool>(tick));
    CHECK(!window.open);
    CHECK(tick.turn_resumed);
    CHECK(turn.turn_deadline_ms == 17500 + 500);
}

TEST_CASE("timers: coordinator early-closes when all responders replied") {
    Harness h;
    TurnState& turn = h.Turn();
    WindowState& window = h.Window();
    REQUIRE(h.timers.Turn().Arm(turn, 1000));
    window.responders = {h.player, h.other};
    window.default_route = "route:pass";
    h.now = 10300;  // 700 ms left
    h.timers.OpenWindow(h.store, h.match, h.player);

    window.responses.push_back(WindowResponse{h.player, Entity{}, true, 1});
    window.responses.push_back(WindowResponse{h.other, Entity{}, true, 2});

    const MatchTimerTick tick = h.timers.Tick(h.store, h.match, h.player);
    CHECK(tick.window_early_closed);
    CHECK(!tick.window_timeout);
    CHECK(!window.open);
    CHECK(tick.turn_resumed);
    CHECK(turn.turn_deadline_ms == h.now + 700);
}

TEST_CASE("timers: coordinator expires the turn when no window is open") {
    Harness h;
    TurnState& turn = h.Turn();
    h.Window();  // exists but closed
    REQUIRE(h.timers.Turn().Arm(turn, 100));

    h.now += 100;
    const MatchTimerTick tick = h.timers.Tick(h.store, h.match, h.player);
    CHECK(tick.turn_expired);
    CHECK(!tick.window_timeout);
    CHECK(!tick.window_early_closed);
    CHECK(static_cast<bool>(tick));
}

TEST_CASE("timers: coordinator manual close resumes the turn clock") {
    Harness h;
    TurnState& turn = h.Turn();
    WindowState& window = h.Window();
    REQUIRE(h.timers.Turn().Arm(turn, 1000));
    h.now = 10400;  // 600 ms left
    h.timers.OpenWindow(h.store, h.match, h.player);
    CHECK(window.open);
    CHECK(turn.turn_deadline_ms == 0);

    h.now = 10500;
    CHECK(h.timers.CloseWindow(h.store, h.match, h.player));
    CHECK(!window.open);
    CHECK(turn.turn_deadline_ms == 10500 + 600);
}

TEST_CASE("timers: coordinator is inert without a window state") {
    Harness h;
    // no WindowState on the match entity
    const WindowDuration duration =
        h.timers.OpenWindow(h.store, h.match, h.player);
    CHECK(duration.duration_ms == 0);
    const MatchTimerTick tick = h.timers.Tick(h.store, h.match, h.player);
    CHECK(!static_cast<bool>(tick));
}

TEST_CASE("timers: window mode tokens are canonical") {
    CHECK(match::WindowModeToken(WindowMode::kFixed) == "fixed");
    CHECK(match::WindowModeToken(WindowMode::kHalfTurn) == "half_turn");
}
