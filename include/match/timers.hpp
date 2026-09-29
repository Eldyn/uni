#pragma once

#include <match/duration.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/**
 * @file timers.hpp
 * @brief Disjoint turn/window timers.
 *
 *  : the turn clock pauses while a response window is open.
 * `TurnTimer` and `WindowTimer` are therefore disjoint — at most one owns
 * the clock. Opening a window suspends the turn clock while keeping its
 * remaining time; closing resumes it from that remainder. `MatchTimers` is
 * the small coordinator that enforces this lifecycle and reports expiries to
 * the caller.
 *
 * The frozen the store components carry the absolute deadlines: `TurnState::
 * turn_deadline_ms` and `WindowState::deadline_ms` are absolute epoch
 * milliseconds. The timers are the conversion
 * owners: an `ms` `DurationSpec` becomes `now_ms + value`.
 *
 * Neither timer executes routes nor owns a match loop: `Tick`
 * only reports what elapsed. All wall-clock reads go through the injected
 * `NowMs` seam; nothing here calls a clock directly. Tests inject a
 * deterministic counter, so no case waits on real time.
 */

namespace match {

/**
 * @enum WindowMode
 * @brief Window duration policy.
 */
enum class WindowMode {
    kFixed,     /**< always `UNI_WINDOW_MS`. */
    kHalfTurn,  /**< `min(UNI_WINDOW_MS, remaining_turn / 2)`. */
};

/**
 * @brief Canonical token for a window mode (`"fixed"` / `"half_turn"`).
 */
std::string_view WindowModeToken(WindowMode mode);

/**
 * @struct WindowConfig
 * @brief Window timing tunables resolved from the environment.
 */
struct WindowConfig {
    int64_t window_ms = 7000;             /**< `UNI_WINDOW_MS`; 7000 default. */
    WindowMode mode = WindowMode::kFixed; /**< `UNI_WINDOW_MODE`; fixed. */

    /**
     * @brief Read `UNI_WINDOW_MS` / `UNI_WINDOW_MODE` through `Env`.
     *
     * A missing or unparsable `UNI_WINDOW_MS` uses the 7000 default; a
     * negative value logs WARN and also uses 7000. An unrecognized mode logs
     * WARN and falls back to `fixed`. Parsing is fail-safe: a bad value can
     * never arm a zero-length window by accident.
     *
     * @return The resolved configuration.
     */
    static WindowConfig FromEnv();
};

/**
 * @struct WindowDuration
 * @brief The duration `WindowTimer::Open` armed, plus the inputs that chose
 *        it.
 *
 * Returned to the caller so the engine can log or emit `window_open` without
 * re-reading the environment. Only `duration_ms` is stored on the frozen
 * `WindowState` (as an absolute deadline).
 */
struct WindowDuration {
    int64_t duration_ms = 0;         /**< chosen window length. */
    int64_t window_ms = 7000;        /**< configured `UNI_WINDOW_MS`. */
    WindowMode mode = WindowMode::kFixed;
    int64_t remaining_turn_ms = 0;   /**< turn remainder at open. */
    bool turn_remaining_known = false; /**< false = no active turn basis. */
};

/**
 * @class TurnTimer
 * @brief The turn clock, with suspension for open windows.
 *
 * One instance is expected per match; only one turn is live at a time, so
 * the suspension remainder is stored on the timer (not per entity).
 *
 * Deadline contract for the engine — the load-bearing rules:
 *
 * - `ecs::TurnState::turn_deadline_ms` is an ABSOLUTE epoch-ms deadline
 * `0` means "no deadline".
 * - `Arm` writes `turn_deadline_ms = now + duration`. Only the `ms`
 *   `DurationUnit` is meaningful for the turn clock: a non-ms
 *   `DurationSpec` is rejected and leaves the state untouched.
 * - `Suspend` saves `deadline - now` (which may be <= 0), then writes
 *   `turn_deadline_ms = 0`. While suspended `Tick` never expires.
 * - `Resume` writes `turn_deadline_ms = now + saved_remaining`. A saved
 *   remainder <= 0 restores a deadline in the past, so the next `Tick`
 *   expires immediately (a window cannot grant extra turn time).
 * - `Tick` is a pure predicate: it reports expiry but does not clear the
 *   deadline.
 */
class TurnTimer {
public:
    /**
     * @brief Construct with an injected clock.
     * @param clock Wall-clock seam; defaults to the epoch-ms system clock.
     */
    explicit TurnTimer(NowMs clock = DefaultNowMs());

    /** @brief Current time from the injected seam. */
    int64_t Now() const { return clock_(); }

    /**
     * @brief Arm a fresh turn from a duration in milliseconds.
     *
     * @param turn Target turn state; `turn_deadline_ms` is overwritten.
     * @param duration_ms Turn length; negative is rejected.
     * @return true when armed, false when rejected.
     */
    bool Arm(ecs::TurnState& turn, int64_t duration_ms);

    /**
     * @brief Arm a fresh turn from a duration leg.
     *
     * Only `kMs` is representable on the turn clock; any other unit is
     * rejected (returns false) and leaves the state untouched.
     */
    bool Arm(ecs::TurnState& turn, const ecs::DurationSpec& duration);

    /**
     * @brief True when the armed turn deadline has been reached.
     *
     * False while suspended. A `0` deadline (no turn) is never expired.
     * Pure: does not mutate the state.
     */
    bool Tick(const ecs::TurnState& turn) const;

    /**
     * @brief Pause the turn clock and return the remaining milliseconds.
     *
     * No-op (returns the stored remainder) if already suspended. A turn with
     * no armed deadline sets no remainder, so `Resume` leaves `0`.
     */
    int64_t Suspend(ecs::TurnState& turn);

    /**
     * @brief Restart the turn clock from the saved remainder.
     *
     * @return true when a deadline was restored, false when there was no
     *         remainder (the timer is nevertheless marked running again).
     */
    bool Resume(ecs::TurnState& turn);

    /**
     * @brief Drop any suspension and saved remainder (a fresh turn owns the
     *        clock).
     */
    void Reset();

    /** @brief True while the turn clock is suspended. */
    bool Suspended() const { return suspended_; }

    /** @brief True when a remainder is held for a later `Resume`. */
    bool HasRemaining() const { return has_remaining_; }

    /** @brief Saved remainder in ms (0 when none). */
    int64_t RemainingMs() const { return has_remaining_ ? remaining_ms_ : 0; }

private:
    NowMs clock_;
    bool suspended_ = false;
    bool has_remaining_ = false;
    int64_t remaining_ms_ = 0;
};

/**
 * @class WindowTimer
 * @brief The response-window clock.
 *
 * Owns the duration policy and the early-close predicate. It writes only
 * `WindowState::open` and `WindowState::deadline_ms`; responders, routes and
 * the response list belong to the caller, except that `Open` clears
 * stale responses from a previous window. `Tick` is a pure timeout predicate
 * so the caller decides to route `default_route` and close.
 */
class WindowTimer {
public:
    /**
     * @brief Construct with a clock, reading the window config from `Env`.
     * @param clock Wall-clock seam; defaults to the epoch-ms system clock.
     */
    explicit WindowTimer(NowMs clock = DefaultNowMs());

    /**
     * @brief Construct with an explicit config (tests / callers that already
     *        resolved the environment).
     */
    explicit WindowTimer(WindowConfig config, NowMs clock = DefaultNowMs());

    /** @brief The resolved configuration. */
    const WindowConfig& Config() const { return config_; }

    /**
     * @brief Chosen window length for a turn remainder.
     *
     * `fixed` is `UNI_WINDOW_MS`. `half_turn` is
     * `min(UNI_WINDOW_MS, remaining_turn_ms / 2)`; when no active turn basis
     * is supplied (`has_remaining == false`) it falls back to `UNI_WINDOW_MS`
     * rather than arming a zero-length window.
     */
    int64_t ComputeDurationMs(
        int64_t remaining_turn_ms, bool has_remaining,
        std::optional<int64_t> override_ms = std::nullopt) const;

    /**
     * @brief Open `window`: clear stale responses, set the absolute deadline
     *        and mark it open.
     *
     * @param remaining_turn_ms Turn clock remainder used by `half_turn`.
     * @param has_remaining     False when there is no active turn deadline.
     * @param override_ms       Per-window length replacing `UNI_WINDOW_MS`.
     * @return The chosen duration and its inputs.
     */
    WindowDuration Open(
        ecs::WindowState& window, int64_t remaining_turn_ms,
        bool has_remaining,
        std::optional<int64_t> override_ms = std::nullopt) const;

    /**
     * @brief True when an open window's deadline has been reached.
     *
     * Pure: does not mutate `window` (the caller routes the default route
     * and closes). A `0` deadline (none) never expires.
     */
    bool Tick(const ecs::WindowState& window) const;

    /**
     * @brief True when every responder has passed or acted.
     *
     * A responder counts as replied once it has any `WindowResponse`. An
     * empty responder set returns false: a window with nobody to wait for is
     * not "complete", it should not have been opened (this avoids an
     * open-then-instant-close loop).
     */
    bool AllResponded(const ecs::WindowState& window) const;

    /**
     * @brief Close `window`: clear `open` and `deadline_ms`.
     *
     * `responses` and `default_route` are left intact so the caller can route
     * them.
     */
    void Close(ecs::WindowState& window) const;

private:
    WindowConfig config_;
    NowMs clock_;
};

/**
 * @struct MatchTimerTick
 * @brief What one coordinator `Tick` observed.
 */
struct MatchTimerTick {
    bool turn_expired = false;        /**< End the turn. */
    bool window_timeout = false;      /**< Route `default_route`. */
    bool window_early_closed = false; /**< Route collected responses. */
    bool turn_resumed = false;        /**< A deadline was restored. */
    ecs::Entity match{};
    std::string default_route;        /**< set when `window_timeout`. */

    /** @brief True when something needs the engine's attention. */
    explicit operator bool() const {
        return turn_expired || window_timeout || window_early_closed;
    }
};

/**
 * @class MatchTimers
 * @brief Enforces the turn/window disjointness for one match.
 *
 * Owns exactly one `TurnTimer` and one `WindowTimer`; because only one is
 * advanced per `Tick`, the "only one runs at a time" rule cannot be violated.
 * `OpenWindow` suspends the turn clock (preserving its remainder), computes
 * the window duration and opens the window; `CloseWindow` closes it and
 * resumes the turn clock. `Tick` reports expiry/timeout/early-close
 * and never executes a route or advances the match itself.
 */
class MatchTimers {
public:
    /**
     * @brief Construct with a clock; the window config comes from `Env`.
     * @param clock Wall-clock seam; defaults to the epoch-ms system clock.
     */
    explicit MatchTimers(NowMs clock = DefaultNowMs());

    /** @brief Construct with an explicit window config. */
    explicit MatchTimers(WindowConfig config, NowMs clock = DefaultNowMs());

    /** @brief The turn timer (tests / diagnostics). */
    TurnTimer& Turn() { return turn_; }
    const TurnTimer& Turn() const { return turn_; }

    /** @brief The window timer (tests / diagnostics). */
    WindowTimer& Window() { return window_; }
    const WindowTimer& Window() const { return window_; }

    /**
     * @brief Suspend the turn clock and open the match's response window.
     *
     * Reads `WindowState` from `match` and `TurnState` from `current_player`.
     * The caller must have populated `responders`, `default_route` and
     * `filter_digest` first. Returns a zeroed `WindowDuration` when `match`
     * has no `WindowState`.
     */
    WindowDuration OpenWindow(
        ecs::EntityStore& store, ecs::Entity match,
        ecs::Entity current_player,
        std::optional<int64_t> override_ms = std::nullopt);

    /**
     * @brief Close the window and resume the turn clock.
     *
     * @return true when a turn deadline was restored.
     */
    bool CloseWindow(ecs::EntityStore& store, ecs::Entity match,
                     ecs::Entity current_player);

    /**
     * @brief Advance the single running timer and report the outcome.
     *
     * With a window open it checks timeout first (reporting `default_route`
     * and resuming the turn clock), then early close when every responder
     * replied. Otherwise it checks the turn deadline. Never executes a route,
     * never advances the turn, never reads the match loop.
     */
    MatchTimerTick Tick(ecs::EntityStore& store, ecs::Entity match,
                        ecs::Entity current_player);

private:
    NowMs clock_;
    TurnTimer turn_;
    WindowTimer window_;
};

}  // namespace match
