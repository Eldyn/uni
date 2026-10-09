#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/event_bus.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

/**
 * @file mod_event_op_budget_test.cpp
 * @brief The payload budget of the `emit_event` op and the optional mod
 *        event of the `emit_signal` op (ticket 13).
 */

namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;
using nlohmann::json;

namespace {

struct EnvOverride {
    std::string key;
    EnvOverride(std::string name, const std::string& value)
        : key(std::move(name)) {
        Env::SetEnv(key, value);
    }
    ~EnvOverride() { Env::SetEnv(key, std::string()); }
};

EventDef WideEvent(const std::string& mod_id, int field_count) {
    EventDef definition;
    definition.id = "wide";
    definition.namespace_id = mod_id;
    definition.event_id = mod_id + ":wide";
    definition.retention = "log";
    for (int index = 0; index < field_count; ++index) {
        definition.fields.push_back(
            EventFieldDef{"f" + std::to_string(index), "string", "all"});
    }
    return definition;
}

struct OpHarness {
    ecs::EntityStore store;
    ecs::BudgetLedger ledger;
    ecs::EventBus bus = ecs::EventBus(std::vector<std::string>{"test"});
    ops::ResolutionFrame frame;
    ops::OpRuntime runtime = ops::MakeDefaultRuntime();
    MatchRegistries registries;
    std::string mod_id = "test";
    ecs::Entity match;

    OpHarness() {
        match = store.Create();
        store.Add(match, ecs::MatchMeta{});
        const EventDef wide = WideEvent("test", 8);
        registries.events[wide.event_id] = wide;
    }

    void SetTurn(uint32_t turn) {
        store.Get<ecs::MatchMeta>(match)->turn = turn;
    }

    ops::OpResult Run(const std::string& op, const json& args) {
        ops::OpArgs op_args(op, args);
        ops::OpContext context(bus, ledger, frame);
        context.registries = &registries;
        context.mod_id = &mod_id;
        return runtime.Invoke(op, store, op_args, context);
    }
};

json WideArgs(std::size_t length_of_each, int field_count = 8) {
    json payload = json::object();
    for (int index = 0; index < field_count; ++index) {
        payload["f" + std::to_string(index)] =
            std::string(length_of_each, 'x');
    }
    return json{{"id", "test:wide"}, {"payload", payload}};
}

std::size_t CountType(const ops::OpResult& result, const std::string& type) {
    std::size_t count = 0;
    for (const json& event : result.events) {
        if (event.value("type", "") == type) ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("emit_event payload budget: the default limit is 1024 bytes") {
    OpHarness harness;
    CHECK(CountType(harness.Run("emit_event", WideArgs(100)), "mod_event")
          == 1);
    CHECK(CountType(harness.Run("emit_event", WideArgs(200)), "mod_event")
          == 0);
}

TEST_CASE("emit_event payload budget: UNI_MOD_EVENT_PAYLOAD_MAX raises "
          "the limit") {
    EnvOverride limit("UNI_MOD_EVENT_PAYLOAD_MAX", "2000");
    OpHarness harness;
    CHECK(CountType(harness.Run("emit_event", WideArgs(200)), "mod_event")
          == 1);
}

TEST_CASE("emit_event payload budget: UNI_MOD_EVENT_PAYLOAD_MAX lowers "
          "the limit") {
    EnvOverride limit("UNI_MOD_EVENT_PAYLOAD_MAX", "100");
    OpHarness harness;
    CHECK(CountType(harness.Run("emit_event", WideArgs(1)), "mod_event")
          == 1);
    CHECK(CountType(harness.Run("emit_event", WideArgs(20)), "mod_event")
          == 0);
}

TEST_CASE("emit_event payload budget: a malformed value keeps the default") {
    EnvOverride limit("UNI_MOD_EVENT_PAYLOAD_MAX", "60x");
    OpHarness harness;
    CHECK(CountType(harness.Run("emit_event", WideArgs(100)), "mod_event")
          == 1);
}

TEST_CASE("emit_event payload budget: a payload at the limit passes, one "
          "byte over drops") {
    OpHarness harness;
    const std::size_t fixed = json{{"f0", ""}, {"f1", ""}, {"f2", ""},
                                   {"f3", ""}, {"f4", ""}, {"f5", ""},
                                   {"f6", ""}, {"f7", ""}}
                                  .dump()
                                  .size();
    EnvOverride limit("UNI_MOD_EVENT_PAYLOAD_MAX",
                      std::to_string(fixed + 80));
    CHECK(CountType(harness.Run("emit_event", WideArgs(10)), "mod_event")
          == 1);
    CHECK(CountType(harness.Run("emit_event", WideArgs(11)), "mod_event")
          == 0);
}

TEST_CASE("emit_event payload budget: a drop is counted in the ledger") {
    OpHarness harness;
    harness.Run("emit_event", WideArgs(200));
    harness.Run("emit_event", WideArgs(200));
    CHECK(harness.ledger.mod_events.DropCounts().at(
              "test|test:wide|payload_bytes")
          == 2);
}

TEST_CASE("emit_event payload budget: an oversize payload in three turns "
          "disarms the mod") {
    OpHarness harness;
    for (uint32_t turn = 1; turn <= 2; ++turn) {
        harness.SetTurn(turn);
        const ops::OpResult result =
            harness.Run("emit_event", WideArgs(200));
        CHECK(CountType(result, "mod_disarmed") == 0);
        CHECK_FALSE(harness.bus.IsDisarmed("test"));
    }
    harness.SetTurn(3);
    const ops::OpResult result = harness.Run("emit_event", WideArgs(200));
    CHECK(CountType(result, "mod_disarmed") == 1);
    CHECK(harness.bus.IsDisarmed("test"));
    CHECK(harness.ledger.mod_events.ViolationTurnCount("test") == 3);
}

TEST_CASE("emit_event payload budget: an oversize payload in one turn "
          "never disarms") {
    OpHarness harness;
    harness.SetTurn(1);
    for (int index = 0; index < 20; ++index) {
        harness.Run("emit_event", WideArgs(200));
    }
    CHECK_FALSE(harness.bus.IsDisarmed("test"));
}

TEST_CASE("emit_event payload budget: a disarmed mod emits nothing") {
    OpHarness harness;
    harness.bus.Disarm("test");
    CHECK(CountType(harness.Run("emit_event", WideArgs(1)), "mod_event")
          == 0);
    CHECK(harness.ledger.mod_events.DropCounts().empty());
}

TEST_CASE("emit_signal: without an event it emits only the signal") {
    OpHarness harness;
    const ops::OpResult result = harness.Run(
        "emit_signal", json{{"name", "ping"}, {"payload", {{"a", 1}}}});
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["type"] == "signal");
    CHECK(result.events[0]["payload"]
          == json{{"name", "ping"}, {"payload", {{"a", 1}}}});
}

TEST_CASE("emit_signal: with an event it emits the signal and the mod "
          "event") {
    OpHarness harness;
    const ops::OpResult result = harness.Run(
        "emit_signal",
        json{{"name", "ping"},
             {"payload", {{"a", 1}}},
             {"event", "test:wide"},
             {"fields", {{"f0", "hello"}, {"unknown", 1}}}});
    REQUIRE(result.events.size() == 2);
    CHECK(result.events[0]["type"] == "signal");
    CHECK(result.events[0]["payload"]
          == json{{"name", "ping"}, {"payload", {{"a", 1}}}});
    CHECK(result.events[1]["type"] == "mod_event");
    CHECK(result.events[1]["droppable"] == true);
    CHECK(result.events[1]["payload"]["id"] == "test:wide");
    CHECK(result.events[1]["payload"]["payload"]
          == json{{"f0", "hello"}});
}

TEST_CASE("emit_signal: the mod event passes the same checks as "
          "emit_event") {
    OpHarness harness;
    const EventDef foreign = WideEvent("other", 1);
    harness.registries.events[foreign.event_id] = foreign;

    SUBCASE("a foreign namespace drops the mod event, keeps the signal") {
        const ops::OpResult result = harness.Run(
            "emit_signal", json{{"name", "ping"}, {"event", "other:wide"}});
        CHECK(CountType(result, "signal") == 1);
        CHECK(CountType(result, "mod_event") == 0);
    }
    SUBCASE("an undefined event drops the mod event, keeps the signal") {
        const ops::OpResult result = harness.Run(
            "emit_signal", json{{"name", "ping"}, {"event", "test:none"}});
        CHECK(CountType(result, "signal") == 1);
        CHECK(CountType(result, "mod_event") == 0);
    }
    SUBCASE("a wrong typed field drops the mod event, keeps the signal") {
        const ops::OpResult result = harness.Run(
            "emit_signal", json{{"name", "ping"},
                                {"event", "test:wide"},
                                {"fields", {{"f0", 5}}}});
        CHECK(CountType(result, "signal") == 1);
        CHECK(CountType(result, "mod_event") == 0);
    }
    SUBCASE("an oversize payload drops the mod event, keeps the signal") {
        const json wide = WideArgs(200);
        const ops::OpResult result = harness.Run(
            "emit_signal", json{{"name", "ping"},
                                {"event", "test:wide"},
                                {"fields", wide["payload"]}});
        CHECK(CountType(result, "signal") == 1);
        CHECK(CountType(result, "mod_event") == 0);
        CHECK(harness.ledger.mod_events.DropCounts().at(
                  "test|test:wide|payload_bytes")
              == 1);
    }
    SUBCASE("a signal without a name emits nothing") {
        const ops::OpResult result =
            harness.Run("emit_signal", json{{"event", "test:wide"}});
        CHECK(result.events.empty());
    }
}
