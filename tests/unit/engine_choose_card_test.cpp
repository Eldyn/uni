#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/match_session.hpp>

#include "support/fake_broadcaster.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_choose_card_test.cpp
 * @brief Acceptance: `choose_card` speaks `CompactCardV2.bits`.
 *
 * Builds in-memory (no filesystem) mod fixtures with a synthetic `modx` card
 * kind, assembles through the real `MatchAssembler`, opens a `transfer_card`
 * `chosen` prompt through a real hook, then asserts:
 * - each option is an integer equal to that candidate's `CompactCardV2.bits`;
 * - the envelope carries `response_schema {"type":"integer"}`;
 * - submitting a chosen candidate's bits resumes the graph with that exact
 *   card entity bound as the `from_prompt` value (server maps what it emits).
 *
 * The synthetic mod kind covers the "same-class" guard for a non-vanilla
 * kind; the `modx` mod is second in the deck's mod list so its cards carry a
 * non-zero `mod_index`, not the vanilla zero.
 */

namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

ModManifest Manifest(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

/** @brief A numbered text card with no behaviors (candidate material). */
CardDef PlainCard(const std::string& ns, const std::string& local,
                  const std::string& label) {
    CardDef card;
    card.id = local;
    card.namespace_id = ns;
    card.kind_id = ns + ":" + local;
    card.title = local;
    card.face.kind = match::modload::FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = label;
    card.tags = {"numbered"};
    return card;
}

DeckDef FixtureDeck(const std::vector<std::string>& mods,
                    const std::vector<std::pair<std::string, int>>& cards) {
    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = mods;
    deck.cards = cards;
    return deck;
}

MatchAssemblyOptions Players(int count, int starting_cards, uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = starting_cards;
    options.seed = seed;
    for (int i = 0; i < count; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    return options;
}

std::unique_ptr<MatchInstance> Assemble(const std::vector<LoadedMod>& mods,
                                        const DeckDef& deck, int players = 2,
                                        int cards = 6, uint64_t seed = 11) {
    AssemblyResult result = MatchAssembler::Assemble(
        mods, deck, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

/** @brief Replace a player's hand with exactly `cards` (test setup). */
void ForceHand(MatchInstance& engine, ecs::Entity player,
               const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
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

/** @brief First assembled card of `kind_id`, or nullopt. */
std::optional<ecs::Entity> FindKindCard(MatchInstance& engine,
                                        const std::string& kind_id) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) return card;
    }
    return std::nullopt;
}

const json* FindEvent(const MatchInstance& engine, const std::string& type) {
    for (const json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) return &event;
    }
    return nullptr;
}

/** @brief A graph node that opens the `chosen` transfer prompt. */
json ChooseCardNode(const std::string& id, const std::string& next) {
    return json{{"id", id},
                {"op", "transfer_card"},
                {"args", json{{"selector", "chosen"},
                              {"from_player", "@self"},
                              {"to_player", "@next_player"}}},
                {"next", next}};
}

/**
 * @brief Test-only op: read a bound `from_prompt` value and emit it.
 *
 * Stands in for the card-consuming op a mod would place after the prompt; it
 * proves the resume path bound the mapped entity, not the raw integer.
 */
ops::OpResult OpReadPromptValue(ecs::EntityStore&, const ops::OpArgs& args,
                                ops::OpContext& ctx) {
    std::string node;
    if (const json* raw = args.Find("from_prompt"); raw != nullptr
        && raw->is_string()) {
        node = raw->get<std::string>();
    }
    const json* value =
        node.empty() ? nullptr : ctx.frame.FindPromptValue(node);
    json payload = json{{"node", node},
                        {"value", value == nullptr ? json(nullptr) : *value}};
    ops::OpResult result = ops::OpResult::Resolved(payload);
    result.events.push_back(ops::MakeEvent("signal", payload));
    return result;
}

/** @brief Attach a `choose_card` -> `read_prompt` graph on `after:draw`. */
void AttachChooseCardSystem(MatchInstance& engine, BehaviorGraph graph) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"draw", ecs::HookPhase::kAfter};
    system.source_id = "test:choose_card";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    MatchAssembly* self = &engine.Assembly();
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [self, index](ecs::HookPayload& payload) {
            self->RunSystem(index, payload);
        });
}

/** @brief Real engine with a `base` pad mod plus a synthetic `modx` card. */
struct Fixture {
    std::vector<LoadedMod> mods;
    DeckDef deck;
};

Fixture MakeFixture() {
    Fixture fixture;
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(PlainCard("base", "pad", "9"));
    LoadedMod modx;
    modx.manifest = Manifest("modx");
    modx.cards.push_back(PlainCard("modx", "probe", "1"));
    fixture.mods = {std::move(base), std::move(modx)};
    fixture.deck = FixtureDeck(
        {"base", "modx"}, {{"base:pad", 10}, {"modx:probe", 10}});
    return fixture;
}

/** @brief An opaque test socket key; never dereferenced by the fake. */
AppWebSocket* PlayerSocket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x100 + index));
}

/**
 * @brief `response_schema.type` of the first `prompt_open` sent to `socket`.
 *
 * Returned by value (the parsed packet is local), empty when none was sent.
 */
std::string SessionPromptSchemaType(const FakeBroadcaster& fake,
                                    AppWebSocket* socket) {
    for (const SentFrame& frame : fake.sent) {
        if (frame.to != socket) continue;
        const json packet = json::parse(frame.payload);
        if (packet.value("action", std::string()) != "match_event") continue;
        if (packet.value("type", std::string()) != "prompt_open") continue;
        const json payload = packet.value("payload", json::object());
        const json schema = payload.value("response_schema", json::object());
        return schema.value("type", std::string());
    }
    return std::string();
}

}  // namespace

TEST_CASE("choose_card: options are bits and the answer maps back to a card") {
    Fixture fixture = MakeFixture();
    std::unique_ptr<MatchInstance> engine =
        Assemble(fixture.mods, fixture.deck);
    engine->Assembly().runtime.Register("test_read_prompt",
                                        &OpReadPromptValue);

    BehaviorGraph graph;
    graph.nodes = json::array(
        {ChooseCardNode("n1", "n2"),
         json{{"id", "n2"},
              {"op", "test_read_prompt"},
              {"args", json{{"from_prompt", "n1"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    AttachChooseCardSystem(*engine, std::move(graph));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> probe =
        FindKindCard(*engine, "modx:probe");
    REQUIRE(probe.has_value());
    ForceHand(*engine, player0, {*probe});

    // INFO: the synthetic mod kind sits at a non-zero mod index.
    const std::optional<ecs::CompactCardV2> probe_id =
        engine->Registries().CardId(*probe);
    REQUIRE(probe_id.has_value());
    CHECK(probe_id->ModIndex() == 1);

    REQUIRE(engine->DrawCard("player0"));

    const std::optional<json> pending = engine->PendingInput();
    REQUIRE(pending.has_value());
    CHECK((*pending)["kind"] == "choose_card");
    const json& payload = (*pending)["payload"];
    REQUIRE(payload["options"].is_array());
    REQUIRE(payload["options"].size() == 2);
    CHECK(payload["response_schema"]["type"] == "integer");
    // INFO: every option is exactly the candidate's frozen bits.
    for (const json& option : payload["options"]) {
        REQUIRE(option.is_number_integer());
        const ecs::CompactCardV2 id{option.get<uint32_t>()};
        CHECK(engine->Registries().CardEntity(id).has_value());
    }
    CHECK(payload["options"][0] == probe_id->bits);

    // INFO: answer with the candidate the prompt offered - the resumed graph
    //       must see that exact card entity, not the wire integer.
    const uint32_t answer = payload["options"][0].get<uint32_t>();
    REQUIRE(engine->SubmitInput("player1", answer));
    CHECK_FALSE(engine->PendingInput().has_value());

    const json* signal = FindEvent(*engine, "signal");
    REQUIRE(signal != nullptr);
    const json& bound = (*signal)["payload"]["value"];
    REQUIRE(bound.is_object());
    CHECK(bound["index"] == probe->index);
    CHECK(bound["generation"] == probe->generation);
}

TEST_CASE("choose_card: unknown bits are rejected and the prompt stays open") {
    Fixture fixture = MakeFixture();
    std::unique_ptr<MatchInstance> engine =
        Assemble(fixture.mods, fixture.deck);

    BehaviorGraph graph;
    graph.nodes = json::array(
        {ChooseCardNode("n1", "n2"),
         json{{"id", "n2"}, {"op", "emit_signal"}, {"args", {{"name", "x"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    AttachChooseCardSystem(*engine, std::move(graph));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> probe =
        FindKindCard(*engine, "modx:probe");
    REQUIRE(probe.has_value());
    ForceHand(*engine, player0, {*probe});

    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingInput().has_value());

    // INFO: I2 defence in depth - a non-integer spoof (which a mod-overridden
    //       schema could invite) is rejected without disturbing the pause.
    CHECK_FALSE(engine->SubmitInput("player1", json("string-answer")));
    REQUIRE(engine->PendingInput().has_value());
    CHECK((*engine->PendingInput())["kind"] == "choose_card");

    // INFO: negative and unknown-bit integers are unmappable and rejected too.
    CHECK_FALSE(engine->SubmitInput("player1", -1));
    CHECK_FALSE(engine->SubmitInput("player1", 0x7FFFFFFFu));
    REQUIRE(engine->PendingInput().has_value());
    CHECK((*engine->PendingInput())["kind"] == "choose_card");

    // INFO: the parked prompt is still answerable with a valid integer.
    const json options = (*engine->PendingInput())["payload"]["options"];
    REQUIRE(options.is_array());
    REQUIRE(!options.empty());
    REQUIRE(engine->SubmitInput("player1", options[0].get<uint32_t>()));
    CHECK_FALSE(engine->PendingInput().has_value());
}

TEST_CASE("choose_card: a mod cannot override the built-in prompt schema") {
    Fixture fixture = MakeFixture();
    // INFO: the mod tries to replace the engine's choose_card validator with a
    //       string schema; the built-in integer schema must win.
    fixture.mods.back().manifest.prompts = json::array(
        {json{{"kind", "choose_card"},
              {"response_schema", json{{"type", "string"}}}}});
    std::unique_ptr<MatchInstance> engine =
        Assemble(fixture.mods, fixture.deck);

    BehaviorGraph graph;
    graph.nodes = json::array(
        {ChooseCardNode("n1", "n2"),
         json{{"id", "n2"}, {"op", "emit_signal"}, {"args", {{"name", "x"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    AttachChooseCardSystem(*engine, std::move(graph));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> probe =
        FindKindCard(*engine, "modx:probe");
    REQUIRE(probe.has_value());
    ForceHand(*engine, player0, {*probe});
    const uint32_t probe_bits = engine->Registries().CardId(*probe)->bits;

    AppWebSocket* s0 = PlayerSocket(0);
    AppWebSocket* s1 = PlayerSocket(1);
    std::vector<LoadedMod> mods = fixture.mods;
    match::server::MatchSession session(
        std::move(engine), std::move(mods),
        match::server::MatchSession::SocketMap{
            {"player0", s0}, {"player1", s1}});
    FakeBroadcaster fake;

    REQUIRE(session.DrawCard("player0"));
    session.EmitEvents(fake);

    // INFO: the emitted prompt (targeted at the chooser) advertises the
    //       built-in integer schema.
    CHECK(SessionPromptSchemaType(fake, s1) == "integer");

    // INFO: the mod's string schema is ignored, so a string answer fails the
    //       built-in validation and the prompt stays parked.
    CHECK_FALSE(session.SubmitInput("player1", "choose_card", json("spoof")));

    // INFO: an integer answer is accepted and resolves the prompt.
    CHECK(session.SubmitInput("player1", "choose_card", json(probe_bits)));
    fake.Clear();
    session.EmitEvents(fake);
    bool closed = false;
    for (const SentFrame& frame : fake.sent) {
        if (frame.to != s1) continue;
        const json packet = json::parse(frame.payload);
        if (packet.value("type", std::string()) == "prompt_close") closed = true;
    }
    CHECK(closed);
}
