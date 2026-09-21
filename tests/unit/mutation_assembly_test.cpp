#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/**
 * @file mutation_assembly_test.cpp
 * @brief End-to-end tests: the mutation compiler wired into assembly.
 *
 * Each case builds in-memory `LoadedMod` fixtures (no filesystem content, so
 * `mods/vanilla/` is untouched), assembles through `MatchAssembler`, plays a
 * card through the real `MatchInstance`, and asserts the observable effect of
 * a `replace` / `wrap` / `veto` mutation. The deferred paths (`filter` and a
 * restriction-target mutation colliding with a behavior kind) assert the
 * compiler's WARN fires at assembly and the original graph still runs.
 */

namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

/** @brief Capture `std::cout` for the lifetime of the object (WARN checks). */
class CaptureStdout {
   public:
    CaptureStdout() : previous_(std::cout.rdbuf(buffer_.rdbuf())) {}
    ~CaptureStdout() { std::cout.rdbuf(previous_); }
    CaptureStdout(const CaptureStdout&) = delete;
    CaptureStdout& operator=(const CaptureStdout&) = delete;

    std::string Text() const { return buffer_.str(); }

   private:
    std::stringstream buffer_;
    std::streambuf* previous_;
};

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

BehaviorGraph MakeGraph(json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

json SignalNode(const std::string& id, const std::string& name,
                const std::string& next = std::string()) {
    json node =
        json{{"id", id}, {"op", "emit_signal"}, {"args", {{"name", name}}}};
    if (!next.empty()) node["next"] = next;
    return node;
}

json CallOriginalNode(const std::string& id) {
    return json{{"id", id}, {"op", "call_original"}};
}

ModManifest Manifest(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

/** @brief A `namespace:local` text card whose `on_play` emits `signal`. */
CardDef SignalCard(const std::string& ns, const std::string& local,
                   const std::string& signal) {
    CardDef card;
    card.id = local;
    card.namespace_id = ns;
    card.kind_id = ns + ":" + local;
    card.title = local;
    card.face.kind = match::modload::FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = std::string("1");
    card.tags = {"numbered"};
    BehaviorEntry entry;
    entry.hook = "on_play";
    entry.graph = MakeGraph(json::array({SignalNode("n1", signal)}));
    card.behaviors.push_back(std::move(entry));
    return card;
}

/** @brief A numbered text card with no behaviors (rule-hook tests). */
CardDef PlainCard(const std::string& ns, const std::string& local) {
    CardDef card;
    card.id = local;
    card.namespace_id = ns;
    card.kind_id = ns + ":" + local;
    card.title = local;
    card.face.kind = match::modload::FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = std::string("1");
    card.tags = {"numbered"};
    return card;
}

/** @brief `SignalCard` plus a second behavior declaring a restriction entry. */
CardDef SignalCardWithRestriction(const std::string& ns,
                                  const std::string& local,
                                  const std::string& signal,
                                  const std::string& entry_id) {
    CardDef card = SignalCard(ns, local, signal);
    BehaviorEntry entry;
    entry.hook = "draw";
    entry.graph = MakeGraph(json::array({json{
        {"id", "d1"},
        {"op", "add_restriction"},
        {"args",
         {{"entry_def",
           {{"id", entry_id},
            {"phase", "deny"},
            {"condition", {{"never", json::object()}}}}}}}}}));
    card.behaviors.push_back(std::move(entry));
    return card;
}

MutationDef Mutation(const std::string& ns, const std::string& local,
                     const std::string& target, const std::string& mode,
                     json nodes) {
    MutationDef mutation;
    mutation.id = local;
    mutation.namespace_id = ns;
    mutation.mutation_id = ns + ":" + local;
    mutation.target = target;
    mutation.mode = mode;
    mutation.replacement = MakeGraph(std::move(nodes));
    return mutation;
}

DeckDef FixtureDeck(const std::vector<std::string>& mods,
                    const std::string& kind_id, int count) {
    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = mods;
    deck.cards = {{kind_id, count}};
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
                                        int cards = 2, uint64_t seed = 7) {
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
        match::ops::MoveCardToZone(engine.Store(), card,
                                   ecs::ZoneRef{ecs::ZoneKind::kDrawPile,
                                                ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        match::ops::MoveCardToZone(engine.Store(), card,
                                   ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

std::optional<ecs::Entity> FindKindCard(MatchInstance& engine,
                                        const std::string& kind_id) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) return card;
    }
    return std::nullopt;
}

/** @brief Play the first card of `kind_id` and return the `signal` names. */
std::vector<std::string> PlaySignals(MatchInstance& engine,
                                     const std::string& username,
                                     const std::string& kind_id) {
    const std::optional<ecs::Entity> player = engine.FindPlayer(username);
    REQUIRE(player.has_value());
    const std::optional<ecs::Entity> card = FindKindCard(engine, kind_id);
    REQUIRE(card.has_value());
    ForceHand(engine, *player, {*card});
    REQUIRE(engine.PlayCard(username, *card));

    std::vector<std::string> signals;
    for (const json& event : engine.Events()) {
        if (!event.is_object()) continue;
        if (event.value("type", std::string()) != "signal") continue;
        const auto payload = event.find("payload");
        if (payload == event.end() || !payload->is_object()) continue;
        signals.push_back(payload->value("name", std::string()));
    }
    return signals;
}

const ecs::RestrictionEntry* FindRestriction(const MatchInstance& engine,
                                             const std::string& id) {
    const ecs::PlayRestriction* pipeline =
        engine.Store().Get<ecs::PlayRestriction>(engine.Registries().match);
    if (pipeline == nullptr) return nullptr;
    for (const ecs::RestrictionEntry& entry : pipeline->entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("mutation assembly: replace swaps a card's on_play graph") {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(SignalCard("base", "wild", "original"));

    LoadedMod replacer;
    replacer.manifest = Manifest("replacer");
    replacer.mutations.push_back(Mutation(
        "replacer", "swap", "base:wild", "replace",
        json::array({SignalNode("r1", "replaced")})));

    std::unique_ptr<MatchInstance> engine =
        Assemble({base, replacer}, FixtureDeck({"base", "replacer"},
                                               "base:wild", 8));

    const std::vector<std::string> signals =
        PlaySignals(*engine, "player0", "base:wild");
    REQUIRE(signals.size() == 1);
    CHECK(signals[0] == "replaced");
}

TEST_CASE("mutation assembly: wrap before splices call_original end-to-end") {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(SignalCard("base", "wild", "original"));

    LoadedMod wrapper;
    wrapper.manifest = Manifest("wrapper");
    wrapper.mutations.push_back(Mutation(
        "wrapper", "prelude", "base:wild", "wrap",
        json::array({SignalNode("w1", "prelude", "w2"),
                     CallOriginalNode("w2")})));

    std::unique_ptr<MatchInstance> engine =
        Assemble({base, wrapper}, FixtureDeck({"base", "wrapper"},
                                              "base:wild", 8));

    const std::vector<std::string> signals =
        PlaySignals(*engine, "player0", "base:wild");
    REQUIRE(signals.size() == 2);
    CHECK(signals[0] == "prelude");
    CHECK(signals[1] == "original");
}

TEST_CASE("mutation assembly: veto suppresses the original only on a match") {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(SignalCard("base", "wild", "original"));

    LoadedMod guard;
    guard.manifest = Manifest("guard");
    MutationDef veto = Mutation("guard", "stop", "base:wild", "veto",
                                json::array());
    veto.where = json{{"always", json::object()}};
    guard.mutations.push_back(std::move(veto));

    SUBCASE("where matches: the original graph does not run") {
        std::unique_ptr<MatchInstance> engine =
            Assemble({base, guard}, FixtureDeck({"base", "guard"},
                                                "base:wild", 8));
        CHECK(PlaySignals(*engine, "player0", "base:wild").empty());
    }

    SUBCASE("where does not match: the original graph runs") {
        guard.mutations[0].where = json{{"never", json::object()}};
        std::unique_ptr<MatchInstance> engine =
            Assemble({base, guard}, FixtureDeck({"base", "guard"},
                                                "base:wild", 8));
        const std::vector<std::string> signals =
            PlaySignals(*engine, "player0", "base:wild");
        REQUIRE(signals.size() == 1);
        CHECK(signals[0] == "original");
    }
}

TEST_CASE("mutation assembly: two wraps from two mods fold in mod-list order") {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(SignalCard("base", "wild", "base"));

    LoadedMod alpha;
    alpha.manifest = Manifest("alpha");
    alpha.mutations.push_back(Mutation(
        "alpha", "wrap", "base:wild", "wrap",
        json::array({SignalNode("a1", "alpha")})));

    LoadedMod beta;
    beta.manifest = Manifest("beta");
    beta.mutations.push_back(Mutation(
        "beta", "wrap", "base:wild", "wrap",
        json::array({SignalNode("b1", "beta")})));

    std::unique_ptr<MatchInstance> engine =
        Assemble({base, alpha, beta}, FixtureDeck({"base", "alpha", "beta"},
                                                  "base:wild", 8));

    // INFO: mod-list order is execution order: the first mod wraps outermost,
    //       so alpha's injected graph runs, then beta's, then the base.
    const std::vector<std::string> signals =
        PlaySignals(*engine, "player0", "base:wild");
    REQUIRE(signals.size() == 3);
    CHECK(signals[0] == "alpha");
    CHECK(signals[1] == "beta");
    CHECK(signals[2] == "base");
}

TEST_CASE("mutation assembly: two after-wraps from two mods keep mod order") {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(SignalCard("base", "wild", "base"));

    LoadedMod alpha;
    alpha.manifest = Manifest("alpha");
    MutationDef alpha_wrap = Mutation(
        "alpha", "wrap", "base:wild", "wrap",
        json::array({SignalNode("a1", "alpha")}));
    alpha_wrap.position = std::string("after");
    alpha.mutations.push_back(std::move(alpha_wrap));

    LoadedMod beta;
    beta.manifest = Manifest("beta");
    MutationDef beta_wrap = Mutation(
        "beta", "wrap", "base:wild", "wrap",
        json::array({SignalNode("b1", "beta")}));
    beta_wrap.position = std::string("after");
    beta.mutations.push_back(std::move(beta_wrap));

    std::unique_ptr<MatchInstance> engine =
        Assemble({base, alpha, beta}, FixtureDeck({"base", "alpha", "beta"},
                                                  "base:wild", 8));

    // INFO: after-wraps run after the base in mod order: base, alpha, beta.
    const std::vector<std::string> signals =
        PlaySignals(*engine, "player0", "base:wild");
    REQUIRE(signals.size() == 3);
    CHECK(signals[0] == "base");
    CHECK(signals[1] == "alpha");
    CHECK(signals[2] == "beta");
}

TEST_CASE("mutation assembly: kind target wins over a colliding restriction") {
    LoadedMod fix;
    fix.manifest = Manifest("fix");
    fix.cards.push_back(SignalCard("fix", "guard", "original"));

    RuleDef rule;
    rule.id = "install_guard";
    rule.namespace_id = "fix";
    rule.rule_id = "fix:install_guard";
    rule.title = "install guard";
    BehaviorEntry hook;
    hook.hook = "after:match_start";
    hook.graph = MakeGraph(json::array({json{
        {"id", "n1"},
        {"op", "add_restriction"},
        {"args",
         {{"entry_def",
           {{"id", "fix:guard"},
            {"phase", "deny"},
            {"condition", {{"never", json::object()}}}}}}}}}));
    rule.hooks.push_back(std::move(hook));
    fix.rules.push_back(std::move(rule));

    // INFO: the target string is both a card kind and a restriction entry; a
    //       kind target always applies to the kind graph.
    fix.mutations.push_back(Mutation(
        "fix", "hijack", "fix:guard", "replace",
        json::array({SignalNode("h1", "mutated")})));

    std::unique_ptr<MatchInstance> engine;
    std::string log;
    {
        CaptureStdout capture;
        engine = Assemble({fix}, FixtureDeck({"fix"}, "fix:guard", 8));
        log = capture.Text();
    }

    CHECK(log.find("targets restriction entry") == std::string::npos);
    REQUIRE(FindRestriction(*engine, "fix:guard") != nullptr);

    const std::vector<std::string> signals =
        PlaySignals(*engine, "player0", "fix:guard");
    REQUIRE(signals.size() == 1);
    CHECK(signals[0] == "mutated");
}

TEST_CASE("mutation assembly: restriction-only target warns and stays inert") {
    LoadedMod fix;
    fix.manifest = Manifest("fix");
    // INFO: the restriction entry is declared in a card behavior (not a rule),
    //       so the assembler must scan that source to classify it (I-3).
    fix.cards.push_back(
        SignalCardWithRestriction("fix", "wild", "original", "fix:ward"));
    fix.mutations.push_back(Mutation(
        "fix", "orphan", "fix:ward", "replace",
        json::array({SignalNode("o1", "mutated")})));

    std::unique_ptr<MatchInstance> engine;
    std::string log;
    {
        CaptureStdout capture;
        engine = Assemble({fix}, FixtureDeck({"fix"}, "fix:wild", 8));
        log = capture.Text();
    }

    CHECK(log.find("targets restriction entry") != std::string::npos);

    const std::vector<std::string> signals =
        PlaySignals(*engine, "player0", "fix:wild");
    REQUIRE(signals.size() == 1);
    CHECK(signals[0] == "original");
}

TEST_CASE("mutation assembly: replace retargets a rule hook graph") {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(PlainCard("base", "wild"));

    RuleDef rule;
    rule.id = "on_any_play";
    rule.namespace_id = "base";
    rule.rule_id = "base:on_any_play";
    rule.title = "on any play";
    BehaviorEntry hook;
    hook.hook = "after:play";
    hook.graph = MakeGraph(json::array({SignalNode("r1", "rule_original")}));
    rule.hooks.push_back(std::move(hook));
    base.rules.push_back(std::move(rule));

    LoadedMod replacer;
    replacer.manifest = Manifest("replacer");
    replacer.mutations.push_back(Mutation(
        "replacer", "swap", "base:on_any_play", "replace",
        json::array({SignalNode("r2", "rule_replaced")})));

    std::unique_ptr<MatchInstance> engine =
        Assemble({base, replacer}, FixtureDeck({"base", "replacer"},
                                               "base:wild", 8));

    const std::vector<std::string> signals =
        PlaySignals(*engine, "player0", "base:wild");
    REQUIRE(signals.size() == 1);
    CHECK(signals[0] == "rule_replaced");
}

TEST_CASE("mutation assembly: veto guards a replacement in either order") {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(SignalCard("base", "wild", "original"));

    auto guard_mod = [](bool veto_first, bool veto_matches) {
        LoadedMod guard;
        guard.manifest = Manifest("guard");
        MutationDef veto =
            Mutation("guard", "stop", "base:wild", "veto", json::array());
        veto.where =
            json{{veto_matches ? "always" : "never", json::object()}};
        MutationDef replace =
            Mutation("guard", "swap", "base:wild", "replace",
                     json::array({SignalNode("g1", "replaced")}));
        if (veto_first) {
            guard.mutations.push_back(std::move(veto));
            guard.mutations.push_back(std::move(replace));
        } else {
            guard.mutations.push_back(std::move(replace));
            guard.mutations.push_back(std::move(veto));
        }
        return guard;
    };

    SUBCASE("[veto, replace]: a matching veto suppresses the replacement") {
        std::unique_ptr<MatchInstance> engine =
            Assemble({base, guard_mod(true, true)},
                     FixtureDeck({"base", "guard"}, "base:wild", 8));
        CHECK(PlaySignals(*engine, "player0", "base:wild").empty());
    }

    SUBCASE("[replace, veto]: a matching veto suppresses the replacement") {
        std::unique_ptr<MatchInstance> engine =
            Assemble({base, guard_mod(false, true)},
                     FixtureDeck({"base", "guard"}, "base:wild", 8));
        CHECK(PlaySignals(*engine, "player0", "base:wild").empty());
    }

    SUBCASE("[veto, replace]: a non-matching veto lets the replacement run") {
        std::unique_ptr<MatchInstance> engine =
            Assemble({base, guard_mod(true, false)},
                     FixtureDeck({"base", "guard"}, "base:wild", 8));
        const std::vector<std::string> signals =
            PlaySignals(*engine, "player0", "base:wild");
        REQUIRE(signals.size() == 1);
        CHECK(signals[0] == "replaced");
    }

    SUBCASE("[replace, veto]: a non-matching veto lets the replacement run") {
        std::unique_ptr<MatchInstance> engine =
            Assemble({base, guard_mod(false, false)},
                     FixtureDeck({"base", "guard"}, "base:wild", 8));
        const std::vector<std::string> signals =
            PlaySignals(*engine, "player0", "base:wild");
        REQUIRE(signals.size() == 1);
        CHECK(signals[0] == "replaced");
    }
}

TEST_CASE("mutation assembly: filter mutation is inert and warns") {
    LoadedMod fix;
    fix.manifest = Manifest("fix");
    fix.cards.push_back(SignalCard("fix", "wild", "original"));
    fix.mutations.push_back(Mutation(
        "fix", "shape", "fix:wild", "filter",
        json::array({SignalNode("f1", "filtered")})));

    std::unique_ptr<MatchInstance> engine;
    std::string log;
    {
        CaptureStdout capture;
        engine = Assemble({fix}, FixtureDeck({"fix"}, "fix:wild", 8));
        log = capture.Text();
    }

    CHECK(log.find("deferred") != std::string::npos);

    const std::vector<std::string> signals =
        PlaySignals(*engine, "player0", "fix:wild");
    REQUIRE(signals.size() == 1);
    CHECK(signals[0] == "original");
}
