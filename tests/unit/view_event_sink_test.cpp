#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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

/**
 * @brief `n` numbered cards matching the match's active colour.
 *
 * A numbered card has no prompt-opening trigger, so it is a legal play and
 * never parks input (mirrors the engine-hooks test helper).
 */
std::vector<ecs::Entity> LegalNumbered(MatchInstance& engine, std::size_t n) {
    std::vector<ecs::Entity> out;
    const json active = engine.ExportState()["active_type"];
    if (!active.is_string()) return out;
    const std::string color = active.get<std::string>();
    for (ecs::Entity card : engine.Registries().cards) {
        if (out.size() == n) break;
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face == nullptr || face->color != color) continue;
        if (face->label.size() != 1) continue;
        if (std::isdigit(static_cast<unsigned char>(face->label[0])) == 0) {
            continue;
        }
        const ecs::CardBehavior* behavior =
            engine.Store().Get<ecs::CardBehavior>(card);
        if (behavior != nullptr && !behavior->triggers.empty()) continue;
        out.push_back(card);
    }
    return out;
}

/** @brief A loaded-mod stub declaring signal audiences. */
LoadedMod SignalMod() {
    LoadedMod mod;
    mod.manifest.id = "signals";
    mod.manifest.version = "1.0.0";
    mod.manifest.signals = json::array(
        {json{{"name", "anim_all"}},
         json{{"name", "anim_players"}, {"audience", "players"}},
         json{{"name", "anim_spec"}, {"audience", "spectators"}}});
    return mod;
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
             {"direction", 1}, {"deadline", 1234},
             {"skipped", json::array({EntityRef(player1)})}},
        *engine);
    REQUIRE(turn.has_value());
    CHECK((*turn)["from"] == "player0");
    CHECK((*turn)["to"] == "player1");
    CHECK((*turn)["direction"] == 1);
    CHECK((*turn)["deadline_ms"] == 1234);
    // INFO: skipped entity handles resolve to usernames for the client X mark.
    REQUIRE((*turn)["skipped"].is_array());
    REQUIRE((*turn)["skipped"].size() == 1);
    CHECK((*turn)["skipped"][0] == "player1");

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

    // INFO: `signal` is NOT an `all`-visibility row: its audience is
    //       declared per mod manifest and resolved by `ViewBuilder` per
    //       recipient, so the public projector must drop it (review fix 2).
    CHECK_FALSE(match::view::ProjectPublicEvent(
                    "signal",
                    json{{"name", "explosion"}, {"payload", json{{"x", 1}}}},
                    *engine)
                    .has_value());

    // INFO: unknown / the view layer types are dropped without consuming a seq.
    CHECK_FALSE(match::view::ProjectPublicEvent(
                    "card_left_zone", json::object(), *engine)
                    .has_value());
}

// NOTE: patchwork. Covers the server-authored hand-movement events.
TEST_CASE("view event sink: hand movement events carry public data only") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);
    REQUIRE(engine != nullptr);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");

    const std::optional<json> swapped = match::view::ProjectPublicEvent(
        "hands_swapped",
        json{{"a", EntityRef(player0)}, {"b", EntityRef(player1)},
             {"a_size", 5}, {"b_size", 2}},
        *engine);
    REQUIRE(swapped.has_value());
    CHECK((*swapped)["a"] == "player0");
    CHECK((*swapped)["b"] == "player1");
    CHECK((*swapped)["a_size"] == 5);
    CHECK((*swapped)["b_size"] == 2);
    CHECK((*swapped).size() == 4);

    const std::optional<json> passed = match::view::ProjectPublicEvent(
        "hands_passed",
        json{{"direction", "forward"},
             {"players", json::array({EntityRef(player0), EntityRef(player1),
                                      EntityRef(player2)})},
             {"hand_sizes", json::array({7, 3, 4})}},
        *engine);
    REQUIRE(passed.has_value());
    CHECK((*passed)["direction"] == "forward");
    REQUIRE((*passed)["players"].size() == 3);
    CHECK((*passed)["players"][0] == "player0");
    CHECK((*passed)["players"][2] == "player2");
    CHECK((*passed)["hand_sizes"] == json::array({7, 3, 4}));
    CHECK((*passed).size() == 3);
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

TEST_CASE("view event sink: signal audience routes through the view builder") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    std::vector<LoadedMod> mods = content.mods;
    mods.push_back(SignalMod());
    match::view::ViewBuilder builder(*engine, mods);

    auto signal = [](const std::string& name) {
        return json{{"type", "signal"},
                    {"payload", json{{"name", name},
                                     {"payload", json{{"x", 1}}}}}};
    };
    auto visible = [&](const match::view::Viewer& viewer,
                       const std::string& name) {
        match::view::EventSink sink;
        return builder.Wrap(signal(name), viewer, sink).has_value();
    };

    // INFO: an undeclared audience defaults to `all`.
    CHECK(builder.SignalAudience("undeclared") == "all");
    CHECK(visible(match::view::Viewer::Player("player1"), "undeclared"));
    CHECK(visible(match::view::Viewer::Spectator(), "undeclared"));
    // INFO: audience `players` excludes spectators.
    CHECK(visible(match::view::Viewer::Player("player1"), "anim_players"));
    CHECK_FALSE(visible(match::view::Viewer::Spectator(), "anim_players"));
    // INFO: audience `spectators` excludes seated players.
    CHECK(visible(match::view::Viewer::Spectator(), "anim_spec"));
    CHECK_FALSE(visible(match::view::Viewer::Player("player1"), "anim_spec"));
    // INFO: audience `all` reaches both.
    CHECK(visible(match::view::Viewer::Player("player1"), "anim_all"));
    CHECK(visible(match::view::Viewer::Spectator(), "anim_all"));
}

TEST_CASE("view event sink: real auto-play card_played resolves its player") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 42);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 3);
    REQUIRE(cards.size() == 3);
    ForceHand(*engine, player0, cards);

    ecs::AutoTrigger trigger;
    trigger.condition = json(true);
    trigger.graph = json{{"nodes", json::array()}};
    trigger.must_apply = true;
    REQUIRE(engine->Store().Add(cards[2], std::move(trigger)) != nullptr);

    REQUIRE(engine->PlayCard("player0", cards[0]));

    // INFO: `AutoPlayCard` emits `player` as an entity object, unlike
    //       `PlayCard`'s username string; gather those real descriptors.
    std::optional<json> auto_card_event;
    std::optional<json> auto_played_event;
    for (const json& event : engine->Events()) {
        if (!event.is_object() || !event.contains("payload")) continue;
        const json& payload = event["payload"];
        if (!payload.is_object() || !payload.contains("player")) continue;
        if (!payload["player"].is_object()) continue;
        const std::string type = event.value("type", std::string());
        if (type == "card_played") auto_card_event = event;
        if (type == "auto_played") auto_played_event = event;
    }
    REQUIRE(auto_card_event.has_value());
    REQUIRE(auto_played_event.has_value());

    match::view::EventSink sink;
    std::optional<json> card_played;
    CHECK_NOTHROW(card_played = sink.WrapPublic(*auto_card_event, *engine));
    REQUIRE(card_played.has_value());
    CHECK((*card_played)["payload"]["player"] == "player0");

    std::optional<json> auto_played;
    CHECK_NOTHROW(auto_played = sink.WrapPublic(*auto_played_event, *engine));
    REQUIRE(auto_played.has_value());
    CHECK((*auto_played)["payload"]["player"] == "player0");
}
