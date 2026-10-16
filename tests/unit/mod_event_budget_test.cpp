#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/ecs/event_bus.hpp>
#include <match/ecs/hooks.hpp>
#include <match/ecs/mod_event_budget.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/**
 * @file mod_event_budget_test.cpp
 * @brief The limits and the ledger of the mod event budgets (ticket 13).
 */

using match::ecs::ModEventBudget;
using match::ecs::ModEventLedger;
using match::ecs::ModEventLimits;

namespace {

struct EnvOverride {
    std::string key;
    EnvOverride(std::string name, const std::string& value)
        : key(std::move(name)) {
        Env::SetEnv(key, value);
    }
    ~EnvOverride() { Env::SetEnv(key, std::string()); }
};

/** @brief Collects what the logger writes to the console. */
struct ConsoleCapture {
    std::ostringstream text;
    std::streambuf* saved;
    ConsoleCapture() : saved(std::cout.rdbuf(text.rdbuf())) {}
    ~ConsoleCapture() { std::cout.rdbuf(saved); }
    std::size_t Count(const std::string& needle) const {
        std::size_t count = 0;
        const std::string all = text.str();
        for (std::size_t at = all.find(needle); at != std::string::npos;
             at = all.find(needle, at + 1)) {
            ++count;
        }
        return count;
    }
};

}  // namespace

TEST_CASE("mod event limits: the defaults") {
    const ModEventLimits limits = ModEventLimits::FromEnv();
    CHECK(limits.events_per_turn == 64);
    CHECK(limits.payload_max_bytes == 1024);
    CHECK(limits.cause_chain_max == 16);
    CHECK(limits.violation_turns == 3);
}

TEST_CASE("mod event limits: UNI_MOD_EVENTS_PER_TURN parse cases") {
    const char* name = "UNI_MOD_EVENTS_PER_TURN";
    SUBCASE("override") {
        EnvOverride env(name, "7");
        CHECK(ModEventLimits::FromEnv().events_per_turn == 7);
    }
    SUBCASE("trailing junk gives the default") {
        EnvOverride env(name, "12abc");
        CHECK(ModEventLimits::FromEnv().events_per_turn == 64);
    }
    SUBCASE("text gives the default") {
        EnvOverride env(name, "many");
        CHECK(ModEventLimits::FromEnv().events_per_turn == 64);
    }
    SUBCASE("negative gives the default") {
        EnvOverride env(name, "-3");
        CHECK(ModEventLimits::FromEnv().events_per_turn == 64);
    }
    SUBCASE("above the maximum is clamped") {
        EnvOverride env(name, "20000");
        CHECK(ModEventLimits::FromEnv().events_per_turn == 10000);
    }
    SUBCASE("out of range is clamped") {
        EnvOverride env(name, "99999999999999999999");
        CHECK(ModEventLimits::FromEnv().events_per_turn == 10000);
    }
    SUBCASE("zero means that the mod emits no event") {
        EnvOverride env(name, "0");
        CHECK(ModEventLimits::FromEnv().events_per_turn == 0);
    }
}

TEST_CASE("mod event limits: UNI_MOD_EVENT_PAYLOAD_MAX parse cases") {
    const char* name = "UNI_MOD_EVENT_PAYLOAD_MAX";
    SUBCASE("override") {
        EnvOverride env(name, "300");
        CHECK(ModEventLimits::FromEnv().payload_max_bytes == 300);
    }
    SUBCASE("trailing junk gives the default") {
        EnvOverride env(name, "300x");
        CHECK(ModEventLimits::FromEnv().payload_max_bytes == 1024);
    }
    SUBCASE("negative gives the default") {
        EnvOverride env(name, "-1");
        CHECK(ModEventLimits::FromEnv().payload_max_bytes == 1024);
    }
    SUBCASE("above the maximum is clamped") {
        EnvOverride env(name, "9999999");
        CHECK(ModEventLimits::FromEnv().payload_max_bytes == 65536);
    }
    SUBCASE("zero is kept") {
        EnvOverride env(name, "0");
        CHECK(ModEventLimits::FromEnv().payload_max_bytes == 0);
    }
}

TEST_CASE("mod event limits: UNI_MOD_CAUSE_CHAIN_MAX parse cases") {
    const char* name = "UNI_MOD_CAUSE_CHAIN_MAX";
    SUBCASE("override") {
        EnvOverride env(name, "4");
        CHECK(ModEventLimits::FromEnv().cause_chain_max == 4);
    }
    SUBCASE("trailing junk gives the default") {
        EnvOverride env(name, "4 ");
        CHECK(ModEventLimits::FromEnv().cause_chain_max == 16);
    }
    SUBCASE("negative gives the default") {
        EnvOverride env(name, "-4");
        CHECK(ModEventLimits::FromEnv().cause_chain_max == 16);
    }
    SUBCASE("above the maximum is clamped") {
        EnvOverride env(name, "5000");
        CHECK(ModEventLimits::FromEnv().cause_chain_max == 1000);
    }
    SUBCASE("zero is kept") {
        EnvOverride env(name, "0");
        CHECK(ModEventLimits::FromEnv().cause_chain_max == 0);
    }
}

TEST_CASE("mod event limits: UNI_MOD_VIOLATION_TURNS parse cases") {
    const char* name = "UNI_MOD_VIOLATION_TURNS";
    SUBCASE("override") {
        EnvOverride env(name, "5");
        CHECK(ModEventLimits::FromEnv().violation_turns == 5);
    }
    SUBCASE("trailing junk gives the default") {
        EnvOverride env(name, "2z");
        CHECK(ModEventLimits::FromEnv().violation_turns == 3);
    }
    SUBCASE("negative gives the default") {
        EnvOverride env(name, "-2");
        CHECK(ModEventLimits::FromEnv().violation_turns == 3);
    }
    SUBCASE("above the maximum is clamped") {
        EnvOverride env(name, "5000");
        CHECK(ModEventLimits::FromEnv().violation_turns == 1000);
    }
    SUBCASE("zero is raised to one") {
        EnvOverride env(name, "0");
        CHECK(ModEventLimits::FromEnv().violation_turns == 1);
    }
}

TEST_CASE("mod event ledger: the per turn count resets on a new turn") {
    ModEventLedger ledger;
    ModEventLimits limits;
    limits.events_per_turn = 2;
    ledger.SetLimits(limits);
    CHECK(ledger.CountEvent("mod_a", 1));
    CHECK(ledger.CountEvent("mod_a", 1));
    CHECK_FALSE(ledger.CountEvent("mod_a", 1));
    CHECK(ledger.CountEvent("mod_a", 2));
    CHECK(ledger.CountEvent("mod_a", 2));
    CHECK_FALSE(ledger.CountEvent("mod_a", 2));
}

TEST_CASE("mod event ledger: each mod has its own count") {
    ModEventLedger ledger;
    ModEventLimits limits;
    limits.events_per_turn = 1;
    ledger.SetLimits(limits);
    CHECK(ledger.CountEvent("mod_a", 1));
    CHECK_FALSE(ledger.CountEvent("mod_a", 1));
    CHECK(ledger.CountEvent("mod_b", 1));
}

TEST_CASE("mod event ledger: a drop that is not counted does not use the "
          "count") {
    ModEventLedger ledger;
    ModEventLimits limits;
    limits.events_per_turn = 1;
    ledger.SetLimits(limits);
    CHECK(ledger.CountEvent("mod_a", 1));
    CHECK_FALSE(ledger.CountEvent("mod_a", 1));
    CHECK_FALSE(ledger.CountEvent("mod_a", 1));
    CHECK(ledger.CountEvent("mod_a", 2));
}

TEST_CASE("mod event ledger: violations in distinct turns disarm at the "
          "threshold") {
    ModEventLedger ledger;
    ModEventLimits limits;
    limits.violation_turns = 3;
    ledger.SetLimits(limits);
    CHECK_FALSE(ledger.NoteDrop("m", "m:e", ModEventBudget::kPerTurn, 1));
    CHECK_FALSE(ledger.NoteDrop("m", "m:e", ModEventBudget::kPerTurn, 1));
    CHECK_FALSE(ledger.NoteDrop("m", "m:e", ModEventBudget::kPayloadBytes, 1));
    CHECK(ledger.ViolationTurnCount("m") == 1);
    CHECK_FALSE(ledger.NoteDrop("m", "m:e", ModEventBudget::kPerTurn, 2));
    CHECK(ledger.NoteDrop("m", "m:e", ModEventBudget::kCauseChain, 3));
    CHECK(ledger.ViolationTurnCount("m") == 3);
    CHECK_FALSE(ledger.NoteDrop("m", "m:e", ModEventBudget::kPerTurn, 4));
}

TEST_CASE("mod event ledger: a drop of one mod never counts for another") {
    ModEventLedger ledger;
    ModEventLimits limits;
    limits.violation_turns = 2;
    ledger.SetLimits(limits);
    CHECK_FALSE(ledger.NoteDrop("a", "a:e", ModEventBudget::kPerTurn, 1));
    CHECK_FALSE(ledger.NoteDrop("b", "b:e", ModEventBudget::kPerTurn, 2));
    CHECK(ledger.ViolationTurnCount("a") == 1);
    CHECK(ledger.ViolationTurnCount("b") == 1);
}

TEST_CASE("mod event ledger: the drop counts name the mod, the event and "
          "the budget") {
    ModEventLedger ledger;
    ledger.NoteDrop("m", "m:e", ModEventBudget::kPerTurn, 1);
    ledger.NoteDrop("m", "m:e", ModEventBudget::kPerTurn, 1);
    ledger.NoteDrop("m", "m:f", ModEventBudget::kCauseChain, 1);
    const auto& counts = ledger.DropCounts();
    REQUIRE(counts.size() == 2);
    CHECK(counts.at("m|m:e|per_turn") == 2);
    CHECK(counts.at("m|m:f|cause_chain") == 1);
}

TEST_CASE("event bus: Disarm disarms a mod once and stops its hooks") {
    match::ecs::EventBus bus(std::vector<std::string>{"good", "bad"});
    const match::ecs::HookId play{"play", match::ecs::HookPhase::kAfter};
    int good_runs = 0;
    int bad_runs = 0;
    bus.Subscribe("good", 0, play,
                  [&](match::ecs::HookPayload&) { ++good_runs; });
    bus.Subscribe("bad", 0, play,
                  [&](match::ecs::HookPayload&) { ++bad_runs; });

    CHECK_FALSE(bus.IsDisarmed("bad"));
    CHECK(bus.Disarm("bad"));
    CHECK_FALSE(bus.Disarm("bad"));
    CHECK(bus.IsDisarmed("bad"));
    CHECK_FALSE(bus.IsDisarmed("good"));
    CHECK_FALSE(bus.Disarm(""));

    match::ecs::HookPayload payload;
    payload.hook = play;
    bus.DispatchAfter(play, payload);
    CHECK(good_runs == 1);
    CHECK(bad_runs == 0);
}

TEST_CASE("mod event ledger: a drop is logged at error level with the mod, "
          "the event and the budget") {
    ModEventLedger ledger;
    ConsoleCapture console;
    ledger.NoteDrop("mod_a", "mod_a:apple", ModEventBudget::kCauseChain, 4);
    const std::string logged = console.text.str();
    CHECK(logged.find("ERROR") != std::string::npos);
    CHECK(logged.find("mod_a") != std::string::npos);
    CHECK(logged.find("mod_a:apple") != std::string::npos);
    CHECK(logged.find("cause_chain") != std::string::npos);
}

TEST_CASE("mod event ledger: a flood writes one log line for each mod, "
          "budget and turn") {
    ModEventLedger ledger;
    ConsoleCapture console;
    for (int index = 0; index < 50; ++index) {
        ledger.NoteDrop("m", "m:e", ModEventBudget::kPerTurn, 1);
    }
    CHECK(console.Count("per_turn budget exceeded") == 1);
    ledger.NoteDrop("m", "m:e", ModEventBudget::kPerTurn, 2);
    CHECK(console.Count("per_turn budget exceeded") == 2);
    ledger.NoteDrop("m", "m:e", ModEventBudget::kPayloadBytes, 2);
    CHECK(console.Count("payload_bytes budget exceeded") == 1);
    ledger.NoteDrop("n", "n:e", ModEventBudget::kPerTurn, 2);
    CHECK(console.Count("per_turn budget exceeded") == 3);
    CHECK(ledger.DropCounts().at("m|m:e|per_turn") == 51);
}
