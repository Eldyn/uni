#include <doctest/doctest.h>

#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;

namespace {

/** Synthetic op used to prove the `OpRuntime::Register` seam. */
OpResult SyntheticEcho(EntityStore&, const OpArgs& args, OpContext&) {
    return OpResult::Resolved(nlohmann::json{{"op", args.OpName()}});
}

/** Second synthetic op reading store state, to prove the store is threaded. */
OpResult SyntheticLiveCount(EntityStore& store, const OpArgs&, OpContext&) {
    return OpResult::Resolved(
        nlohmann::json{{"live", store.LiveCount()}});
}

}  // namespace

TEST_CASE("ops: every OpCatalog name has a registered body") {
    OpRuntime runtime = match::ops::MakeDefaultRuntime();
    const std::vector<match::modload::OpSignature>& catalog =
        match::modload::OpCatalog();

    CHECK(runtime.Size() == catalog.size());
    for (const match::modload::OpSignature& signature : catalog) {
        CHECK(runtime.IsRegistered(signature.name));
    }
}

TEST_CASE("ops: default stubs report not implemented") {
    OpRuntime runtime = match::ops::MakeDefaultRuntime();
    EventBus bus;
    BudgetLedger ledger;
    ResolutionFrame frame;
    EntityStore store;
    OpContext ctx(bus, ledger, frame);

    // INFO: call_original is special (wrap marker) but still a catalog row;
    //       The op layer keeps it a stub, unlike the bodies.
    OpArgs call_original("call_original", nlohmann::json::object());
    OpResult special =
        runtime.Invoke("call_original", store, call_original, ctx);
    CHECK(special.status == OpStatus::kError);
    CHECK(special.error == "not implemented");
    CHECK_FALSE(special.ok());

    OpResult unknown =
        runtime.Invoke("no_such_op", store, call_original, ctx);
    CHECK(unknown.status == OpStatus::kError);
    CHECK(unknown.error.find("unknown op") != std::string::npos);
}

TEST_CASE("ops: Register seam installs a synthetic op") {
    OpRuntime runtime;
    CHECK_FALSE(runtime.Register("", &SyntheticEcho));
    CHECK_FALSE(runtime.Register("null_body", nullptr));
    CHECK(runtime.Register("synthetic_echo", &SyntheticEcho));

    EventBus bus;
    BudgetLedger ledger;
    ResolutionFrame frame;
    EntityStore store;
    OpContext ctx(bus, ledger, frame);
    store.Create();

    OpArgs args("synthetic_echo");
    OpResult result = runtime.Invoke("synthetic_echo", store, args, ctx);
    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value["op"] == "synthetic_echo");

    CHECK(runtime.Register("synthetic_live", &SyntheticLiveCount));
    OpResult live = runtime.Invoke("synthetic_live", store, args, ctx);
    CHECK(live.status == OpStatus::kResolved);
    CHECK(live.value["live"] == 1);
}

TEST_CASE("ops: arg extraction fails safe on missing or wrong type") {
    // INFO: n is declared kInt but handed a string; target is a selector.
    OpArgs args("draw_cards",
                nlohmann::json{{"n", "not-a-number"}, {"target", "@self"}});

    int64_t number = 0;
    CHECK_FALSE(args.GetInt("n", number));       // wrong JSON type.
    CHECK_FALSE(args.GetInt("missing", number));  // absent.
    CHECK_FALSE(args.GetInt("target", number));   // declared kSelector.

    std::string text;
    CHECK(args.GetString("target", text));
    CHECK(text == "@self");
    CHECK_FALSE(args.GetString("n", text));  // declared kInt, not string.

    CHECK(args.StringOr("missing", "fallback") == "fallback");
    CHECK(args.StringOr("target", "fallback") == "@self");
    CHECK(args.IntOr("n", -1) == -1);

    CHECK(args.Declares("n"));
    CHECK(args.Spec("n") != nullptr);
    CHECK_FALSE(args.Declares("bogus"));
    CHECK(args.Spec("bogus") == nullptr);
    CHECK(args.Has("n"));
    CHECK_FALSE(args.Has("missing"));
    CHECK(args.Find("missing") == nullptr);

    // Object/array extraction is declaration-checked too.
    OpArgs prompt("prompt",
                  nlohmann::json{{"kind", "choose_color"},
                                 {"payload", {{"a", 1}}}});
    const nlohmann::json* payload = prompt.GetObject("payload");
    REQUIRE(payload != nullptr);
    CHECK((*payload)["a"] == 1);
    CHECK(prompt.GetObject("kind") == nullptr);  // kString, not object-like.

    OpArgs grant("grant_visibility",
                 nlohmann::json{{"viewer", "@self"},
                                {"target", "@others"},
                                {"aspects", {"count", "color"}}});
    const nlohmann::json* aspects = grant.GetArray("aspects");
    REQUIRE(aspects != nullptr);
    CHECK(aspects->size() == 2);
    CHECK(grant.GetArray("viewer") == nullptr);  // kSelector, not array.

    // Fail safe on a non-object raw args body.
    OpArgs malformed("draw_cards", nlohmann::json::array());
    CHECK_FALSE(malformed.Has("n"));
    CHECK(malformed.Find("n") == nullptr);
    CHECK_FALSE(malformed.GetInt("n", number));
}

TEST_CASE("ops: resolved selector entities are readable and fail safe") {
    const Entity card{7, 0};
    OpArgs args("move_card", nlohmann::json{{"card", "@card"}});

    CHECK_FALSE(args.HasEntities("card"));
    CHECK_FALSE(args.FirstEntity("card").has_value());
    CHECK(args.EntitiesOrEmpty("card").empty());

    args.BindSelector("card", {card});
    CHECK(args.HasEntities("card"));
    REQUIRE(args.FindEntities("card") != nullptr);
    CHECK(args.FindEntities("card")->size() == 1);
    REQUIRE(args.FirstEntity("card").has_value());
    CHECK(args.FirstEntity("card").value() == card);
    CHECK(args.EntitiesOrEmpty("card").size() == 1);
}

TEST_CASE("ops: OpResult factories encode status and value") {
    OpResult resolved = OpResult::Resolved(nlohmann::json{{"k", 1}});
    CHECK(resolved.status == OpStatus::kResolved);
    CHECK(resolved.ok());
    CHECK(resolved.value["k"] == 1);

    OpResult needs = OpResult::NeedsInput(nlohmann::json{{"kind", "choose"}});
    CHECK(needs.status == OpStatus::kNeedsInput);
    CHECK(needs.ok());  // a pause is not an error.

    OpResult error = OpResult::Error("boom");
    CHECK(error.status == OpStatus::kError);
    CHECK_FALSE(error.ok());
    CHECK(error.error == "boom");
}
