#pragma once

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

namespace match::ops {
struct ResolutionFrame;
}  // namespace match::ops

namespace match {

/**
 * @file rng.hpp
 * @brief RNG subsystem.
 *
 * One splitmix64 stream per match: `Rng` wraps the `RngState{seed,
 * op_counter}` and draws through the shared `ops::SplitMix64` primitive from
 * The op layer's `op_helpers.hpp` — there is no second RNG implementation.
 *
 * Seed initialisation: `ResolveSeed` prefers the `UNI_SEED`
 * environment override, otherwise a random 64-bit source; `InitializeRngSeed`
 * writes it to a match's `rng_state` (and mirrors it into `match_meta`).
 *
 * Roll logging: `LogRoll` is the integration point for the `$last_roll` frame
 * key the op layer's `rolled` condition already reads; it delegates to the op
 * layer `ops::BindLastRoll` seam.
 *
 * The op layer seam: the `roll` op path already reads/writes this same
 * `RngState` through `ops::AdvanceRng` and draws through `ops::SplitMix64`, so
 * it is on the same stream by construction. `Rng::NextRoll` and
 * `ops::AdvanceRng` both delegate to `AdvanceRngState` so the counter
 * arithmetic exists once.
 */

/**
 * @class Rng
 * @brief The match's splitmix64 stream over a live `RngState`.
 *
 * Holds a non-owning pointer to the component; the store-owned `RngState` is
 * the single source of truth, so copies share the same stream.
 */
class Rng {
public:
    /**
     * @brief Wrap an existing match `rng_state`.
     * @param state The component to read/write; must outlive the wrapper.
     */
    explicit Rng(ecs::RngState& state);

    /**
     * @brief A random 64-bit seed from `std::random_device`.
     */
    static uint64_t RandomSeed();

    /**
     * @brief `UNI_SEED` override when set and parseable as an unsigned 64-bit.
     */
    static std::optional<uint64_t> SeedFromEnv();

    /**
     * @brief Resolve the assembly seed: env override first, else random.
     *
     * random 64-bit at assembly, or an explicit seed via settings.
     */
    static uint64_t ResolveSeed();

    /** @brief Set the stream seed (does not touch `op_counter`). */
    void Seed(uint64_t seed);

    /** @brief Seed from `ResolveSeed()`. */
    void SeedRandom();

    /** @brief The current seed. */
    uint64_t SeedValue() const;

    /** @brief The current monotonic op counter. */
    uint64_t Counter() const;

    /**
     * @struct Roll
     * @brief One counter advance plus the stream state it seeded.
     */
    struct Roll {
        uint64_t counter = 0;  /**< post-increment op counter. */
        uint64_t state = 0;    /**< splitmix64 state (`seed + counter`). */
    };

    /**
     * @brief Advance the counter and return the seeded stream state.
     *
     * Mirrors `ops::AdvanceRng` exactly: `op_counter += 1`, state =
     * `seed + op_counter`. The counter is monotonic and never wraps in
     * practice; identical `(seed, counter)` pairs replay identically.
     */
    Roll NextRoll();

    /**
     * @brief Convenience: one `NextRoll` then a single splitmix64 draw.
     */
    uint64_t NextDraw();

private:
    ecs::RngState* state_;
};

/**
 * @brief The single counter-math step.
 *
 * Increments `op_counter` and returns the post-increment counter plus the
 * splitmix64 stream state `seed + counter`. `Rng::NextRoll` and the op layer's
 * `ops::AdvanceRng` both delegate here, so the `(seed, counter)` stream is
 * defined in exactly one place.
 */
inline Rng::Roll AdvanceRngState(ecs::RngState& state) {
    state.op_counter += 1;
    Rng::Roll roll;
    roll.counter = state.op_counter;
    roll.state = state.seed + state.op_counter;
    return roll;
}

/**
 * @brief An `Rng` over `match`'s `rng_state`, or `nullopt` when absent.
 */
std::optional<Rng> RngFor(ecs::EntityStore& store, ecs::Entity match);

/**
 * @brief Seed `match`'s stream: explicit seed > `UNI_SEED` > random.
 *
 * Writes `rng_state.seed` and, when the entity carries `match_meta`, mirrors
 * the value into `match_meta.rng_seed` so snapshots/logs agree. Leaves
 * `op_counter` at 0. Returns the seed applied, or `nullopt` when the entity has
 * no `rng_state`.
 */
std::optional<uint64_t> InitializeRngSeed(
    ecs::EntityStore& store, ecs::Entity match,
    std::optional<uint64_t> explicit_seed = std::nullopt);

/**
 * @brief Bind the most recent roll into `frame`'s `$last_roll` slot.
 *
 * The roll-logging integration point: delegates to the op layer
 * `ops::BindLastRoll` seam so the op layer's `rolled` condition reads the same
 * shape.
 */
void LogRoll(ops::ResolutionFrame& frame, int64_t total,
             nlohmann::json outcomes = nlohmann::json::array());

}  // namespace match
