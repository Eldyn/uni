#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/event_projector.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file view_event_audience_test.cpp
 * @brief Audience table and per-seat projector tests.
 *
 * One test for each rule of the hidden-information model: an undeclared field
 * is never sent, a field reaches only its audience, a hidden card loses its
 * instance bits and entity handle, and a seat never learns how many hidden
 * random draws happened.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;
using match::view::Audience;
using match::view::EventSink;
using match::view::SeatRoles;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

bool LoadContent(Content& out) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    LoadResult load = ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content,
                                          std::size_t players,
                                          uint64_t seed,
                                          uint32_t race_target = 0) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.race_target = race_target;
    options.seed = seed;
    for (std::size_t index = 0; index < players; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

json EntityRef(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

void MoveToHand(MatchInstance& engine, ecs::Entity player,
                ecs::Entity card) {
    ops::MoveCardToZone(engine.Store(), card,
                        ecs::ZoneRef{ecs::ZoneKind::kHand, player});
}

uint32_t BitsOf(const MatchInstance& engine, ecs::Entity card) {
    const std::optional<ecs::CompactCardV2> id =
        engine.Registries().CardId(card);
    REQUIRE(id.has_value());
    return id->bits;
}

}  // namespace

TEST_CASE("audience: a field that no table row declares has audience none") {
    CHECK(match::view::AudienceOf("card_played", "card") == Audience::kAll);
    CHECK(match::view::AudienceOf("card_played", "secret_note")
          == Audience::kNone);
    CHECK(match::view::AudienceOf("event_nobody_declared", "card")
          == Audience::kNone);
    CHECK(match::view::AudienceOf("cards_drawn", "cards")
          == Audience::kOwner);
    CHECK(match::view::AudienceOf("play_rejected", "reason_id")
          == Audience::kTarget);
}

TEST_CASE("audience: the projector never sends an undeclared field") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    const json wire = json{{"player", "player0"},
                           {"card", 0},
                           {"from_zone_ordinal", 3},
                           {"secret_note", "next card is red 5"}};
    const json projected = match::view::ProjectForViewer(
        *engine, "card_played", wire, SeatRoles{},
        Viewer::Player("player1"));
    CHECK(projected.contains("player"));
    CHECK(projected.contains("from_zone_ordinal"));
    CHECK_FALSE(projected.contains("secret_note"));

    const json unknown_event = match::view::ProjectForViewer(
        *engine, "event_nobody_declared", wire, SeatRoles{},
        Viewer::Player("player1"));
    CHECK(unknown_event.empty());
}

TEST_CASE("audience: actor, target and owner fields reach only that seat") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    SeatRoles roles;
    roles.actor = "player0";
    roles.target = "player1";
    roles.owner = "player2";
    CHECK(match::view::AudienceAllows(Audience::kAll, roles,
                                      Viewer::Spectator()));
    CHECK(match::view::AudienceAllows(Audience::kActor, roles,
                                      Viewer::Player("player0")));
    CHECK_FALSE(match::view::AudienceAllows(Audience::kActor, roles,
                                            Viewer::Player("player1")));
    CHECK(match::view::AudienceAllows(Audience::kTarget, roles,
                                      Viewer::Player("player1")));
    CHECK_FALSE(match::view::AudienceAllows(Audience::kTarget, roles,
                                            Viewer::Player("player0")));
    CHECK(match::view::AudienceAllows(Audience::kOwner, roles,
                                      Viewer::Player("player2")));
    CHECK(match::view::AudienceAllows(Audience::kOwner, roles,
                                      Viewer::Spectator()));
    CHECK_FALSE(match::view::AudienceAllows(Audience::kOwner, roles,
                                            Viewer::Spectator({"player2"})));
    CHECK_FALSE(match::view::AudienceAllows(Audience::kOwner, SeatRoles{},
                                            Viewer::Spectator()));
    CHECK_FALSE(match::view::AudienceAllows(Audience::kNone, roles,
                                            Viewer::Player("player0")));
    CHECK_FALSE(match::view::AudienceAllows(Audience::kActor, SeatRoles{},
                                            Viewer::Player("")));

    const json wire = json{{"prompt_id", "choose_color"},
                           {"kind", "choose_color"},
                           {"payload", json::object()}};
    SeatRoles prompt_roles;
    prompt_roles.target = "player1";
    CHECK(match::view::ProjectForViewer(*engine, "prompt_opened", wire,
                                        prompt_roles,
                                        Viewer::Player("player1"))
              .contains("kind"));
    CHECK(match::view::ProjectForViewer(*engine, "prompt_opened", wire,
                                        prompt_roles,
                                        Viewer::Player("player2"))
              .empty());
}

TEST_CASE("audience: roles come from the engine descriptor keys") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    const ecs::Entity player1 = *engine->FindPlayer("player1");

    const SeatRoles drawn = match::view::ResolveSeatRoles(
        *engine, "cards_drawn", json{{"player", EntityRef(player1)}},
        "player0");
    CHECK(drawn.actor == "player0");
    CHECK(drawn.owner == "player1");

    const SeatRoles rejected = match::view::ResolveSeatRoles(
        *engine, "play_rejected", json{{"player", "player2"}}, "");
    CHECK(rejected.target == "player2");
    CHECK(rejected.actor.empty());
}

TEST_CASE("audience: a hidden card loses its instance bits and handle") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity own_card = engine->Registries().cards[0];
    const ecs::Entity other_card = engine->Registries().cards[1];
    MoveToHand(*engine, player0, own_card);
    MoveToHand(*engine, player1, other_card);

    SeatRoles roles;
    roles.owner = "player0";
    const json wire = json{
        {"player", "player0"},
        {"count", 2},
        {"cards", json::array({BitsOf(*engine, own_card),
                               BitsOf(*engine, other_card)})}};
    const json seen = match::view::ProjectForViewer(
        *engine, "cards_drawn", wire, roles, Viewer::Player("player0"));
    REQUIRE(seen.contains("cards"));
    CHECK(seen["cards"]
          == json::array({BitsOf(*engine, own_card)}));

    const json prompt = json{
        {"prompt_id", "choose_card"},
        {"kind", "choose_card"},
        {"payload",
         json{{"candidates", json::array({EntityRef(own_card),
                                          EntityRef(other_card)})}}}};
    SeatRoles prompt_roles;
    prompt_roles.target = "player0";
    const json prompt_seen = match::view::ProjectForViewer(
        *engine, "prompt_opened", prompt, prompt_roles,
        Viewer::Player("player0"));
    const json& candidates = prompt_seen["payload"]["candidates"];
    REQUIRE(candidates.size() == 2);
    CHECK(candidates[0] == EntityRef(own_card));
    CHECK(candidates[1].is_null());
    CHECK(prompt_seen.dump().find("\"generation\":"
                                  + std::to_string(other_card.generation)
                                  + ",\"index\":"
                                  + std::to_string(other_card.index))
          == std::string::npos);
}

TEST_CASE("audience: the roll counter does not count hidden random draws") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);
    EventSink sink;

    const auto roll = [&](int engine_counter) {
        return builder.Wrap(
            json{{"type", "outcome_rolled"},
                 {"payload", json{{"spec", json{{"sides", 6}}},
                                  {"outcomes", json::array({3})},
                                  {"counter", engine_counter}}}},
            Viewer::Player("player1"), sink);
    };
    const std::optional<json> first = roll(5);
    const std::optional<json> second = roll(97);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK((*first)["payload"]["roll_ordinal"] == 1);
    CHECK((*second)["payload"]["roll_ordinal"] == 2);

    const std::optional<json> pick = builder.Wrap(
        json{{"type", "outcome_rolled"},
             {"payload", json{{"table", "uni:t"},
                              {"key", "uni:k"},
                              {"value", 1},
                              {"counter", 311}}}},
        Viewer::Player("player1"), sink);
    REQUIRE(pick.has_value());
    CHECK((*pick)["payload"]["roll_ordinal"] == 3);
    CHECK(pick->dump().find("311") == std::string::npos);
}

TEST_CASE("audience: a losing window response hides its card from others") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity card = engine->Registries().cards[0];
    MoveToHand(*engine, player1, card);

    const auto response = [&](const std::string& outcome) {
        return json{{"type", "window_responded"},
                    {"payload", json{{"window", 7},
                                     {"player", "player1"},
                                     {"card", EntityRef(card)},
                                     {"pass", false},
                                     {"outcome", outcome}}}};
    };
    const auto wrap = [&](const std::string& viewer, const json& event) {
        EventSink sink;
        return *builder.Wrap(event, Viewer::Player(viewer), sink);
    };

    CHECK_FALSE(wrap("player2", response("lost"))["payload"].contains("card"));
    CHECK(wrap("player1", response("lost"))["payload"].contains("card"));
    CHECK(wrap("player2", response("winning"))["payload"].contains("card"));
}

TEST_CASE("audience: window_responded carries window_kind and outcome") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity card = engine->Registries().cards[0];
    MoveToHand(*engine, player1, card);

    EventSink sink;
    const json played = *builder.Wrap(
        json{{"type", "window_responded"},
             {"payload", json{{"window", 3},
                              {"player", "player1"},
                              {"card", EntityRef(card)},
                              {"kind", "red_draw2"},
                              {"window_kind", "jump_in"},
                              {"pass", false},
                              {"outcome", "winning"}}}},
        Viewer::Player("player1"), sink);
    CHECK(played["payload"]["window_kind"] == "jump_in");
    CHECK(played["payload"]["outcome"] == "winning");

    const json passed = *builder.Wrap(
        json{{"type", "window_responded"},
             {"payload", json{{"window", 3},
                              {"player", "player1"},
                              {"pass", true},
                              {"window_kind", "jump_in"},
                              {"outcome", "pass"}}}},
        Viewer::Player("player2"), sink);
    CHECK(passed["payload"]["passed"] == true);
    CHECK(passed["payload"]["outcome"] == "pass");
    CHECK_FALSE(passed["payload"].contains("card"));
}

TEST_CASE("audience: a lost window_responded keeps its card hidden") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    ViewBuilder builder(*engine, content.mods);
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity card = engine->Registries().cards[0];
    MoveToHand(*engine, player1, card);

    const json lost = json{{"type", "window_responded"},
                           {"payload", json{{"window", 3},
                                            {"player", "player1"},
                                            {"card", EntityRef(card)},
                                            {"pass", false},
                                            {"window_kind", "jump_in"},
                                            {"outcome", "lost"}}}};
    EventSink sink;
    const json other = *builder.Wrap(lost, Viewer::Player("player2"), sink);
    CHECK_FALSE(other["payload"].contains("card"));
    CHECK(other["payload"]["outcome"] == "lost");
    CHECK(builder.Wrap(lost, Viewer::Player("player1"), sink)
              ->at("payload")
              .contains("card"));
}

TEST_CASE("audience: placement_set carries finished from the race target") {
    Content content;
    REQUIRE(LoadContent(content));
    const auto finished = [&](MatchInstance& engine, int place,
                              bool is_elimination) {
        ViewBuilder builder(engine, content.mods);
        EventSink sink;
        const json wrapped = *builder.Wrap(
            json{{"type", "placement_set"},
                 {"payload", json{{"player", "player1"},
                                  {"place", place},
                                  {"is_elimination", is_elimination}}}},
            Viewer::Player("player2"), sink);
        return wrapped["payload"]["finished"].get<bool>();
    };

    std::unique_ptr<MatchInstance> race = MakeEngine(content, 4, 42, 2);
    CHECK(finished(*race, 1, false));
    CHECK(finished(*race, 2, false));
    CHECK_FALSE(finished(*race, 3, false));
    CHECK_FALSE(finished(*race, 1, true));

    std::unique_ptr<MatchInstance> standard = MakeEngine(content, 4, 42);
    CHECK(finished(*standard, 1, false));
    CHECK_FALSE(finished(*standard, 2, false));
}
