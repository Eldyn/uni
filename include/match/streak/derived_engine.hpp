#pragma once

#include <match/modload/artifacts.hpp>
#include <match/streak/matcher.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

/**
 * @file derived_engine.hpp
 * @brief The derived event engine: windows and blocks over base client
 *        events, one evaluation pass for each base event.
 *
 * A derived event is a core client event that the engine emits right after
 * its base event (spec "Streaks and derived events"). The definitions are in
 * `derived/*.json` (see `derived_definition_keys.hpp`).
 *
 * Rules:
 *
 * - `Evaluate` ignores a view with `derived` set, and a derived event is not
 *   a client event that a matcher can name. A derived event therefore never
 *   feeds a window, a block, or a streak, and the engine cannot loop.
 * - One call is one pass. In the pass the engine first reads the blocks with
 *   the windows as they were BEFORE the base event, then closes windows, then
 *   opens windows. An event that blocks and closes a window still blocks.
 * - Each definition that is relevant to the event costs one evaluation. The
 *   caller passes the charge function of the streak engine, so derived
 *   evaluations count toward `UNI_STREAK_COST_MAX`. A definition past the
 *   limit is skipped for the event.
 * - The actor of a derivation is the actor of the base event. When the base
 *   event has none (a forced draw has none), it is the last seat that acted.
 */

namespace match::streak {

/** @brief One derived event that the engine must emit. */
struct Derivation {
    std::string event_id;  /**< Core event id, for example `blocked`. */
    std::string seat;      /**< Payload `target`. */
    std::string actor;     /**< Envelope actor; may be empty. */
};

/** @brief The state of one window. */
struct WindowState {
    std::string seat;      /**< Empty when no window was ever opened. */
    bool open = false;
};

/** @class DerivedEngine */
class DerivedEngine {
   public:
    /** @brief Charges one evaluation in `turn`; false past the limit. */
    using ChargeFunction =
        std::function<bool(int64_t turn, const std::string& label)>;

    /** @brief Replace the definitions and close every window. */
    void SetDefinitions(const modload::DerivedRegistry& definitions);

    /** @brief True when no derived event is defined. */
    bool Empty() const { return definitions_.empty(); }

    /** @brief Run the one pass for one base client event. */
    std::vector<Derivation> Evaluate(const ClientEventView& event,
                                     const ChargeFunction& charge);

    /** @brief The window that emits `event_id`; closed when unknown. */
    WindowState WindowFor(const std::string& event_id) const;

    /**
     * @brief The windows for the snapshot `context`, keyed by event id.
     *
     * Each value is `{target, open}`; `target` is null while no window was
     * opened. Null when no window is defined.
     */
    nlohmann::json WindowsContext() const;

   private:
    std::map<std::string, modload::DerivedEventDef> definitions_;
    std::map<std::string, WindowState> windows_;
    std::string last_actor_;
    int64_t cost_turn_ = -1;
    std::set<std::string> logged_skips_;
};

}  // namespace match::streak
