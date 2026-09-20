#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::PlayerInfo;
using match::ops::InputRequest;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;

using nlohmann::json;

namespace {

Entity AddPlayer(EntityStore& store, const std::string& name, uint32_t seat) {
    Entity entity = store.Create();
    PlayerInfo info;
    info.username = name;
    info.seat = seat;
    store.Add(entity, info);
    return entity;
}

/** INFO: one store/bus/runtime fixture per case; `last_input` captures the
 *        op-writable slot the body is required to fill. */
struct Harness {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus = EventBus(std::vector<std::string>{"m"});
    ResolutionFrame frame;
    OpRuntime runtime = MakeDefaultRuntime();
    std::optional<InputRequest> last_input;

    OpResult Invoke(
        const std::string& op, json raw,
        std::initializer_list<std::pair<std::string, std::vector<Entity>>>
            bindings = {}) {
        OpArgs args(op, std::move(raw));
        for (const auto& binding : bindings) {
            args.BindSelector(binding.first, binding.second);
        }
        OpContext ctx(bus, ledger, frame);
        OpResult result = runtime.Invoke(op, store, args, ctx);
        last_input = ctx.input_request;
        return result;
    }
};

}  // namespace

TEST_CASE("prompt_ops: opens the envelope and fills input_request") {
    Harness h;
    Entity player = AddPlayer(h.store, "p0", 0);

    const OpResult result = h.Invoke(
        "prompt",
        json{{"kind", "choose_color"},
             {"payload", {{"options", {"red", "blue"}}}},
             {"timeout", 15000},
             {"default", "red"}},
        {{"target", {player}}});

    CHECK(result.status == OpStatus::kNeedsInput);
    REQUIRE(result.value.is_object());
    CHECK(result.value["kind"] == "choose_color");
    CHECK(result.value["payload"]["options"] == json::array({"red", "blue"}));
    CHECK(result.value["timeout_ms"] == 15000);
    CHECK(result.value["default"] == "red");
    // INFO: response_schema is the engine and the session layer's to attach.
    CHECK_FALSE(result.value.contains("response_schema"));
    CHECK(result.events.empty());

    REQUIRE(h.last_input.has_value());
    CHECK(h.last_input->kind == "choose_color");
    REQUIRE(h.last_input->target.has_value());
    CHECK(*h.last_input->target == player);
    CHECK(h.last_input->payload ==
          json{{"options", {"red", "blue"}}});
}

TEST_CASE("prompt_ops: defaults timeout 0, payload object, default null") {
    Harness h;
    Entity player = AddPlayer(h.store, "p0", 0);

    const OpResult result =
        h.Invoke("prompt", json{{"kind", "choose_yes_no"}},
                 {{"target", {player}}});

    CHECK(result.status == OpStatus::kNeedsInput);
    CHECK(result.value["kind"] == "choose_yes_no");
    CHECK(result.value["payload"].is_object());
    CHECK(result.value["payload"].empty());
    CHECK(result.value["timeout_ms"] == 0);
    CHECK(result.value["default"].is_null());

    REQUIRE(h.last_input.has_value());
    CHECK(h.last_input->payload.empty());
}

TEST_CASE("prompt_ops: an arbitrary default passes through untouched") {
    Harness h;
    Entity player = AddPlayer(h.store, "p0", 0);

    const OpResult result = h.Invoke(
        "prompt",
        json{{"kind", "choose_value"}, {"default", {{"min", 1}, {"max", 6}}}},
        {{"target", {player}}});

    CHECK(result.status == OpStatus::kNeedsInput);
    CHECK(result.value["default"] == json{{"min", 1}, {"max", 6}});
}

TEST_CASE("prompt_ops: unbound target is a fail-safe no-op") {
    Harness h;
    AddPlayer(h.store, "p0", 0);

    const OpResult result =
        h.Invoke("prompt", json{{"kind", "choose_color"}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value.is_null());
    CHECK(result.events.empty());
    CHECK_FALSE(h.last_input.has_value());
}

TEST_CASE("prompt_ops: dead target is a fail-safe no-op") {
    Harness h;
    Entity dead = AddPlayer(h.store, "p0", 0);
    REQUIRE(h.store.Destroy(dead));

    const OpResult result =
        h.Invoke("prompt", json{{"kind", "choose_color"}},
                 {{"target", {dead}}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value.is_null());
    CHECK_FALSE(h.last_input.has_value());
}

TEST_CASE("prompt_ops: missing or empty kind is a no-op") {
    Harness h;
    Entity player = AddPlayer(h.store, "p0", 0);

    const OpResult missing =
        h.Invoke("prompt", json::object(), {{"target", {player}}});
    CHECK(missing.status == OpStatus::kResolved);
    CHECK(missing.value.is_null());

    const OpResult empty = h.Invoke("prompt", json{{"kind", ""}},
                                    {{"target", {player}}});
    CHECK(empty.status == OpStatus::kResolved);
    CHECK(empty.value.is_null());
    CHECK_FALSE(h.last_input.has_value());
}

TEST_CASE("prompt_ops: out-of-bounds or ill-typed timeout is a no-op") {
    Harness h;
    Entity player = AddPlayer(h.store, "p0", 0);

    for (const json& bad_timeout : json::array({-1, 600001, "soon"})) {
        const OpResult result =
            h.Invoke("prompt", json{{"kind", "choose_color"},
                                    {"timeout", bad_timeout}},
                     {{"target", {player}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
        CHECK_FALSE(h.last_input.has_value());
    }
}

TEST_CASE("prompt_ops: non-object payload is a no-op") {
    Harness h;
    Entity player = AddPlayer(h.store, "p0", 0);

    const OpResult result = h.Invoke(
        "prompt", json{{"kind", "choose_color"}, {"payload", json::array()}},
        {{"target", {player}}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value.is_null());
    CHECK_FALSE(h.last_input.has_value());
}
