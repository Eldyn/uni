#include <match/rng.hpp>

#include <common/env.hpp>
#include <match/ops/op_helpers.hpp>

#include <random>
#include <string>
#include <utility>

/**
 * @file rng.cpp
 * @brief RNG bodies: seeding, the next-roll entry point, roll logging.
 */

namespace match {

Rng::Rng(ecs::RngState& state) : state_(&state) {}

uint64_t Rng::RandomSeed() {
    std::random_device device;
    const uint64_t high = static_cast<uint64_t>(device());
    const uint64_t low = static_cast<uint64_t>(device());
    return (high << 32) ^ low;
}

std::optional<uint64_t> Rng::SeedFromEnv() {
    const std::string raw = Env::Get("UNI_SEED");
    if (raw.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        const unsigned long long value = std::stoull(raw, &consumed, 10);
        if (consumed != raw.size()) return std::nullopt;
        return static_cast<uint64_t>(value);
    } catch (...) {
        // INFO: a malformed UNI_SEED is ignored; assembly falls back to random.
        return std::nullopt;
    }
}

uint64_t Rng::ResolveSeed() {
    const std::optional<uint64_t> env = SeedFromEnv();
    if (env.has_value()) return *env;
    return RandomSeed();
}

void Rng::Seed(uint64_t seed) { state_->seed = seed; }

void Rng::SeedRandom() { Seed(ResolveSeed()); }

uint64_t Rng::SeedValue() const { return state_->seed; }

uint64_t Rng::Counter() const { return state_->op_counter; }

Rng::Roll Rng::NextRoll() { return AdvanceRngState(*state_); }

uint64_t Rng::NextDraw() {
    const Roll roll = NextRoll();
    uint64_t state = roll.state;
    return ops::SplitMix64(state);
}

std::optional<Rng> RngFor(ecs::EntityStore& store, ecs::Entity match) {
    ecs::RngState* state = store.Get<ecs::RngState>(match);
    if (state == nullptr) return std::nullopt;
    return Rng(*state);
}

std::optional<uint64_t> InitializeRngSeed(
    ecs::EntityStore& store, ecs::Entity match,
    std::optional<uint64_t> explicit_seed) {
    ecs::RngState* state = store.Get<ecs::RngState>(match);
    if (state == nullptr) return std::nullopt;
    const uint64_t seed =
        explicit_seed.has_value() ? *explicit_seed : Rng::ResolveSeed();
    state->seed = seed;
    state->op_counter = 0;
    ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(match);
    if (meta != nullptr) meta->rng_seed = seed;
    return seed;
}

void LogRoll(ops::ResolutionFrame& frame, int64_t total,
             nlohmann::json outcomes) {
    ops::BindLastRoll(frame, total, std::move(outcomes));
}

}  // namespace match
