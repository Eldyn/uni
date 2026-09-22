#include <doctest/doctest.h>

#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;

using nlohmann::json;

namespace {

/** INFO: one store/bus/runtime fixture per case. */
struct Harness {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus = EventBus(std::vector<std::string>{"m"});
    ResolutionFrame frame;
    OpRuntime runtime = MakeDefaultRuntime();

    OpResult Invoke(const std::string& op, json raw) {
        OpArgs args(op, std::move(raw));
        OpContext ctx(bus, ledger, frame);
        return runtime.Invoke(op, store, args, ctx);
    }
};

const json* FindEvent(const OpResult& result, const std::string& type) {
    for (const json& event : result.events) {
        if (event.value("type", std::string()) == type) return &event;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("meta_ops: emit_signal shapes a signal descriptor") {
    Harness h;
    const OpResult result =
        h.Invoke("emit_signal",
                 json{{"name", "vfx.blackhole"},
                      {"payload", {{"intensity", 3}, {"color", "black"}}}});

    CHECK(result.status == OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
    const json& event = result.events[0];
    CHECK(event["type"] == "signal");
    CHECK(event["payload"]["name"] == "vfx.blackhole");
    CHECK(event["payload"]["payload"]["intensity"] == 3);
    CHECK(event["payload"]["payload"]["color"] == "black");

    CHECK(result.value["name"] == "vfx.blackhole");
    CHECK(result.value["payload"]["intensity"] == 3);
}

TEST_CASE("meta_ops: emit_signal defaults a missing payload to an object") {
    Harness h;
    const OpResult result =
        h.Invoke("emit_signal", json{{"name", "ping"}});

    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["payload"]["name"] == "ping");
    CHECK(result.events[0]["payload"]["payload"].is_object());
    CHECK(result.events[0]["payload"]["payload"].empty());
}

TEST_CASE("meta_ops: emit_signal fail-safe on a missing or empty name") {
    Harness h;
    const OpResult missing = h.Invoke("emit_signal", json::object());
    CHECK(missing.status == OpStatus::kResolved);
    CHECK(missing.events.empty());

    const OpResult empty =
        h.Invoke("emit_signal", json{{"name", ""}});
    CHECK(empty.events.empty());
}

TEST_CASE("meta_ops: emit_signal fail-safe on a non-object payload") {
    Harness h;
    const OpResult result =
        h.Invoke("emit_signal",
                 json{{"name", "bad"}, {"payload", "not-an-object"}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.events.empty());
}

TEST_CASE("meta_ops: call_original stays the wrap-mutation marker") {
    Harness h;
    const OpResult result = h.Invoke("call_original", json::object());
    CHECK(result.status == OpStatus::kError);
    CHECK(result.error == "not implemented");
    CHECK_FALSE(result.ok());
    CHECK(result.events.empty());
}
