#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_auto_trigger_test.cpp
 * @brief Must-apply auto-trigger acceptance tests.
 *
 * A synthetic in-memory mod declares a `must_apply: true` card whose condition
 * is always true at the trigger point. The tests prove the assembler attaches
 * `ecs::AutoTrigger` from the def, that `RunMustApply("play")` auto-plays the
 * card (emitting `card_played` + `auto_played` and applying its graph), and
 * that the intra-pass `played` guard stops it firing twice.
 */

namespace fs = std::filesystem;
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
            if (deck.deck_id == "uni:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

/**
 * @brief Synthetic behavior graphs for the in-memory auto-trigger card.
 *
 * Chosen over a tests-fixture folder because the engine tests already build
 * synthetic `LoadedMod`s in memory (see engine_forced_play_test.cpp); it keeps
 * the card out of `mods/uni/` and therefore out of the deck + golden.
 */
json SignalNodes(const std::string& signal) {
    return json::array({json{{"id", "n1"},
                             {"op", "emit_signal"},
                             {"args", json{{"name", signal}}}}});
}

/**
 * @brief In-memory mod with one auto-trigger card.
 *
 * `nodes` is the card's behavior graph; `must_apply` decides whether the
 * must-apply scanner is allowed to fire it without player agency.
 */
LoadedMod AutoTriggerMod(const json& nodes = SignalNodes("wp9x_auto_fired"),
                         bool must_apply = true) {
    AutoTriggerDef auto_trigger;
    // INFO: `always` matches for the card wherever it is; at the trigger point
    //       it is in player0's hand, so the must-apply scanner fires it.
    auto_trigger.condition = json{{"always", json::object()}};
    auto_trigger.graph = json{{"nodes", nodes}};
    auto_trigger.must_apply = must_apply;

    CardDef card;
    card.id = "auto";
    card.namespace_id = "wp9x_auto";
    card.kind_id = "wp9x_auto:auto";
    card.title = "auto card";
    card.face.kind = match::modload::FaceKind::kText;
    card.face.color = "red";
    card.face.label = "A";
    card.tags = {"wp9x_auto"};
    card.auto_trigger = auto_trigger;
    card.raw = json{{"id", "auto"}};

    LoadedMod mod;
    mod.folder = "wp9x_auto";
    mod.manifest.id = "wp9x_auto";
    mod.manifest.name = "auto-trigger test";
    mod.manifest.version = "1.0.0";
    mod.manifest.api = "1";
    mod.cards.push_back(std::move(card));
    return mod;
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

MatchAssemblyOptions TestPlayers(int cards, uint64_t seed) {
    return Players(2, cards, seed);
}

DeckDef AutoDeck(const Content& content) {
    DeckDef deck = content.classic;
    deck.mods = {"uni", "wp9x_auto"};
    deck.cards.push_back({"wp9x_auto:auto", 1});
    return deck;
}

AssemblyResult AssembleAuto(const Content& content, int cards, uint64_t seed) {
    return MatchAssembler::Assemble(content.mods, AutoDeck(content),
                                    TestPlayers(cards, seed));
}

std::unique_ptr<MatchInstance> MakeAutoEngine(Content& content, int cards,
                                              uint64_t seed) {
    AssemblyResult result = AssembleAuto(content, cards, seed);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

/** @brief First live entity carrying the auto-trigger component. */
std::optional<ecs::Entity> FindAutoCard(MatchInstance& engine) {
    for (ecs::Entity card : engine.Store().EntitiesWith<ecs::AutoTrigger>()) {
        if (engine.Store().IsAlive(card)) return card;
    }
    return std::nullopt;
}

std::size_t CountEvents(const MatchInstance& engine,
                        const std::string& type) {
    std::size_t count = 0;
    for (const json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) ++count;
    }
    return count;
}

/** @brief True when `type` references `card` in its payload. */
bool HasCardEvent(const MatchInstance& engine, const std::string& type,
                  ecs::Entity card) {
    for (const json& event : engine.Events()) {
        if (!event.is_object() || event.value("type", "") != type) continue;
        if (!event.contains("payload") || !event["payload"].is_object()) {
            continue;
        }
        const json& payload = event["payload"];
        if (payload.contains("card") && payload["card"].is_object()
            && payload["card"].value("index", 0u) == card.index) {
            return true;
        }
    }
    return false;
}

bool HasSignal(const MatchInstance& engine, const std::string& name) {
    for (const json& event : engine.Events()) {
        if (!event.is_object() || event.value("type", "") != "signal") {
            continue;
        }
        const json payload =
            event.contains("payload") ? event["payload"] : json::object();
        if (payload.value("name", "") == name) return true;
    }
    return false;
}

/** @brief The first event of `type` whose payload references `card`. */
const json* CardEvent(const MatchInstance& engine, const std::string& type,
                      ecs::Entity card) {
    for (const json& event : engine.Events()) {
        if (!event.is_object() || event.value("type", "") != type) continue;
        if (!event.contains("payload") || !event["payload"].is_object()) {
            continue;
        }
        const json& payload = event["payload"];
        if (payload.contains("card") && payload["card"].is_object()
            && payload["card"].value("index", 0u) == card.index) {
            return &event;
        }
    }
    return nullptr;
}

/** @brief The first emitted event named `name`. */
const json* SignalEvent(const MatchInstance& engine, const std::string& name) {
    for (const json& event : engine.Events()) {
        if (!event.is_object() || event.value("type", "") != "signal") {
            continue;
        }
        const json payload =
            event.contains("payload") ? event["payload"] : json::object();
        if (payload.value("name", "") == name) return &event;
    }
    return nullptr;
}

/** @brief Emission position of `event`, or -1 when it was never emitted. */
int EventOrdinal(const MatchInstance& engine, const json& event) {
    const int64_t id = event.value("event_id", int64_t{-1});
    int ordinal = 0;
    for (const json& candidate : engine.Events()) {
        if (candidate.value("event_id", int64_t{-1}) == id) return ordinal;
        ++ordinal;
    }
    return -1;
}

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

/** @brief A numbered card matching the active colour (never opens a prompt). */
std::optional<ecs::Entity> LegalNumbered(
    MatchInstance& engine, const std::vector<ecs::Entity>& exclude = {}) {
    const json active = engine.ExportState()["active_type"];
    if (!active.is_string()) return std::nullopt;
    const std::string color = active.get<std::string>();

    const ecs::PileContents* discard = engine.Store().Get<ecs::PileContents>(
        engine.Registries().discard_pile);
    const std::optional<ecs::Entity> top =
        (discard == nullptr || discard->cards.empty())
            ? std::nullopt
            : std::optional<ecs::Entity>(discard->cards.back());
    for (ecs::Entity card : engine.Registries().cards) {
        if (top.has_value() && card == *top) continue;
        if (std::find(exclude.begin(), exclude.end(), card) != exclude.end()) {
            continue;
        }
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face == nullptr || face->color != color) continue;
        if (face->label.size() != 1) continue;
        if (std::isdigit(static_cast<unsigned char>(face->label[0])) == 0) {
            continue;
        }
        const ecs::CardBehavior* behavior =
            engine.Store().Get<ecs::CardBehavior>(card);
        if (behavior != nullptr && !behavior->triggers.empty()) continue;
        return card;
    }
    return std::nullopt;
}

/** @brief A graph that marks its owner and then emits `signal`. */
json MarkNodes(const std::string& signal) {
    return json::array(
        {json{{"id", "n1"},
              {"op", "apply_status"},
              {"args", json{{"target", "@self"},
                            {"status_kind", "wp9x_auto_marked"}}},
              {"next", "n2"}},
         json{{"id", "n2"},
              {"op", "emit_signal"},
              {"args", json{{"name", signal}}}}});
}

/**
 * @brief Attach a system that parks a prompt on the auto card's own event.
 *
 * An auto-trigger graph parks its prompt with the system slot it reserved, so
 * `SubmitInput` refuses to resume it (there is no subscribed system behind
 * that slot). The prompt here is opened by a system that reacts to the
 * `status_applied` event the auto card's graph emits, which is the shape a
 * mod reacting to an auto play takes and the one `SubmitInput` resumes.
 */
void AttachStatusPromptSystem(MatchInstance& engine,
                              const std::string& signal) {
    const json nodes = json::array(
        {json{{"id", "p1"},
              {"op", "prompt"},
              {"args", json{{"kind", "choose_color"},
                            {"target", "@target"},
                            {"payload",
                             json{{"options", json::array({"red", "blue"})}}}}},
              {"next", "p2"}},
         json{{"id", "p2"},
              {"op", "emit_signal"},
              {"args", json{{"name", signal}}}}});

    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "wp9x_prompt";
    system.registration_index = 0;
    system.hook = ecs::HookId{"status_applied", ecs::HookPhase::kAfter};
    system.source_id = "wp9x_prompt:status_applied";
    system.graph.nodes = nodes;
    system.graph.raw = json{{"nodes", nodes}};
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "wp9x_prompt", 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}

}  // namespace

TEST_CASE("engine auto_trigger: assembler attaches ecs::AutoTrigger") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(AutoTriggerMod());

    AssemblyResult result = AssembleAuto(content, 5, 42);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));

    std::vector<ecs::Entity> tagged;
    for (ecs::Entity card : result.assembly->store
                                .EntitiesWith<ecs::AutoTrigger>()) {
        tagged.push_back(card);
    }
    REQUIRE(tagged.size() == 1);

    const ecs::AutoTrigger* trigger =
        result.assembly->store.Get<ecs::AutoTrigger>(tagged[0]);
    REQUIRE(trigger != nullptr);
    CHECK(trigger->must_apply);
    CHECK(trigger->condition == json{{"always", json::object()}});
    REQUIRE(trigger->graph.is_object());
    REQUIRE(trigger->graph.contains("nodes"));
    CHECK(trigger->graph["nodes"].size() == 1);
    CHECK(trigger->graph["nodes"][0]["op"] == "emit_signal");

    const ecs::CardIdentity* identity =
        result.assembly->store.Get<ecs::CardIdentity>(tagged[0]);
    REQUIRE(identity != nullptr);
    CHECK(identity->kind_id == "wp9x_auto:auto");
}

TEST_CASE("engine auto_trigger: must-apply card auto-plays on play") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(AutoTriggerMod());

    std::unique_ptr<MatchInstance> engine = MakeAutoEngine(content, 5, 42);
    const std::optional<ecs::Entity> auto_card = FindAutoCard(*engine);
    REQUIRE(auto_card.has_value());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> numbered = LegalNumbered(*engine);
    REQUIRE(numbered.has_value());
    ForceHand(*engine, player0, {*numbered, *auto_card});

    REQUIRE(engine->PlayCard("player0", *numbered));

    // INFO: the must-apply scanner auto-played the card, emitting both the
    //       normal play event and the auto-play marker, and ran its graph.
    CHECK(HasCardEvent(*engine, "card_played", *auto_card));
    CHECK(CountEvents(*engine, "auto_played") == 1);
    CHECK(HasCardEvent(*engine, "auto_played", *auto_card));
    CHECK(HasSignal(*engine, "wp9x_auto_fired"));

    const ecs::InZone* zone = engine->Store().Get<ecs::InZone>(*auto_card);
    REQUIRE(zone != nullptr);
    CHECK(zone->zone.kind == ecs::ZoneKind::kDiscardPile);

    const ecs::Hand* hand = engine->Store().Get<ecs::Hand>(player0);
    REQUIRE(hand != nullptr);
    CHECK(std::find(hand->cards.begin(), hand->cards.end(), *auto_card)
          == hand->cards.end());
}

TEST_CASE("engine auto_trigger: played guard prevents a self re-trigger") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(AutoTriggerMod());

    std::unique_ptr<MatchInstance> engine = MakeAutoEngine(content, 5, 42);
    const std::optional<ecs::Entity> auto_card = FindAutoCard(*engine);
    REQUIRE(auto_card.has_value());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> numbered = LegalNumbered(*engine);
    REQUIRE(numbered.has_value());
    ForceHand(*engine, player0, {*numbered, *auto_card});

    REQUIRE(engine->PlayCard("player0", *numbered));

    // INFO: the condition stays true after the card reaches the discard, so
    //       within this one trigger pass only the `played` vector stops the
    //       scanner from auto-playing it again (up to must_apply_cap).
    CHECK(CountEvents(*engine, "auto_played") == 1);
}

TEST_CASE("engine auto_trigger: discarded card is not re-selected next pass") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(AutoTriggerMod());

    std::unique_ptr<MatchInstance> engine = MakeAutoEngine(content, 5, 42);
    const std::optional<ecs::Entity> auto_card = FindAutoCard(*engine);
    REQUIRE(auto_card.has_value());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> numbered0 = LegalNumbered(*engine);
    REQUIRE(numbered0.has_value());
    // INFO: a spare keeps player0's hand non-empty after the play, so the
    //       turn advances instead of settling into a hand-empty win check.
    const std::optional<ecs::Entity> spare0 =
        LegalNumbered(*engine, {*numbered0});
    REQUIRE(spare0.has_value());
    ForceHand(*engine, player0, {*numbered0, *auto_card, *spare0});

    REQUIRE(engine->PlayCard("player0", *numbered0));
    REQUIRE(CountEvents(*engine, "auto_played") == 1);

    // INFO: player1 now triggers a second pass. The auto card is in the
    //       discard, so the kHand zone gate keeps it out of the candidate set
    //       even though its `always` condition is still true (the `played`
    //       vector does not survive into this pass).
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    REQUIRE(engine->GetCurrentPlayerUsername() == "player1");
    const std::optional<ecs::Entity> numbered1 =
        LegalNumbered(*engine, {*auto_card, *numbered0, *spare0});
    REQUIRE(numbered1.has_value());
    const std::optional<ecs::Entity> spare1 =
        LegalNumbered(*engine, {*auto_card, *numbered0, *spare0, *numbered1});
    REQUIRE(spare1.has_value());
    ForceHand(*engine, player1, {*numbered1, *spare1});

    REQUIRE(engine->PlayCard("player1", *numbered1));

    CHECK(CountEvents(*engine, "auto_played") == 1);
    const ecs::InZone* zone = engine->Store().Get<ecs::InZone>(*auto_card);
    REQUIRE(zone != nullptr);
    CHECK(zone->zone.kind == ecs::ZoneKind::kDiscardPile);
}

TEST_CASE("engine auto_trigger: a card outside a hand is never selected") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(AutoTriggerMod());

    std::unique_ptr<MatchInstance> engine = MakeAutoEngine(content, 5, 42);
    const std::optional<ecs::Entity> auto_card = FindAutoCard(*engine);
    REQUIRE(auto_card.has_value());

    // INFO: the auto card stays in the draw pile; only a hand card is forced
    //       in. The scanner must ignore the non-hand auto card outright.
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> numbered =
        LegalNumbered(*engine, {*auto_card});
    REQUIRE(numbered.has_value());
    ForceHand(*engine, player0, {*numbered});
    ops::MoveCardToZone(engine->Store(), *auto_card,
                        ecs::ZoneRef{ecs::ZoneKind::kDrawPile,
                                     ecs::Entity{}});

    const ecs::InZone* before = engine->Store().Get<ecs::InZone>(*auto_card);
    REQUIRE(before != nullptr);
    REQUIRE(before->zone.kind == ecs::ZoneKind::kDrawPile);

    REQUIRE(engine->PlayCard("player0", *numbered));

    CHECK(CountEvents(*engine, "auto_played") == 0);
    const ecs::InZone* after = engine->Store().Get<ecs::InZone>(*auto_card);
    REQUIRE(after != nullptr);
    CHECK(after->zone.kind == ecs::ZoneKind::kDrawPile);
}

TEST_CASE("engine auto_trigger: card_played leads the graph event and names its cause") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(AutoTriggerMod());

    std::unique_ptr<MatchInstance> engine = MakeAutoEngine(content, 5, 42);
    const std::optional<ecs::Entity> auto_card = FindAutoCard(*engine);
    REQUIRE(auto_card.has_value());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> numbered = LegalNumbered(*engine);
    REQUIRE(numbered.has_value());
    ForceHand(*engine, player0, {*numbered, *auto_card});

    REQUIRE(engine->PlayCard("player0", *numbered));

    const json* played = CardEvent(*engine, "card_played", *auto_card);
    const json* auto_played = CardEvent(*engine, "auto_played", *auto_card);
    const json* signal = SignalEvent(*engine, "wp9x_auto_fired");
    REQUIRE(played != nullptr);
    REQUIRE(auto_played != nullptr);
    REQUIRE(signal != nullptr);

    // INFO: an auto play is its own causal chain, exactly like a manual play:
    //       the card_played leads, auto_played marks it, and only then do the
    //       graph events it causes arrive.
    CHECK(EventOrdinal(*engine, *played) < EventOrdinal(*engine, *auto_played));
    CHECK(EventOrdinal(*engine, *auto_played)
          < EventOrdinal(*engine, *signal));
    const int64_t cause =
        (*signal)["causal_frame"].value("cause_event", int64_t{-1});
    CHECK(cause == (*played)["event_id"].get<int64_t>());
}

TEST_CASE("engine auto_trigger: a prompt keeps the auto play cause across the answer") {
    Content content;
    REQUIRE(LoadContent(content));
    content.mods.push_back(AutoTriggerMod(MarkNodes("wp9x_auto_fired")));

    std::unique_ptr<MatchInstance> engine = MakeAutoEngine(content, 5, 42);
    const std::optional<ecs::Entity> auto_card = FindAutoCard(*engine);
    REQUIRE(auto_card.has_value());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> numbered = LegalNumbered(*engine);
    REQUIRE(numbered.has_value());
    ForceHand(*engine, player0, {*numbered, *auto_card});
    AttachStatusPromptSystem(*engine, "wp9x_prompt_answered");

    REQUIRE(engine->PlayCard("player0", *numbered));

    const json* played = CardEvent(*engine, "card_played", *auto_card);
    REQUIRE(played != nullptr);

    // INFO: the parked prompt belongs to a graph driven by an event the auto
    //       card's graph emitted, so the answer resumes that chain and not the
    //       enclosing play's.
    const std::optional<json> pending = engine->PendingInput();
    REQUIRE(pending.has_value());
    CHECK((*pending)["kind"] == "choose_color");
    REQUIRE(engine->SubmitInput("player0", "red"));

    const json* signal = SignalEvent(*engine, "wp9x_prompt_answered");
    REQUIRE(signal != nullptr);
    const int64_t cause =
        (*signal)["causal_frame"].value("cause_event", int64_t{-1});
    CHECK(cause == (*played)["event_id"].get<int64_t>());
}

TEST_CASE("engine auto_trigger: non-must-apply trigger does not fire") {
    Content content;
    REQUIRE(LoadContent(content));
    // INFO: 9-4b ruling - the scanner only fires `must_apply: true`; the
    //       Shield shape parses/validates but has no phase-1 firing semantics.
    content.mods.push_back(
        AutoTriggerMod(SignalNodes("wp9x_auto_fired"), /*must_apply=*/false));

    std::unique_ptr<MatchInstance> engine = MakeAutoEngine(content, 5, 42);
    const std::optional<ecs::Entity> auto_card = FindAutoCard(*engine);
    REQUIRE(auto_card.has_value());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> numbered = LegalNumbered(*engine);
    REQUIRE(numbered.has_value());
    ForceHand(*engine, player0, {*numbered, *auto_card});

    REQUIRE(engine->PlayCard("player0", *numbered));

    CHECK(CountEvents(*engine, "auto_played") == 0);
    CHECK_FALSE(HasSignal(*engine, "wp9x_auto_fired"));
    const ecs::InZone* zone = engine->Store().Get<ecs::InZone>(*auto_card);
    REQUIRE(zone != nullptr);
    CHECK(zone->zone.kind == ecs::ZoneKind::kHand);
}
