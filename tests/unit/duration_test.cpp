#include <doctest/doctest.h>

#include <match/duration.hpp>
#include <match/ecs/components.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

using match::DefaultNowMs;
using match::Duration;
using match::DurationElapsed;
using match::DurationProgress;
using match::DurationUnitFromToken;
using match::DurationUnitToken;
using match::LegElapsed;
using match::ParseDuration;
using match::ParseDurationSpec;
using match::ecs::DurationSpec;
using match::ecs::DurationUnit;

using nlohmann::json;

TEST_CASE("duration: parse a single leg object") {
    const std::optional<Duration> parsed =
        ParseDuration(json{{"unit", "ms"}, {"value", 1500}});
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->legs.size() == 1);
    CHECK(parsed->legs[0].unit == DurationUnit::kMs);
    CHECK(parsed->legs[0].value == 1500);
}

TEST_CASE("duration: parse a compound array of legs") {
    const json raw = json::array({
        json{{"unit", "turns"}, {"value", 2}},
        json{{"unit", "ms"}, {"value", 30000}},
    });
    const std::optional<Duration> parsed = ParseDuration(raw);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->legs.size() == 2);
    CHECK(parsed->legs[0].unit == DurationUnit::kTurns);
    CHECK(parsed->legs[1].unit == DurationUnit::kMs);
    CHECK(parsed->legs[1].value == 30000);
}

TEST_CASE("duration: malformed specs are rejected") {
    CHECK(!ParseDuration(json{{"unit", "eons"}, {"value", 1}}).has_value());
    CHECK(!ParseDuration(json{{"unit", "ms"}}).has_value());
    CHECK(!ParseDuration(json{{"value", 1}}).has_value());
    CHECK(!ParseDuration(json{{"unit", "ms"}, {"value", -1}}).has_value());
    CHECK(!ParseDuration(json{{"unit", "ms"}, {"value", 1.5}}).has_value());
    CHECK(!ParseDuration(json::array()).has_value());
    CHECK(!ParseDuration(json(7)).has_value());
    CHECK(!ParseDuration(json::array({json(3)})).has_value());
    CHECK(!ParseDurationSpec(json("ms")).has_value());
}

TEST_CASE("duration: unit tokens round-trip") {
    const DurationUnit units[] = {DurationUnit::kMs, DurationUnit::kTurns,
                                  DurationUnit::kRounds,
                                  DurationUnit::kCardsPlayed};
    for (DurationUnit unit : units) {
        const std::optional<DurationUnit> parsed =
            DurationUnitFromToken(DurationUnitToken(unit));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == unit);
    }
    CHECK(!DurationUnitFromToken("minutes").has_value());
}

TEST_CASE("duration: each leg unit elapses at its threshold") {
    DurationProgress progress;
    progress.elapsed_ms = 100;
    progress.turns = 2;
    progress.rounds = 3;
    progress.cards_played = 4;

    CHECK(LegElapsed(DurationSpec{DurationUnit::kMs, 100}, progress));
    CHECK(!LegElapsed(DurationSpec{DurationUnit::kMs, 101}, progress));
    CHECK(LegElapsed(DurationSpec{DurationUnit::kTurns, 2}, progress));
    CHECK(!LegElapsed(DurationSpec{DurationUnit::kTurns, 3}, progress));
    CHECK(LegElapsed(DurationSpec{DurationUnit::kRounds, 3}, progress));
    CHECK(LegElapsed(DurationSpec{DurationUnit::kCardsPlayed, 4}, progress));
    CHECK(!LegElapsed(DurationSpec{DurationUnit::kCardsPlayed, 5}, progress));
}

TEST_CASE("duration: a non-positive threshold is already elapsed") {
    const DurationProgress progress;
    CHECK(LegElapsed(DurationSpec{DurationUnit::kTurns, 0}, progress));
    CHECK(!LegElapsed(DurationSpec{DurationUnit::kTurns, 1}, progress));
}

TEST_CASE("duration: compound elapses on the first elapsed leg") {
    Duration compound;
    compound.legs.push_back(DurationSpec{DurationUnit::kMs, 1000});
    compound.legs.push_back(DurationSpec{DurationUnit::kTurns, 2});

    DurationProgress clock_first;
    clock_first.elapsed_ms = 1000;
    CHECK(DurationElapsed(compound, clock_first));

    DurationProgress turns_first;
    turns_first.turns = 2;
    CHECK(DurationElapsed(compound, turns_first));

    DurationProgress neither;
    neither.elapsed_ms = 999;
    neither.turns = 1;
    CHECK(!DurationElapsed(compound, neither));
}

TEST_CASE("duration: DefaultNowMs returns epoch milliseconds") {
    const int64_t now = DefaultNowMs()();
    // INFO: > 1e12 ms is after 2001-09-09; a plausible epoch-ms reading.
    CHECK(now > 1000000000000LL);
}
