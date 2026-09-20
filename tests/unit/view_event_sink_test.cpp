#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/event_sink.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file view_event_sink_test.cpp
 * @brief `EventSink` + public-event projection tests.
 *
 * Covers the monotonic `seq`, the `{seq, type, payload}` envelope, and the
 * `all`-visibility public payload shapes built from engine descriptors and
 * live state. Per-recipient filtering is the view layer and is not exercised
 * here.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;
using nlohmann::json;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

fs::path ProjectRoot() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (fs::is_directory(p / "contract" / "schemas", ec)) return p;
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
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
            if (deck.deck_id == "vanilla:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content,
                                          std::size_t players,
                                          uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (std::size_t i = 0; i < players; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
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

std::vector<ecs::Entity> CardsByKind(const MatchInstance& engine,
                                     const std::string& kind) {
    std::vector<ecs::Entity> out;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind) {
            out.push_back(card);
        }
    }
    return out;
}

void ForceHand(MatchInstance& engine, ecs::Entity player,
               const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    // INFO: copy first; MoveCardToZone mutates hand->cards while iterating.
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kDrawPile,
                                         ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

}  // namespace

TEST_CASE("view event sink: seq is monotonic and never reused") {
    match::view::EventSink sink;

    const json first = sink.Wrap("a", json::object());
    const json second = sink.Wrap("b", json::object());
    const json third = sink.Wrap("c", json::object());

    CHECK(first["seq"] == 0u);
    CHECK(second["seq"] == 1u);
    CHECK(third["seq"] == 2u);
    CHECK(first["type"] == "a");
    CHECK(second["payload"].is_object());
    CHECK(sink.NextSeq() == 3u);

    // INFO: a filtered-out event is simply never wrapped; the next emitted
    //       packet keeps the unbroken sequence (drop but never renumber).
    sink.Reset();
    CHECK(sink.Wrap("a", json::object())["seq"] == 0u);
    CHECK(sink.Wrap("b", json::object())["seq"] == 1u);
    CHECK(sink.NextSeq() == 2u);
}

TEST_CASE("view event sink: public projections match the 14.2 shapes") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    REQUIRE(engine != nullptr);

    const ecs::Entity card = engine->Registries().cards.front();
    const std::optional<ecs::CompactCardV2> compact =
        engine->Registries().CardId(card);
    REQUIRE(compact.has_value());

    const std::optional<json> played = match::view::ProjectPublicEvent(
        "card_played",
        json{{"player", "player0"}, {"card", EntityRef(card)},
             {"from_ordinal", 3}},
        *engine);
    REQUIRE(played.has_value());
    CHECK((*played)["player"] == "player0");
    CHECK((*played)["card"] == compact->bits);
    CHECK((*played)["from_zone_ordinal"] == 3);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::optional<json> turn = match::view::ProjectPublicEvent(
        "turn_advance",
        json{{"from", EntityRef(player0)}, {"to", EntityRef(player1)},
             {"direction", 1}, {"deadline", 1234}},
        *engine);
    REQUIRE(turn.has_value());
    CHECK((*turn)["from"] == "player0");
    CHECK((*turn)["to"] == "player1");
    CHECK((*turn)["direction"] == 1);
    CHECK((*turn)["deadline_ms"] == 1234);

    const std::optional<json> reshuffle = match::view::ProjectPublicEvent(
        "reshuffle", json{{"draw_size", 8}, {"discard_size", 1}}, *engine);
    REQUIRE(reshuffle.has_value());
    CHECK((*reshuffle)["draw_size"] == 8);
    CHECK((*reshuffle)["discard_size"] == 1);

    const std::optional<json> round = match::view::ProjectPublicEvent(
        "round_advance", json{{"round", 2}}, *engine);
    REQUIRE(round.has_value());
    CHECK((*round)["round"] == 2);

    const std::optional<json> placement = match::view::ProjectPublicEvent(
        "placement", json{{"player", "player0"}, {"place", 1}}, *engine);
    REQUIRE(placement.has_value());
    CHECK((*placement)["player"] == "player0");
    CHECK((*placement)["place"] == 1);

    const std::optional<json> roll = match::view::ProjectPublicEvent(
        "roll_result",
        json{{"spec", json{{"sides", 6}}}, {"outcomes", json::array({3, 5})},
             {"counter", 7}},
        *engine);
    REQUIRE(roll.has_value());
    CHECK((*roll)["spec"].get<std::string>().find("sides")
          != std::string::npos);
    CHECK((*roll)["outcomes"].size() == 2);
    CHECK((*roll)["roll_counter"] == 7);

    const std::optional<json> signal = match::view::ProjectPublicEvent(
        "signal", json{{"name", "explosion"}, {"payload", json{{"x", 1}}}},
        *engine);
    REQUIRE(signal.has_value());
    CHECK((*signal)["name"] == "explosion");
    CHECK((*signal)["payload"]["x"] == 1);

    // INFO: unknown / the view layer types are dropped without consuming a seq.
    CHECK_FALSE(match::view::ProjectPublicEvent(
                    "card_left_zone", json::object(), *engine)
                    .has_value());
}

TEST_CASE("view event sink: flat abort descriptors wrap correctly") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    match::view::EventSink sink;
    const std::optional<json> aborted = sink.WrapPublic(
        json{{"type", "chain_aborted"}, {"mod", "m"}, {"node", "n"}},
        *engine);
    REQUIRE(aborted.has_value());
    CHECK((*aborted)["seq"] == 0u);
    CHECK((*aborted)["payload"]["mod"] == "m");
    CHECK((*aborted)["payload"]["node"] == "n");
    REQUIRE((*aborted)["payload"]["reason"].is_string());

    const std::optional<json> disarmed = sink.WrapPublic(
        json{{"type", "mod_disarmed"}, {"mod", "m"}}, *engine);
    REQUIRE(disarmed.has_value());
    CHECK((*disarmed)["seq"] == 1u);
    CHECK((*disarmed)["payload"]["mod_id"] == "m");

    // INFO: an event that is not a public packet is dropped and does not
    //       consume a seq, so the stream stays gap-free.
    CHECK_FALSE(
        sink.WrapPublic(json{{"type", "card_left_zone"}}, *engine)
            .has_value());
    CHECK(sink.NextSeq() == 2u);
}

TEST_CASE("view event sink: a played card and turn wrap gap-free") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    REQUIRE(wilds.size() >= 2);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ForceHand(*engine, player0, {wilds[0], wilds[1]});

    REQUIRE(engine->PlayCard("player0", wilds[0]));
    REQUIRE(engine->PendingInput().has_value());
    REQUIRE(engine->SubmitInput("player0", "red"));

    match::view::EventSink sink;
    std::vector<json> envelopes;
    for (const json& event : engine->Events()) {
        std::optional<json> wrapped = sink.WrapPublic(event, *engine);
        if (wrapped.has_value()) envelopes.push_back(*wrapped);
    }

    REQUIRE_FALSE(envelopes.empty());
    for (std::size_t i = 0; i < envelopes.size(); ++i) {
        CHECK(envelopes[i]["seq"] == static_cast<int>(i));
    }
    bool saw_played = false;
    bool saw_turn = false;
    for (const json& envelope : envelopes) {
        if (envelope["type"] == "card_played") {
            saw_played = true;
            CHECK(envelope["payload"]["player"] == "player0");
            CHECK(envelope["payload"]["card"].is_number_unsigned());
        }
        if (envelope["type"] == "turn_advance") {
            saw_turn = true;
            CHECK(envelope["payload"]["to"] == "player1");
        }
    }
    CHECK(saw_played);
    CHECK(saw_turn);
}

TEST_CASE("view event sink: match_end carries winner and placements") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    const std::vector<ecs::Entity> wilds =
        CardsByKind(*engine, "vanilla:wild");
    REQUIRE_FALSE(wilds.empty());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ForceHand(*engine, player0, {wilds[0]});

    REQUIRE(engine->PlayCard("player0", wilds[0]));
    REQUIRE(engine->SubmitInput("player0", "red"));
    REQUIRE(engine->IsMatchOver());

    const std::optional<json> end = match::view::ProjectPublicEvent(
        "match_end", json::object(), *engine);
    REQUIRE(end.has_value());
    CHECK((*end)["winner"] == "player0");
    REQUIRE((*end)["placements"].size() == 1);
    CHECK((*end)["placements"][0]["player"] == "player0");
    CHECK((*end)["placements"][0]["place"] == 1);
    CHECK((*end)["final_digest"].get<std::string>().size() == 16);
}
