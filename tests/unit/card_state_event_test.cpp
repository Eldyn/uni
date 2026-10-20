#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/view/event_projector.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/event_tail.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file card_state_event_test.cpp
 * @brief `card_changed` projection and redaction by attribute/property
 *        visibility.
 *
 * The tests drive the Task 7 ops through the engine's default runtime and
 * project the emitted descriptor with the same `ViewBuilder` path the live
 * stream uses. An `owner` value reaches the holder and a privacy-off
 * spectator; a `strict_owner` value reaches only the holder; a `hidden` value
 * reaches nobody. A card in the discard pile shows its identity but not a
 * non-public value.
 */

namespace {

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadResult;
using match::modload::LoadedMod;
using match::view::EventSink;
using match::view::EventTail;
using match::view::SeatRoles;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        const fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

fs::path FixtureClean() {
    return ProjectRoot() / "tests" / "fixtures" / "card_schema" / "clean";
}

std::vector<LoadedMod> LoadMods() {
    const LoadResult load =
        match::modload::ScanModsDirectory(FixtureClean().string());
    REQUIRE(load.ok());
    return load.mods;
}

DeckDef Deck() {
    DeckDef deck;
    deck.id = "card_state_event";
    deck.namespace_id = "test";
    deck.deck_id = "test:card_state_event";
    deck.name = "Card state event";
    deck.mods = {"uni"};
    // INFO: the six trinkets cover both values of every slot. The numbered
    //       cards are the plan-1 generated cards.
    deck.cards = {{"uni:red_spade_alpha", 1},
                  {"uni:red_spade_beta", 1},
                  {"uni:red_heart_alpha", 1},
                  {"uni:red_heart_beta", 1},
                  {"uni:blue_spade_alpha", 1},
                  {"uni:blue_heart_beta", 1},
                  {"uni:red_1", 6}};
    return deck;
}

std::unique_ptr<MatchInstance> MakeMatch(const std::vector<LoadedMod>& mods) {
    MatchAssemblyOptions options;
    options.starting_cards = 0;
    options.seed = 1;
    for (const char* player : {"Alice", "Bob"}) {
        MatchPlayerSpec spec;
        spec.username = player;
        options.players.push_back(spec);
    }
    auto assembly = MatchAssembler::Assemble(mods, Deck(), options);
    REQUIRE(assembly.ok());
    return std::make_unique<MatchInstance>(std::move(assembly.assembly));
}

ecs::Entity FindCard(MatchInstance& engine, const std::string& kind_id) {
    for (const ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) {
            return card;
        }
    }
    FAIL("card not found: " << kind_id);
    return {};
}

void MoveTo(MatchInstance& engine, ecs::Entity card, ecs::ZoneRef zone) {
    REQUIRE(ops::MoveCardToZone(engine.Store(), card, zone));
}

ecs::Entity HandCard(MatchInstance& engine, const std::string& kind_id) {
    const ecs::Entity card = FindCard(engine, kind_id);
    REQUIRE(engine.FindPlayer("Alice").has_value());
    MoveTo(engine, card,
           ecs::ZoneRef{ecs::ZoneKind::kHand, *engine.FindPlayer("Alice")});
    return card;
}

void LimboCard(MatchInstance& engine, ecs::Entity card) {
    MoveTo(engine, card, ecs::ZoneRef{ecs::ZoneKind::kLimbo, ecs::Entity{}});
}

ops::OpResult RunOp(MatchInstance& engine, const std::string& op,
                    const json& raw, ecs::Entity card) {
    ops::OpArgs args(op, raw);
    args.BindSelector("card", {card});
    ops::ResolutionFrame frame;
    ops::OpContext context(engine.Assembly().bus, engine.Assembly().budget,
                           frame);
    context.registries = &engine.Registries();
    return engine.Assembly().runtime.Invoke(op, engine.Store(), args, context);
}

/**
 * @brief The `card_changed` payload this viewer receives, if any.
 *
 * A dropped event never becomes a packet: `ViewBuilder::Wrap` returns
 * `nullopt` when no field survives. A delivered packet must carry a
 * non-empty payload.
 */
std::optional<json> CardChangedFor(ViewBuilder& builder, const json& event,
                                   const Viewer& viewer) {
    EventSink sink;
    const std::optional<json> packet = builder.Wrap(event, viewer, sink);
    if (!packet.has_value()) return std::nullopt;
    if (packet->value("id", std::string()) != "card_changed") {
        return std::nullopt;
    }
    return (*packet)["payload"];
}

const Viewer kAlice = Viewer::Player("Alice");
const Viewer kBob = Viewer::Player("Bob");
const Viewer kOwnerSpectator = Viewer::Spectator();
const Viewer kPrivateSpectator = Viewer::Spectator({"Alice"});
const Viewer kSpectator = Viewer::PublicSpectator();
const Viewer kDirector = Viewer::PlayerDirector("Carol");

/** @brief Set one loaded attribute's visibility before assembly. */
void SetAttributeVisibility(std::vector<LoadedMod>& mods,
                            const std::string& identifier,
                            const std::string& visibility) {
    for (LoadedMod& mod : mods) {
        for (match::cardschema::AttributeDef& attribute :
             mod.card_schema.attributes) {
            if (attribute.identifier != identifier) continue;
            attribute.visibility = visibility;
            return;
        }
    }
    FAIL("attribute not found: " << identifier);
}

/** @brief Set one loaded property's visibility before assembly. */
void SetPropertyVisibility(std::vector<LoadedMod>& mods,
                           const std::string& identifier,
                           const std::string& visibility) {
    for (LoadedMod& mod : mods) {
        for (match::cardschema::PropertyDef& property :
             mod.card_schema.properties) {
            if (property.identifier != identifier) continue;
            property.visibility = visibility;
            return;
        }
    }
    FAIL("property not found: " << identifier);
}

json Emit(MatchInstance& engine, const std::string& op, const json& raw,
          ecs::Entity card) {
    const ops::OpResult result = RunOp(engine, op, raw, card);
    REQUIRE(result.status == ops::OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
    REQUIRE(result.events.front().value("type", std::string())
            == "card_changed");
    return result.events.front();
}

}  // namespace

TEST_SUITE("card state event") {

TEST_CASE("public_change_reaches_everyone") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "color"}, {"member", "uni:blue"}}, card);

    for (const Viewer* viewer : {&kAlice, &kBob, &kSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const std::optional<json> payload =
            CardChangedFor(builder, event, *viewer);
        REQUIRE(payload.has_value());
        CHECK((*payload)["slot"] == "color");
        CHECK((*payload)["member"] == "uni:blue");
        CHECK((*payload)["previous"] == "uni:red");
        if (viewer != &kAlice) CHECK_FALSE(payload->contains("card"));
        if (viewer == &kAlice) CHECK(payload->contains("card"));
    }
}

TEST_CASE("owner_change_reaches_holder_and_privacy_off_spectator") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    const std::optional<json> alice =
        CardChangedFor(builder, event, kAlice);
    REQUIRE(alice.has_value());
    CHECK((*alice)["slot"] == "suit");
    CHECK((*alice)["member"] == "uni:heart");
    CHECK((*alice)["previous"] == "uni:spade");
    CHECK(alice->contains("card"));

    const std::optional<json> owner_spectator =
        CardChangedFor(builder, event, kOwnerSpectator);
    REQUIRE(owner_spectator.has_value());
    CHECK((*owner_spectator)["member"] == "uni:heart");
    CHECK((*owner_spectator)["previous"] == "uni:spade");
    CHECK(owner_spectator->contains("card"));

    CHECK_FALSE(CardChangedFor(builder, event, kPrivateSpectator).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kBob).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kSpectator).has_value());
}

TEST_CASE("strict_owner_reaches_the_holder_only") {
    std::vector<LoadedMod> mods = LoadMods();
    SetAttributeVisibility(mods, "uni:suit", "strict_owner");
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    const std::optional<json> alice =
        CardChangedFor(builder, event, kAlice);
    REQUIRE(alice.has_value());
    CHECK((*alice)["slot"] == "suit");
    CHECK((*alice)["member"] == "uni:heart");
    CHECK((*alice)["previous"] == "uni:spade");
    CHECK(alice->contains("card"));

    const std::optional<json> owner_spectator =
        CardChangedFor(builder, event, kOwnerSpectator);
    REQUIRE(owner_spectator.has_value());
    CHECK((*owner_spectator)["slot"] == "suit");
    CHECK(owner_spectator->contains("card"));
    CHECK_FALSE(owner_spectator->contains("member"));
    CHECK_FALSE(owner_spectator->contains("previous"));

    CHECK_FALSE(CardChangedFor(builder, event, kPrivateSpectator).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kBob).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kSpectator).has_value());

    const std::optional<json> director =
        CardChangedFor(builder, event, kDirector);
    REQUIRE(director.has_value());
    CHECK((*director)["slot"] == "suit");
    CHECK(director->contains("card"));
    CHECK_FALSE(director->contains("member"));
    CHECK_FALSE(director->contains("previous"));
}

TEST_CASE("strict_owner_property_reaches_the_holder_only") {
    std::vector<LoadedMod> mods = LoadMods();
    SetPropertyVisibility(mods, "uni:revealed", "strict_owner");
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_property",
             json{{"property", "uni:revealed"}, {"value", true}}, card);

    const std::optional<json> alice =
        CardChangedFor(builder, event, kAlice);
    REQUIRE(alice.has_value());
    CHECK((*alice)["property"] == "uni:revealed");
    CHECK((*alice)["value"] == true);
    CHECK((*alice)["previous"] == false);
    CHECK(alice->contains("card"));

    const std::optional<json> owner_spectator =
        CardChangedFor(builder, event, kOwnerSpectator);
    REQUIRE(owner_spectator.has_value());
    CHECK((*owner_spectator)["property"] == "uni:revealed");
    CHECK(owner_spectator->contains("card"));
    CHECK_FALSE(owner_spectator->contains("value"));
    CHECK_FALSE(owner_spectator->contains("previous"));

    CHECK_FALSE(CardChangedFor(builder, event, kPrivateSpectator).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kBob).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kSpectator).has_value());

    const std::optional<json> director =
        CardChangedFor(builder, event, kDirector);
    REQUIRE(director.has_value());
    CHECK((*director)["property"] == "uni:revealed");
    CHECK(director->contains("card"));
    CHECK_FALSE(director->contains("value"));
    CHECK_FALSE(director->contains("previous"));
}

TEST_CASE("strict_owner_on_discard_has_no_value") {
    std::vector<LoadedMod> mods = LoadMods();
    SetAttributeVisibility(mods, "uni:suit", "strict_owner");
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = FindCard(*engine, "uni:red_spade_alpha");
    MoveTo(*engine, card,
           ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    for (const Viewer* viewer : {&kAlice, &kBob, &kOwnerSpectator,
                                 &kPrivateSpectator, &kSpectator, &kDirector}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const std::optional<json> payload =
            CardChangedFor(builder, event, *viewer);
        REQUIRE(payload.has_value());
        CHECK(payload->contains("card"));
        CHECK((*payload)["slot"] == "suit");
        CHECK_FALSE(payload->contains("member"));
        CHECK_FALSE(payload->contains("previous"));
    }
}

TEST_CASE("limbo_change_has_no_card_identity") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    LimboCard(*engine, card);

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "color"}, {"member", "uni:blue"}}, card);

    for (const Viewer* viewer : {&kAlice, &kBob, &kOwnerSpectator, &kSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const std::optional<json> payload =
            CardChangedFor(builder, event, *viewer);
        REQUIRE(payload.has_value());
        CHECK((*payload)["slot"] == "color");
        CHECK((*payload)["member"] == "uni:blue");
        CHECK_FALSE(payload->contains("card"));
    }
}

TEST_CASE("non_synced_property_keeps_value_off_the_event") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_property",
             json{{"property", "uni:locked"}, {"value", true}}, card);

    for (const Viewer* viewer : {&kAlice, &kOwnerSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const std::optional<json> payload =
            CardChangedFor(builder, event, *viewer);
        REQUIRE(payload.has_value());
        CHECK((*payload)["property"] == "uni:locked");
        CHECK(payload->contains("card"));
        CHECK_FALSE(payload->contains("value"));
        CHECK_FALSE(payload->contains("previous"));
    }

    for (const Viewer* viewer :
         {&kBob, &kPrivateSpectator, &kSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(CardChangedFor(builder, event, *viewer).has_value());
    }

    const std::optional<json> director =
        CardChangedFor(builder, event, kDirector);
    REQUIRE(director.has_value());
    CHECK((*director)["property"] == "uni:locked");
    CHECK(director->contains("card"));
    CHECK_FALSE(director->contains("value"));
    CHECK_FALSE(director->contains("previous"));
}

TEST_CASE("hidden_change_reaches_nobody_with_value") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "secret"}, {"member", "uni:beta"}}, card);

    const std::optional<json> alice =
        CardChangedFor(builder, event, kAlice);
    REQUIRE(alice.has_value());
    CHECK((*alice)["slot"] == "secret");
    CHECK(alice->contains("card"));
    CHECK_FALSE(alice->contains("member"));
    CHECK_FALSE(alice->contains("previous"));

    CHECK_FALSE(CardChangedFor(builder, event, kBob).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kSpectator).has_value());
}

TEST_CASE("discard_pile_card_is_public_identity") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = FindCard(*engine, "uni:red_spade_alpha");
    MoveTo(*engine, card,
           ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    for (const Viewer* viewer : {&kAlice, &kBob, &kSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const std::optional<json> payload =
            CardChangedFor(builder, event, *viewer);
        REQUIRE(payload.has_value());
        CHECK(payload->contains("card"));
        CHECK((*payload)["slot"] == "suit");
        CHECK_FALSE(payload->contains("member"));
        CHECK_FALSE(payload->contains("previous"));
    }
}

TEST_CASE("property_visibility_owner") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_property",
             json{{"property", "uni:revealed"}, {"value", true}}, card);

    const std::optional<json> alice =
        CardChangedFor(builder, event, kAlice);
    REQUIRE(alice.has_value());
    CHECK((*alice)["property"] == "uni:revealed");
    CHECK((*alice)["value"] == true);
    CHECK((*alice)["previous"] == false);
    CHECK(alice->contains("card"));

    const std::optional<json> owner_spectator =
        CardChangedFor(builder, event, kOwnerSpectator);
    REQUIRE(owner_spectator.has_value());
    CHECK((*owner_spectator)["value"] == true);
    CHECK(owner_spectator->contains("card"));

    CHECK_FALSE(CardChangedFor(builder, event, kPrivateSpectator).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kBob).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kSpectator).has_value());
}

TEST_CASE("a_dropped_event_does_not_consume_a_sequence") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    EventSink sink;
    CHECK_FALSE(builder.Wrap(event, kBob, sink).has_value());
    CHECK(sink.NextSeq() == 0);
    const std::optional<json> alice = builder.Wrap(event, kAlice, sink);
    REQUIRE(alice.has_value());
    CHECK((*alice)["sequence"] == 0);
    CHECK(sink.NextSeq() == 1);
}

TEST_CASE("wrap_public_resolves_public_hand_roles") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "color"}, {"member", "uni:blue"}}, card);

    EventSink sink;
    const std::optional<json> packet = sink.WrapPublic(event, *engine);
    REQUIRE(packet.has_value());
    const json& payload = (*packet)["payload"];
    CHECK(payload["slot"] == "color");
    CHECK(payload["member"] == "uni:blue");
    CHECK(payload["previous"] == "uni:red");
    CHECK_FALSE(payload.contains("_card"));
    CHECK_FALSE(payload.contains("card"));
    CHECK(sink.NextSeq() == 1);
}

TEST_CASE("wrap_public_drops_private_hand_changes") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json owner_event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);
    const json hidden_event =
        Emit(*engine, "set_card_member",
             json{{"slot", "secret"}, {"member", "uni:beta"}}, card);

    EventSink sink;
    CHECK_FALSE(sink.WrapPublic(owner_event, *engine).has_value());
    CHECK(sink.NextSeq() == 0);
    CHECK_FALSE(sink.WrapPublic(hidden_event, *engine).has_value());
    CHECK(sink.NextSeq() == 0);
}

TEST_CASE("non_string_visibility_resolves_hidden") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);
    event["payload"]["visibility"] = 5;

    SeatRoles roles;
    CHECK_NOTHROW(roles = match::view::ResolveSeatRoles(
                      *engine, "card_changed", event["payload"], "Alice"));
    CHECK_FALSE(roles.state_public);
    CHECK_FALSE(roles.state_owner);
    CHECK_FALSE(roles.state_strict_owner);

    std::optional<json> projected;
    CHECK_NOTHROW(projected = match::view::ProjectPublicEvent(
                      "card_changed", event["payload"], *engine));
    REQUIRE(projected.has_value());
    CHECK((*projected)["slot"] == "suit");
    CHECK_FALSE(projected->contains("member"));
    CHECK_FALSE(projected->contains("value"));
    CHECK_FALSE(projected->contains("previous"));
}

TEST_CASE("director_sees_hand_card_bits_without_private_values") {
    const Viewer director = Viewer::PlayerDirector("Bob");
    {
        const std::vector<LoadedMod> mods = LoadMods();
        const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
        ViewBuilder builder(*engine, mods);
        const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

        const json public_event =
            Emit(*engine, "set_card_member",
                 json{{"slot", "color"}, {"member", "uni:blue"}}, card);
        const std::optional<json> public_payload =
            CardChangedFor(builder, public_event, director);
        REQUIRE(public_payload.has_value());
        CHECK((*public_payload)["slot"] == "color");
        CHECK((*public_payload)["member"] == "uni:blue");
        CHECK(public_payload->contains("card"));

        const json owner_event =
            Emit(*engine, "set_card_member",
                 json{{"slot", "suit"}, {"member", "uni:heart"}}, card);
        const std::optional<json> owner_payload =
            CardChangedFor(builder, owner_event, director);
        REQUIRE(owner_payload.has_value());
        CHECK((*owner_payload)["slot"] == "suit");
        CHECK(owner_payload->contains("card"));
        CHECK_FALSE(owner_payload->contains("member"));
        CHECK_FALSE(owner_payload->contains("previous"));

        MoveTo(*engine, card,
               ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
        const json draw_event =
            Emit(*engine, "set_card_member",
                 json{{"slot", "color"}, {"member", "uni:red"}}, card);
        const std::optional<json> draw_payload =
            CardChangedFor(builder, draw_event, director);
        REQUIRE(draw_payload.has_value());
        CHECK_FALSE(draw_payload->contains("card"));
    }
    {
        std::vector<LoadedMod> mods = LoadMods();
        SetAttributeVisibility(mods, "uni:suit", "strict_owner");
        const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
        ViewBuilder builder(*engine, mods);
        const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

        const json event =
            Emit(*engine, "set_card_member",
                 json{{"slot", "suit"}, {"member", "uni:heart"}}, card);
        const std::optional<json> payload =
            CardChangedFor(builder, event, director);
        REQUIRE(payload.has_value());
        CHECK((*payload)["slot"] == "suit");
        CHECK(payload->contains("card"));
        CHECK_FALSE(payload->contains("member"));
        CHECK_FALSE(payload->contains("previous"));
    }
}

TEST_CASE("late_join_tail_keeps_owner_value_after_hand_move") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);
    EventTail tail;
    tail.Record(event);

    MoveTo(*engine, card,
           ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});

    const json alice_envelopes = builder.BuildTail(kAlice, tail);
    REQUIRE(alice_envelopes.size() == 1);
    const json& alice_payload = alice_envelopes.front()["payload"];
    CHECK(alice_payload["member"] == "uni:heart");
    CHECK(alice_payload["previous"] == "uni:spade");

    const json bob_envelopes = builder.BuildTail(kBob, tail);
    CHECK(bob_envelopes.empty());

    MoveTo(*engine, card, ecs::ZoneRef{ecs::ZoneKind::kLimbo, ecs::Entity{}});
    const json alice_limbo = builder.BuildTail(kAlice, tail);
    REQUIRE(alice_limbo.size() == 1);
    CHECK(alice_limbo.front()["payload"]["member"] == "uni:heart");
    CHECK_FALSE(alice_limbo.front()["payload"].contains("card"));
    CHECK(builder.BuildTail(kBob, tail).empty());
}

TEST_CASE("public_body_is_internal_and_wire_redacts_the_handle") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json owner_event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    const std::optional<json> owner_body = match::view::ProjectPublicEvent(
        "card_changed", owner_event["payload"], *engine);
    REQUIRE(owner_body.has_value());
    CHECK(owner_body->contains("_card"));
    CHECK((*owner_body)["member"] == "uni:heart");

    EventSink sink;
    CHECK_FALSE(sink.WrapPublic(owner_event, *engine).has_value());
    CHECK(sink.NextSeq() == 0);

    const json public_event =
        Emit(*engine, "set_card_member",
             json{{"slot", "color"}, {"member", "uni:blue"}}, card);

    const std::optional<json> public_body = match::view::ProjectPublicEvent(
        "card_changed", public_event["payload"], *engine);
    REQUIRE(public_body.has_value());
    CHECK(public_body->contains("_card"));

    const std::optional<json> wire = sink.WrapPublic(public_event, *engine);
    REQUIRE(wire.has_value());
    const json& wire_payload = (*wire)["payload"];
    CHECK(wire_payload["member"] == "uni:blue");
    CHECK_FALSE(wire_payload.contains("_card"));
    CHECK_FALSE(wire_payload.contains("card"));
    CHECK_FALSE(wire_payload.contains("in_hand"));
}

TEST_CASE("late_join_tail_respects_redaction") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    EventTail tail;
    tail.Record(event);
    const json alice_envelopes = builder.BuildTail(kAlice, tail);
    REQUIRE(alice_envelopes.size() == 1);
    CHECK(alice_envelopes.front()["payload"]["member"] == "uni:heart");

    const json bob_envelopes = builder.BuildTail(kBob, tail);
    CHECK(bob_envelopes.empty());
}
}  // namespace
