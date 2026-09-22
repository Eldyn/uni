#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/rng.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <vector>

using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::MatchMeta;
using match::ecs::RngState;
using match::ops::AdvanceRng;
using match::ops::LastRollTotal;
using match::ops::ResolutionFrame;
using match::ops::SplitMix64;
using match::LogRoll;
using match::Rng;

using nlohmann::json;

namespace {

/** @brief A store with one match entity carrying `rng_state`. */
Entity AddMatch(EntityStore& store, uint64_t seed) {
    Entity entity = store.Create();
    store.Add(entity, MatchMeta{});
    RngState rng;
    rng.seed = seed;
    store.Add(entity, rng);
    return entity;
}

/** @brief Collect `count` draws from a fresh stream seeded with `seed`. */
std::vector<uint64_t> Draws(uint64_t seed, int count) {
    RngState state;
    Rng rng(state);
    rng.Seed(seed);
    std::vector<uint64_t> draws;
    for (int i = 0; i < count; ++i) draws.push_back(rng.NextDraw());
    return draws;
}

}  // namespace

TEST_CASE("rng: explicit seed replays and the counter is monotonic") {
    RngState first_state;
    Rng first(first_state);
    first.Seed(0x5EED1234ULL);
    CHECK(first.SeedValue() == 0x5EED1234ULL);
    CHECK(first.Counter() == 0);

    RngState second_state;
    Rng second(second_state);
    second.Seed(0x5EED1234ULL);

    for (uint64_t expected = 1; expected <= 3; ++expected) {
        const uint64_t a = first.NextDraw();
        const uint64_t b = second.NextDraw();
        CHECK(a == b);
        CHECK(first.Counter() == expected);
        CHECK(second.Counter() == expected);
    }

    CHECK(Draws(42, 4) == Draws(42, 4));
    CHECK(Draws(42, 4) != Draws(43, 4));
}

TEST_CASE("rng: NextDraw uses the shared splitmix64 primitive") {
    RngState state;
    Rng rng(state);
    rng.Seed(9001);
    const uint64_t draw = rng.NextDraw();

    uint64_t expected_state = 9001 + 1;
    CHECK(draw == SplitMix64(expected_state));
    CHECK(rng.Counter() == 1);
}

TEST_CASE("rng: NextRoll mirrors the AdvanceRng stream") {
    RngState state;
    state.seed = 12345;
    Rng rng(state);
    const Rng::Roll roll = rng.NextRoll();

    EntityStore store;
    AddMatch(store, 12345);
    const std::optional<match::ops::RngDraw> draw = AdvanceRng(store);

    REQUIRE(draw.has_value());
    CHECK(draw->counter == roll.counter);
    CHECK(draw->state == roll.state);
}

TEST_CASE("rng: InitializeRngSeed writes explicit seed and match_meta") {
    EntityStore store;
    const Entity match = AddMatch(store, 0);
    const std::optional<uint64_t> applied =
        match::InitializeRngSeed(store, match, 4242);
    REQUIRE(applied.has_value());
    CHECK(*applied == 4242);

    const RngState* rng = store.Get<RngState>(match);
    REQUIRE(rng != nullptr);
    CHECK(rng->seed == 4242);
    CHECK(rng->op_counter == 0);

    const MatchMeta* meta = store.Get<MatchMeta>(match);
    REQUIRE(meta != nullptr);
    CHECK(meta->rng_seed == 4242);

    const std::optional<Rng> wrapper = match::RngFor(store, match);
    REQUIRE(wrapper.has_value());
    CHECK(wrapper->SeedValue() == 4242);
    CHECK(wrapper->Counter() == 0);

    EntityStore empty;
    const Entity bare = empty.Create();
    CHECK(!match::InitializeRngSeed(empty, bare, 1).has_value());
    CHECK(!match::RngFor(empty, bare).has_value());
}

TEST_CASE("rng: SeedFromEnv reads UNI_SEED and ignores malformed values") {
    const std::string previous = Env::Get("UNI_SEED");
    Env::SetEnv("UNI_SEED", "123456789012");
    CHECK(match::Rng::SeedFromEnv()
          == std::optional<uint64_t>(123456789012ULL));
    CHECK(match::Rng::ResolveSeed() == 123456789012ULL);

    Env::SetEnv("UNI_SEED", "not-a-number");
    CHECK(!match::Rng::SeedFromEnv().has_value());

    Env::SetEnv("UNI_SEED", previous);
}

TEST_CASE("rng: LogRoll binds $last_roll for the rolled condition") {
    ResolutionFrame frame;
    const json outcomes = json::array({3, 4, 4});
    LogRoll(frame, 11, outcomes);

    REQUIRE(LastRollTotal(frame).has_value());
    CHECK(*LastRollTotal(frame) == 11);
    const json* roll = match::ops::LastRoll(frame);
    REQUIRE(roll != nullptr);
    CHECK((*roll)["outcomes"] == outcomes);
}
