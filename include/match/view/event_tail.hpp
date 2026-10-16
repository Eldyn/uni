#pragma once

#include <match/view/event_audience.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>

/**
 * @file event_tail.hpp
 * @brief The store of logical client events that a late joiner replays.
 *
 * The session records each engine event one time, as the raw descriptor. It
 * never stores one copy for each seat. When a seat or a spectator joins, or
 * asks for a snapshot, `ViewBuilder::BuildTail` projects the stored events for
 * that viewer with the same projector as the live stream. The projection
 * reads the card visibility that the op fixed at emit time (`card_public`,
 * `to_zone`, `target`), so a late joiner never sees a card that a live seat
 * could not see.
 *
 * Only ONE name of an alias pair is stored: the new name. The old name is
 * `Retention::kTransient`, so the tail holds each fact one time. The live
 * stream still sends both names. TODO: nothing to remove here when the old
 * names go (ticket 24).
 *
 * Bounds: `kUntilTurnEnd` events leave when the turn counter grows,
 * `kUntilRoundEnd` events when the round counter grows, and each retention
 * class holds at most `Limit()` events (`UNI_EVENT_TAIL_MAX`, default 200).
 * The counters are the ones that the engine stamped on the events
 * (`causal_frame.turn` and `causal_frame.round`), so a jump-in and an extra
 * turn end a turn at their turn advance like any other advance.
 */

namespace match::view {

/** @brief One stored logical client event. */
struct TailEntry {
    nlohmann::json engine_event;  /**< The engine descriptor, unprojected. */
    Retention retention = Retention::kLog;
    uint32_t turn = 0;   /**< Turn counter value at emit time. */
    uint32_t round = 0;  /**< Round counter value at emit time. */
};

/** @class EventTail */
class EventTail {
public:
    /** Largest count of `log` events when the environment names none. */
    static constexpr std::size_t kDefaultLimit = 200;
    /** Largest accepted limit; a larger value is clamped to it. */
    static constexpr std::size_t kMaxLimit = 10000;

    /**
     * @brief Read `UNI_EVENT_TAIL_MAX`.
     *
     * A missing, negative or malformed value (for example `12abc`) gives 200;
     * a malformed value also logs a warning. A value above 10000 is clamped
     * to 10000. `0` is explicit: the server keeps no tail.
     */
    static std::size_t LimitFromEnv();

    /** @brief A tail with the limit of the environment. */
    EventTail() : limit_(LimitFromEnv()) {}

    /** @brief A tail with an explicit limit (tests). */
    explicit EventTail(std::size_t limit) : limit_(limit) {}

    /**
     * @brief Observe one engine event in engine order.
     *
     * Every event moves the turn and round counters, so pruning also happens
     * after a transient event. Then the event is stored when its retention
     * asks for it.
     */
    void Record(const nlohmann::json& engine_event);

    /** @brief The stored events in engine order. */
    const std::deque<TailEntry>& Entries() const { return entries_; }

    /** @brief The largest count of events for each retention class. */
    std::size_t Limit() const { return limit_; }

private:
    void AdvanceCounters(uint32_t turn, uint32_t round);
    void EnforceLimit(Retention retention);

    std::size_t limit_;
    uint32_t current_turn_ = 0;
    uint32_t current_round_ = 0;
    std::deque<TailEntry> entries_;
};

}  // namespace match::view
