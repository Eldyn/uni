#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using match::ecs::ActiveTypeReq;
using match::ecs::BudgetLedger;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::MatchMeta;
using match::ecs::PlayRestriction;
using match::ecs::RestrictionEntry;
using match::ecs::RestrictionPhase;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;

using nlohmann::json;

namespace {

Entity AddMatch(EntityStore& store) {
    Entity match = store.Create();
    store.Add(match, MatchMeta{});
    return match;
}

/** INFO: one store/bus/runtime fixture per case. */
struct Harness {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus = EventBus(std::vector<std::string>{"m"});
    ResolutionFrame frame;
    OpRuntime runtime = MakeDefaultRuntime();

    OpResult Invoke(
        const std::string& op, json raw,
        std::initializer_list<std::pair<std::string, std::vector<Entity>>>
            bindings = {}) {
        OpArgs args(op, std::move(raw));
        for (const auto& binding : bindings) {
            args.BindSelector(binding.first, binding.second);
        }
        OpContext ctx(bus, ledger, frame);
        return runtime.Invoke(op, store, args, ctx);
    }
};

const PlayRestriction* GetPipeline(EntityStore& store, Entity match) {
    return store.Get<PlayRestriction>(match);
}

json EntryDef(const std::string& id, const std::string& phase) {
    return json{{"id", id},
                {"phase", phase},
                {"condition", {{"always", true}}}};
}

}  // namespace

TEST_CASE("restriction_ops: add_restriction appends allow and deny entries") {
    Harness h;
    Entity match = AddMatch(h.store);

    const OpResult allow = h.Invoke(
        "add_restriction",
        json{{"entry_def", EntryDef("mod:jump_in", "allow")}});
    const OpResult deny =
        h.Invoke("add_restriction",
                 json{{"entry_def", EntryDef("vanilla:turn_order", "deny")}});

    CHECK(allow.status == OpStatus::kResolved);
    CHECK(deny.status == OpStatus::kResolved);
    CHECK(allow.events.empty());
    CHECK(allow.value["id"] == "mod:jump_in");
    CHECK(allow.value["phase"] == "allow");

    const PlayRestriction* pipeline = GetPipeline(h.store, match);
    REQUIRE(pipeline != nullptr);
    REQUIRE(pipeline->entries.size() == 2);
    CHECK(pipeline->entries[0].id == "mod:jump_in");
    CHECK(pipeline->entries[0].phase == RestrictionPhase::kAllow);
    CHECK(pipeline->entries[0].condition == json{{"always", true}});
    CHECK(pipeline->entries[1].id == "vanilla:turn_order");
    CHECK(pipeline->entries[1].phase == RestrictionPhase::kDeny);
    (void)match;
}

TEST_CASE("restriction_ops: add_restriction rejects a duplicate id") {
    Harness h;
    Entity match = AddMatch(h.store);

    h.Invoke("add_restriction",
             json{{"entry_def", EntryDef("vanilla:turn_order", "deny")}});
    const OpResult dup = h.Invoke(
        "add_restriction",
        json{{"entry_def", EntryDef("vanilla:turn_order", "allow")}});

    CHECK(dup.status == OpStatus::kError);
    CHECK(dup.error.find("duplicate") != std::string::npos);
    const PlayRestriction* pipeline = GetPipeline(h.store, match);
    REQUIRE(pipeline != nullptr);
    REQUIRE(pipeline->entries.size() == 1);
    CHECK(pipeline->entries[0].phase == RestrictionPhase::kDeny);
}

TEST_CASE("restriction_ops: malformed or missing entry_def is a no-op") {
    Harness h;
    Entity match = AddMatch(h.store);

    const std::vector<json> malformed = {
        json(5),
        json{{"phase", "deny"}},
        json{{"id", "no_colon", "phase", "deny"}},
        json{{"id", "vanilla:x", "phase", "maybe"}},
    };
    for (const json& bad : malformed) {
        const OpResult result =
            h.Invoke("add_restriction", json{{"entry_def", bad}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
        CHECK(result.events.empty());
    }
    const OpResult missing = h.Invoke("add_restriction", json::object());
    CHECK(missing.status == OpStatus::kResolved);
    CHECK(GetPipeline(h.store, match) == nullptr);
}

TEST_CASE("restriction_ops: remove_restriction drops every matching id") {
    Harness h;
    Entity match = AddMatch(h.store);

    // INFO: seed a duplicate directly, since add_restriction refuses one.
    PlayRestriction seeded;
    seeded.entries.push_back(
        RestrictionEntry{"mod:a", RestrictionPhase::kDeny, json{{"a", 1}}});
    seeded.entries.push_back(
        RestrictionEntry{"mod:a", RestrictionPhase::kAllow, json{{"a", 2}}});
    seeded.entries.push_back(
        RestrictionEntry{"mod:b", RestrictionPhase::kDeny, json{{"b", 1}}});
    h.store.Add(match, seeded);

    const OpResult result = h.Invoke(
        "remove_restriction", json{{"entry_id", "mod:a"}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.events.empty());
    CHECK(result.value["id"] == "mod:a");
    CHECK(result.value["removed"] == 2);
    const PlayRestriction* pipeline = GetPipeline(h.store, match);
    REQUIRE(pipeline != nullptr);
    REQUIRE(pipeline->entries.size() == 1);
    CHECK(pipeline->entries[0].id == "mod:b");
}

TEST_CASE("restriction_ops: remove_restriction fail-safe paths") {
    SUBCASE("unknown id") {
        Harness h;
        Entity match = AddMatch(h.store);
        h.Invoke("add_restriction",
                 json{{"entry_def", EntryDef("mod:a", "deny")}});
        const OpResult result =
            h.Invoke("remove_restriction", json{{"entry_id", "mod:nope"}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
        CHECK(result.events.empty());
        REQUIRE(GetPipeline(h.store, match) != nullptr);
        CHECK(GetPipeline(h.store, match)->entries.size() == 1);
    }
    SUBCASE("missing or empty entry_id") {
        Harness h;
        AddMatch(h.store);
        const std::vector<json> raw_args = {
            json::object(), json{{"entry_id", ""}}};
        for (const json& raw : raw_args) {
            const OpResult result = h.Invoke("remove_restriction", raw);
            CHECK(result.status == OpStatus::kResolved);
            CHECK(result.value.is_null());
        }
    }
    SUBCASE("no pipeline component") {
        Harness h;
        Entity bare = AddMatch(h.store);
        const OpResult result =
            h.Invoke("remove_restriction", json{{"entry_id", "mod:a"}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(GetPipeline(h.store, bare) == nullptr);
    }
}

TEST_CASE("restriction_ops: set_active_type writes the match requirement") {
    Harness h;
    Entity match = AddMatch(h.store);

    const OpResult result =
        h.Invoke("set_active_type", json{{"type", "space:shielded"}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value["type"] == "space:shielded");
    const ActiveTypeReq* req = h.store.Get<ActiveTypeReq>(match);
    REQUIRE(req != nullptr);
    REQUIRE(req->type.has_value());
    CHECK(*req->type == "space:shielded");
}

TEST_CASE("restriction_ops: set_active_type binds a prompt result") {
    Harness h;
    Entity match = AddMatch(h.store);

    SUBCASE("string prompt value") {
        h.frame.BindPromptValue("n1", "red");
        const OpResult result =
            h.Invoke("set_active_type", json{{"from_prompt", "n1"}});
        CHECK(result.status == OpStatus::kResolved);
        const ActiveTypeReq* req = h.store.Get<ActiveTypeReq>(match);
        REQUIRE(req != nullptr);
        REQUIRE(req->type.has_value());
        CHECK(*req->type == "red");
    }
    SUBCASE("object prompt value with a type field") {
        h.frame.BindPromptValue("n2", json{{"type", "blue"}, {"n", 2}});
        const OpResult result =
            h.Invoke("set_active_type", json{{"from_prompt", "n2"}});
        CHECK(result.status == OpStatus::kResolved);
        const ActiveTypeReq* req = h.store.Get<ActiveTypeReq>(match);
        REQUIRE(req != nullptr);
        REQUIRE(req->type.has_value());
        CHECK(*req->type == "blue");
    }
    SUBCASE("literal type wins over from_prompt") {
        h.frame.BindPromptValue("n1", "red");
        const OpResult result = h.Invoke(
            "set_active_type", json{{"type", "green"}, {"from_prompt", "n1"}});
        CHECK(result.status == OpStatus::kResolved);
        const ActiveTypeReq* req = h.store.Get<ActiveTypeReq>(match);
        REQUIRE(req != nullptr);
        REQUIRE(req->type.has_value());
        CHECK(*req->type == "green");
    }
}

TEST_CASE("restriction_ops: set_active_type fail-safe paths") {
    Harness h;
    Entity match = AddMatch(h.store);

    SUBCASE("missing prompt value") {
        const OpResult result =
            h.Invoke("set_active_type", json{{"from_prompt", "n9"}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
        CHECK(h.store.Get<ActiveTypeReq>(match) == nullptr);
    }
    SUBCASE("malformed prompt value") {
        h.frame.BindPromptValue("n1", 5);
        const OpResult result =
            h.Invoke("set_active_type", json{{"from_prompt", "n1"}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(h.store.Get<ActiveTypeReq>(match) == nullptr);
    }
    SUBCASE("missing both args and empty literal") {
        const OpResult none = h.Invoke("set_active_type", json::object());
        CHECK(none.status == OpStatus::kResolved);
        const OpResult empty =
            h.Invoke("set_active_type", json{{"type", ""}});
        CHECK(empty.status == OpStatus::kResolved);
        CHECK(h.store.Get<ActiveTypeReq>(match) == nullptr);
    }
    SUBCASE("no match entity") {
        Harness bare;
        const OpResult result =
            bare.Invoke("set_active_type", json{{"type", "red"}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
    }
}

TEST_CASE("restriction_ops: clear_active_type resets the requirement") {
    Harness h;
    Entity match = AddMatch(h.store);

    h.Invoke("set_active_type", json{{"type", "red"}});
    const OpResult result = h.Invoke("clear_active_type", json::object());

    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value["type"].is_null());
    const ActiveTypeReq* req = h.store.Get<ActiveTypeReq>(match);
    REQUIRE(req != nullptr);
    CHECK_FALSE(req->type.has_value());
}

TEST_CASE("restriction_ops: clear_active_type fail-safe paths") {
    Harness h;
    Entity match = AddMatch(h.store);

    SUBCASE("nothing set") {
        const OpResult result = h.Invoke("clear_active_type", json::object());
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
        CHECK(h.store.Get<ActiveTypeReq>(match) == nullptr);
    }
    SUBCASE("no match entity") {
        Harness bare;
        const OpResult result =
            bare.Invoke("clear_active_type", json::object());
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
    }
}
