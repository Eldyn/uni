#include <doctest/doctest.h>

#include <match/content_wiring.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/resolver.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::engine::MatchRegistries;
using match::modload::CardDef;
using match::ops::CardHasTag;
using match::ops::OpContext;
using match::ops::ResolutionFrame;
using match::resolver::ConditionRegistry;

using nlohmann::json;

namespace {

/** Minimal store + registry + frame, mirroring condition_eval_test. */
struct Harness {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus;
    ResolutionFrame frame;
    ConditionRegistry conditions;
    OpContext ctx;

    Harness() : ctx(bus, ledger, frame) {}

    bool Eval(const json& condition) {
        return conditions.Evaluate(store, condition, ctx);
    }
};

json Cond(const std::string& keyword, json args) {
    return json{{keyword, std::move(args)}};
}

}  // namespace

TEST_CASE("content_wiring: installs every default condition") {
    Harness harness;
    match::wiring::InstallDefaultConditions(harness.conditions);

    for (const auto& signature : match::modload::ConditionCatalog()) {
        CHECK(harness.conditions.Has(signature.keyword));
    }
    CHECK(harness.conditions.Has("turns_elapsed"));
    CHECK(harness.conditions.Has("status_active"));
    CHECK_FALSE(harness.conditions.Has("no_such_condition"));
}

TEST_CASE("content_wiring: binds turns_elapsed for the caller") {
    Harness harness;
    match::wiring::InstallDefaultConditions(harness.conditions);

    CHECK_FALSE(harness.Eval(Cond("turns_elapsed", {{"cmp", "eq"}, {"n", 4}})));
    match::wiring::BindTurnsElapsed(harness.frame, 4);
    CHECK(harness.Eval(Cond("turns_elapsed", {{"cmp", "eq"}, {"n", 4}})));
    CHECK(harness.Eval(Cond("turns_elapsed", {{"cmp", "gte"}, {"n", 4}})));
    CHECK_FALSE(harness.Eval(Cond("turns_elapsed", {{"cmp", "lt"}, {"n", 4}})));
}

TEST_CASE("content_wiring: loads card tags into the per-match table") {
    CardDef red;
    red.kind_id = "vanilla:red_5";
    red.tags = {"stackable", "red"};
    CardDef blue;
    blue.kind_id = "vanilla:blue_2";
    blue.tags = {"cold"};
    CardDef unnamed;  // INFO: empty kind id is ignored.
    unnamed.tags = {"ignored"};

    MatchRegistries registries;
    match::wiring::LoadCardTags({red, blue, unnamed}, registries.card_tags);

    CHECK(CardHasTag(&registries, "vanilla:red_5", "stackable"));
    CHECK(CardHasTag(&registries, "vanilla:red_5", "red"));
    CHECK(CardHasTag(&registries, "vanilla:blue_2", "cold"));
    CHECK_FALSE(CardHasTag(&registries, "vanilla:red_5", "cold"));
    CHECK_FALSE(CardHasTag(&registries, "", "ignored"));
    CHECK(registries.card_tags.size() == 2);

    // INFO: a later load replaces the previous table entirely.
    match::wiring::LoadCardTags({blue}, registries.card_tags);
    CHECK_FALSE(CardHasTag(&registries, "vanilla:red_5", "stackable"));
    CHECK(CardHasTag(&registries, "vanilla:blue_2", "cold"));
    CHECK(registries.card_tags.size() == 1);
}

TEST_CASE("content_wiring: WireContent installs conditions and tags") {
    Harness harness;
    CardDef card;
    card.kind_id = "vanilla:red_7";
    card.tags = {"stackable"};

    MatchRegistries registries;
    match::wiring::WireContent(harness.conditions, {card},
                               registries.card_tags);

    CHECK(harness.conditions.Has("turns_elapsed"));
    CHECK(CardHasTag(&registries, "vanilla:red_7", "stackable"));
}
