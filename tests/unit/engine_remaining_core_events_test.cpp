#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "support/leak_scanner.hpp"

/**
 * @file engine_remaining_core_events_test.cpp
 * @brief The window, prompt, chance, status, visibility and match-end client
 * events, from the engine to the seat stream.
 *
 * The engine emits each new event one time, with its own sequence number.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;
using match::view::EventSink;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

constexpr int kPlayerCount = 3;

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

Content LoadContent() {
    Content content;
    LoadResult load = ScanModsDirectory((ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    content.mods = std::move(load.mods);
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") content.classic = deck;
        }
    }
    REQUIRE_FALSE(content.classic.deck_id.empty());
    return content;
}

std::unique_ptr<MatchInstance> MakeEngine(
    Content& content, const std::vector<std::string>& mods) {
    DeckDef deck = content.classic;
    deck.mods = mods;
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < kPlayerCount; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

json EntityRef(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

std::optional<ecs::Entity> FindCard(MatchInstance& engine,
                                    const std::string& color,
                                    const std::string& label) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == color && face->label == label) {
            return card;
        }
    }
    return std::nullopt;
}

void ForceHand(MatchInstance& engine, ecs::Entity player,
               const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

std::vector<json> EventsOfType(const MatchInstance& engine,
                               const std::string& type) {
    std::vector<json> found;
    for (const json& event : engine.Events()) {
        if (event.value("type", std::string()) == type) {
            found.push_back(event);
        }
    }
    return found;
}

std::optional<json> Wrap(const MatchInstance& engine, const Content& content,
                         const json& event, const Viewer& viewer,
                         EventSink& sink) {
    ViewBuilder builder(engine, content.mods);
    return builder.Wrap(event, viewer, sink);
}

/** @brief Play a +2 so that a window opens, with a draw stacking engine. */
std::unique_ptr<MatchInstance> OpenStackingWindow(Content& content) {
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, {"uni", "draw_stacking"});
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> draw_two =
        FindCard(*engine, "red", "+2");
    REQUIRE(draw_two.has_value());
    ForceHand(*engine, player0, {*draw_two});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";
    REQUIRE(engine->PlayCard("player0", *draw_two));
    REQUIRE(engine->WindowOpen());
    return engine;
}

}  // namespace

TEST_CASE("windows: each window event happens one time") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = OpenStackingWindow(content);
    const json window = engine->ExportWindow();
    const std::string responder =
        window["responders"][0].is_string()
            ? window["responders"][0].get<std::string>()
            : std::string("player1");
    REQUIRE(engine->PassWindow(responder));

    for (const char* name :
         {"window_opened", "window_responded", "window_closed"}) {
        CAPTURE(name);
        CHECK(EventsOfType(*engine, name).size() == 1);
    }
}

TEST_CASE("windows: the wire payloads use the generic field names") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = OpenStackingWindow(content);
    const std::vector<json> opened = EventsOfType(*engine, "window_opened");
    REQUIRE(opened.size() == 1);
    EventSink sink;
    const std::optional<json> wrapped = Wrap(
        *engine, content, opened[0], Viewer::Player("player2"), sink);
    REQUIRE(wrapped.has_value());
    const json& payload = (*wrapped)["payload"];
    CHECK((*wrapped)["id"] == "window_opened");
    CHECK(payload.contains("targets"));
    CHECK_FALSE(payload.contains("responders"));
    CHECK(payload.contains("window_id"));

    const json window = engine->ExportWindow();
    REQUIRE(engine->PassWindow(
        window["responders"][0].get<std::string>()));
    const std::vector<json> responded =
        EventsOfType(*engine, "window_responded");
    REQUIRE(responded.size() == 1);
    const std::optional<json> response = Wrap(
        *engine, content, responded[0], Viewer::Player("player2"), sink);
    REQUIRE(response.has_value());
    CHECK((*response)["payload"]["passed"] == true);
    CHECK((*response)["payload"].contains("target"));
    CHECK_FALSE((*response)["payload"].contains("player"));
}

TEST_CASE("windows: a closing window with a winner names it as target") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, {"uni"});
    const json closing =
        json{{"type", "window_closed"},
             {"payload", json{{"id", 4},
                              {"outcome", "response"},
                              {"winner", "player1"}}}};
    EventSink sink;
    const std::optional<json> wrapped =
        Wrap(*engine, content, closing, Viewer::Player("player2"), sink);
    REQUIRE(wrapped.has_value());
    CHECK((*wrapped)["payload"]["outcome"] == "winner");
    CHECK((*wrapped)["payload"]["target"] == "player1");
    CHECK((*wrapped)["payload"]["window_id"] == "4");
    CHECK_FALSE((*wrapped)["payload"].contains("winner"));
}

TEST_CASE("windows: a losing response hides the card from other seats") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, {"uni"});
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity card = engine->Registries().cards[0];
    ForceHand(*engine, player1, {card});
    const std::optional<ecs::CompactCardV2> identity =
        engine->Registries().CardId(card);
    REQUIRE(identity.has_value());
    const leak_scanner::HiddenCard hidden{card, identity->bits};

    const auto response = [&](const std::string& outcome) {
        return json{{"type", "window_responded"},
                    {"payload", json{{"window", 7},
                                     {"player", "player1"},
                                     {"card", EntityRef(card)},
                                     {"pass", false},
                                     {"outcome", outcome}}}};
    };
    const auto wrap = [&](const Viewer& viewer, const json& event) {
        EventSink sink;
        return *Wrap(*engine, content, event, viewer, sink);
    };

    const std::vector<Viewer> others = {Viewer::Player("player0"),
                                        Viewer::Player("player2"),
                                        Viewer::PublicSpectator()};
    for (const Viewer& viewer : others) {
        const json packet = wrap(viewer, response("lost"));
        CHECK_FALSE(packet["payload"].contains("card"));
        CHECK(leak_scanner::FindLeaks(packet, {hidden}).empty());
    }
    CHECK(wrap(Viewer::Player("player1"), response("lost"))["payload"]
              .contains("card"));
    CHECK(wrap(Viewer::Player("player0"), response("winning"))["payload"]
              .contains("card"));
    json without_outcome = response("winning");
    without_outcome["payload"].erase("outcome");
    CHECK(wrap(Viewer::Player("player0"), without_outcome)["payload"]
              ["outcome"] == "winning");
}

TEST_CASE("windows: both names take sequence numbers without a gap") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = OpenStackingWindow(content);
    ViewBuilder builder(*engine, content.mods);
    for (const std::string username : {"player0", "player1", "player2"}) {
        CAPTURE(username);
        const std::vector<json> packets =
            builder.BuildPackets(Viewer::Player(username));
        uint32_t expected = 0;
        for (const json& packet : packets) {
            CHECK(packet["sequence"].get<uint32_t>() == expected);
            ++expected;
        }
    }
}

TEST_CASE("chance: outcome_rolled counts the public ordinal one time") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {"uni"});
    ViewBuilder builder(*engine, content.mods);
    EventSink sink;
    const auto wrap = [&](const std::string& type, const json& payload) {
        return *builder.Wrap(json{{"type", type}, {"payload", payload}},
                             Viewer::Player("player1"), sink);
    };
    const json roll = json{{"spec", json{{"sides", 6}}},
                           {"outcomes", json::array({3})},
                           {"counter", 311}};
    const json pick = json{{"table", "uni:t"},
                           {"key", "uni:k"},
                           {"value", 1},
                           {"counter", 977}};

    const json new_roll = wrap("outcome_rolled", roll);
    const json new_pick = wrap("outcome_rolled", pick);

    CHECK(new_roll["payload"]["roll_ordinal"] == 1);
    CHECK(new_pick["payload"]["roll_ordinal"] == 2);
    CHECK(new_pick["payload"]["table"] == "uni:t");
    CHECK(new_pick["payload"]["value"] == 1);
    for (const json& packet : {new_roll, new_pick}) {
        const std::string text = packet.dump();
        CHECK(text.find("311") == std::string::npos);
        CHECK(text.find("977") == std::string::npos);
        CHECK_FALSE(packet["payload"].contains("counter"));
    }
}

TEST_CASE("status: status_modified hides a hidden status from other seats") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {"uni"});
    LoadedMod stub;
    stub.manifest.id = "testmod";
    stub.manifest.version = "1.0.0";
    StatusDef secret;
    secret.id = "secret";
    secret.namespace_id = "testmod";
    secret.status_id = "testmod:secret";
    secret.hidden = true;
    stub.statuses.push_back(secret);
    std::vector<LoadedMod> mods = content.mods;
    mods.push_back(stub);
    ViewBuilder builder(*engine, mods);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const json modified =
        json{{"type", "status_modified"},
             {"payload", json{{"target", EntityRef(player0)},
                              {"status_kind", "testmod:secret"},
                              {"magnitude", 5},
                              {"delta", 3},
                              {"instance", 2}}}};
    EventSink sink;
    const std::optional<json> owner =
        builder.Wrap(modified, Viewer::Player("player0"), sink);
    REQUIRE(owner.has_value());
    CHECK((*owner)["payload"]["delta"] == 3);
    CHECK((*owner)["payload"]["instance_id"] == 2);
    CHECK_FALSE(builder.Wrap(modified, Viewer::Player("player1"), sink)
                    .has_value());
    CHECK_FALSE(
        builder.Wrap(modified, Viewer::PublicSpectator(), sink).has_value());
}

TEST_CASE("visibility: identities reach the grantee only, under both names") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {"uni"});
    ViewBuilder builder(*engine, content.mods);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity card = engine->Registries().cards[0];
    for (const std::string type :
         {"visibility_granted", "visibility_revoked"}) {
        CAPTURE(type);
        const json event =
            json{{"type", type},
                 {"payload", json{{"viewer", EntityRef(player0)},
                                  {"target", EntityRef(card)},
                                  {"aspects", json::array({"identity"})}}}};
        EventSink sink;
        CHECK(builder.Wrap(event, Viewer::Player("player0"), sink)
                  .has_value());
        for (const Viewer& other :
             {Viewer::Player("player1"), Viewer::Player("player2"),
              Viewer::PublicSpectator()}) {
            CHECK_FALSE(builder.Wrap(event, other, sink).has_value());
        }
    }
}

TEST_CASE("play_rejected: the target seat gets target and reason only") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {"uni"});
    ViewBuilder builder(*engine, content.mods);
    const json rejected =
        json{{"type", "play_rejected"},
             {"payload", json{{"player", "player0"},
                              {"reason_id", "unplayable"}}}};
    EventSink sink;
    const std::optional<json> seen =
        builder.Wrap(rejected, Viewer::Player("player0"), sink);
    REQUIRE(seen.has_value());
    CHECK((*seen)["payload"]["target"] == "player0");
    CHECK((*seen)["payload"]["reason_id"] == "unplayable");
    CHECK_FALSE(builder.Wrap(rejected, Viewer::Player("player1"), sink)
                    .has_value());
    CHECK_FALSE(builder.Wrap(rejected, Viewer::PublicSpectator(), sink)
                    .has_value());
}

TEST_CASE("finish: placement_set and match_ended happen one time") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {"uni"});
    const std::vector<ecs::Entity> wilds = [&] {
        std::vector<ecs::Entity> found;
        for (ecs::Entity card : engine->Registries().cards) {
            const ecs::CardIdentity* identity =
                engine->Store().Get<ecs::CardIdentity>(card);
            if (identity != nullptr && identity->kind_id == "uni:wild") {
                found.push_back(card);
            }
        }
        return found;
    }();
    REQUIRE_FALSE(wilds.empty());
    ForceHand(*engine, *engine->FindPlayer("player0"), {wilds[0]});
    REQUIRE(engine->PlayCard("player0", wilds[0]));
    REQUIRE(engine->SubmitInput("player0", "red"));
    REQUIRE(engine->IsMatchOver());

    for (const char* name : {"placement_set", "match_ended"}) {
        CAPTURE(name);
        CHECK(EventsOfType(*engine, name).size() == 1);
    }

    ViewBuilder builder(*engine, content.mods);
    for (const Viewer& viewer :
         {Viewer::Player("player1"), Viewer::PublicSpectator()}) {
        EventSink sink;
        const std::optional<json> placement = builder.Wrap(
            EventsOfType(*engine, "placement_set")[0], viewer, sink);
        REQUIRE(placement.has_value());
        CHECK((*placement)["payload"]["target"] == "player0");
        CHECK((*placement)["payload"]["place"] == 1);
        CHECK((*placement)["payload"]["is_elimination"] == false);
        const std::optional<json> ended = builder.Wrap(
            EventsOfType(*engine, "match_ended")[0], viewer, sink);
        REQUIRE(ended.has_value());
        CHECK((*ended)["payload"]["target"] == "player0");
        CHECK((*ended)["payload"]["targets"] == json::array({"player0"}));
    }
}

TEST_CASE("diagnostics: no seat and no spectator receives them") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {"uni"});
    ViewBuilder builder(*engine, content.mods);
    const std::vector<json> events = {
        json{{"type", "chain_aborted"}, {"mod", "secret_mod"}, {"node", "n"}},
        json{{"type", "mod_disarmed"}, {"mod", "secret_mod"}}};
    for (const json& event : events) {
        CAPTURE(event["type"]);
        for (const std::string& type : {event["type"].get<std::string>()}) {
            const match::view::EventRule* rule =
                match::view::FindEventRule(type);
            REQUIRE(rule != nullptr);
            CHECK(rule->fields.empty());
        }
        std::vector<Viewer> viewers = {Viewer::PublicSpectator(),
                                       Viewer::Spectator()};
        for (int seat = 0; seat < kPlayerCount; ++seat) {
            viewers.push_back(Viewer::Player("player" + std::to_string(seat)));
        }
        for (const Viewer& viewer : viewers) {
            EventSink sink;
            CHECK_FALSE(builder.Wrap(event, viewer, sink).has_value());
            CHECK(sink.NextSeq() == 0u);
        }
    }
}
