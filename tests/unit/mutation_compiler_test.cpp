#include <doctest/doctest.h>

#include <match/engine/mutation_compiler.hpp>
#include <match/resolver.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

/**
 * @file mutation_compiler_test.cpp
 * @brief `CompileMutations` acceptance tests.
 *
 * Every case builds a small graph whose ops log a tag, compiles the ordered
 * mutations, then either inspects the emitted node shape or walks the result
 * through the real Resolver to assert execution order. The `filter`,
 * restriction-target and malformed cases only require that compilation stays
 * inert and does not corrupt the graph.
 */

namespace {

using match::ecs::BudgetLedger;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::engine::CompileMutations;
using match::engine::MutationCompileOptions;
using match::modload::BehaviorGraph;
using match::modload::MutationDef;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::ResolutionFrame;
using match::resolver::ConditionRegistry;
using match::resolver::ResolveResult;
using match::resolver::Resolver;
using match::resolver::ResolverConfig;
using match::resolver::SelectorContext;

using nlohmann::json;

BehaviorGraph MakeGraph(json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

MutationDef MakeMutation(const std::string& mode, BehaviorGraph replacement) {
    MutationDef mutation;
    mutation.id = mode;
    mutation.mutation_id = "mod:" + mode;
    mutation.target = "mod:kind";
    mutation.mode = mode;
    mutation.replacement = std::move(replacement);
    mutation.raw = json::object();
    return mutation;
}

json LogNode(const std::string& id, const std::string& tag,
             const std::string& next = std::string()) {
    json node = {{"id", id}, {"op", "synthetic_log"}, {"args", {{"tag", tag}}}};
    if (!next.empty()) node["next"] = next;
    return node;
}

OpResult LogOp(EntityStore&, const OpArgs& args, OpContext&) {
    std::string tag = "?";
    if (const json* value = args.Find("tag");
        value != nullptr && value->is_string()) {
        tag = value->get<std::string>();
    }
    OpResult result = OpResult::Resolved();
    result.events.push_back({{"type", "synthetic"}, {"tag", tag}});
    return result;
}

/** @brief Compile + walk `graph`, returning the comma-joined synthetic tags. */
std::string RunTags(const BehaviorGraph& graph,
                    ConditionRegistry* conditions = nullptr) {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry local;
    ConditionRegistry& registry = conditions != nullptr ? *conditions : local;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);
    Resolver resolver(store, runtime, bus, ledger, registry, ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;
    ResolveResult result = resolver.Resolve(graph, "m", context, frame);

    std::string joined;
    for (const json& event : result.events) {
        if (event.value("type", std::string()) != "synthetic") continue;
        if (!joined.empty()) joined += ",";
        joined += event.value("tag", std::string());
    }
    return joined;
}

bool HasNodeId(const BehaviorGraph& graph, const std::string& id) {
    for (const json& node : graph.nodes) {
        if (node.value("id", std::string()) == id) return true;
    }
    return false;
}

std::size_t CountMarkers(const BehaviorGraph& graph) {
    std::size_t count = 0;
    for (const json& node : graph.nodes) {
        if (node.value("op", std::string()) == "call_original") ++count;
    }
    return count;
}

BehaviorGraph OneLogGraph() {
    return MakeGraph(json::array({LogNode("o1", "O")}));
}

}  // namespace

TEST_CASE("mutation compiler: replace returns the replacement verbatim") {
    BehaviorGraph original = OneLogGraph();
    MutationDef mutation =
        MakeMutation("replace", MakeGraph(json::array({LogNode("r1", "R")})));

    BehaviorGraph out = CompileMutations(original, {&mutation});

    CHECK(out.nodes == mutation.replacement.nodes);
    CHECK(out.nodes.front().value("id", std::string()) == "r1");
    CHECK(out.raw.is_object());
    CHECK(out.raw["nodes"] == out.nodes);
    CHECK(RunTags(out) == "R");
}

TEST_CASE("mutation compiler: wrap before splices call_original in place") {
    BehaviorGraph original = OneLogGraph();
    const json injected = json::array({
        LogNode("w1", "W1", "w2"),
        json{{"id", "w2"}, {"op", "call_original"}, {"next", "w3"}},
        LogNode("w3", "W2"),
    });
    MutationDef mutation = MakeMutation("wrap", MakeGraph(injected));

    BehaviorGraph out = CompileMutations(original, {&mutation});

    CHECK(CountMarkers(out) == 0);
    CHECK(RunTags(out) == "W1,O,W2");
}

TEST_CASE("mutation compiler: wrap before without marker runs injected first") {
    BehaviorGraph original = OneLogGraph();
    const json injected =
        json::array({LogNode("w1", "W1", "w2"), LogNode("w2", "W2")});
    MutationDef mutation = MakeMutation("wrap", MakeGraph(injected));

    BehaviorGraph out = CompileMutations(original, {&mutation});

    CHECK(RunTags(out) == "W1,W2,O");
}

TEST_CASE("mutation compiler: wrap after runs the injected graph last") {
    BehaviorGraph original = OneLogGraph();
    const json injected =
        json::array({LogNode("w1", "W1", "w2"), LogNode("w2", "W2")});
    MutationDef mutation = MakeMutation("wrap", MakeGraph(injected));
    mutation.position = std::string("after");

    BehaviorGraph out = CompileMutations(original, {&mutation});

    CHECK(RunTags(out) == "O,W1,W2");
}

TEST_CASE("mutation compiler: wraps fold in mod-list order") {
    BehaviorGraph original = OneLogGraph();
    MutationDef first =
        MakeMutation("wrap", MakeGraph(json::array({LogNode("a1", "A")})));
    MutationDef second =
        MakeMutation("wrap", MakeGraph(json::array({LogNode("b1", "B")})));

    BehaviorGraph out = CompileMutations(original, {&first, &second});

    CHECK(RunTags(out) == "B,A,O");
}

TEST_CASE("mutation compiler: veto guards the original on where match") {
    BehaviorGraph original = OneLogGraph();
    ConditionRegistry conditions;
    conditions.Register(
        "deny", [](EntityStore&, const json&, OpContext&) { return true; });
    conditions.Register(
        "allow", [](EntityStore&, const json&, OpContext&) { return false; });

    MutationDef veto = MakeMutation("veto", BehaviorGraph{});
    veto.where = json{{"deny", json::object()}};

    BehaviorGraph blocked = CompileMutations(original, {&veto});
    REQUIRE(blocked.nodes.front().contains("cases"));
    CHECK(blocked.nodes.front()["cases"].size() == 1);
    const std::string end_id =
        blocked.nodes.front()["cases"][0].value("next", std::string());
    CHECK(blocked.nodes.front().value("else", std::string()) == "o1");
    CHECK(HasNodeId(blocked, end_id));
    CHECK(RunTags(blocked, &conditions) == "");

    veto.where = json{{"allow", json::object()}};
    BehaviorGraph allowed = CompileMutations(original, {&veto});
    CHECK(RunTags(allowed, &conditions) == "O");
}

TEST_CASE("mutation compiler: multiple vetoes AND together") {
    BehaviorGraph original = OneLogGraph();
    ConditionRegistry conditions;
    conditions.Register(
        "yes", [](EntityStore&, const json&, OpContext&) { return true; });
    conditions.Register(
        "no", [](EntityStore&, const json&, OpContext&) { return false; });

    MutationDef yes = MakeMutation("veto", BehaviorGraph{});
    yes.where = json{{"yes", json::object()}};
    MutationDef no = MakeMutation("veto", BehaviorGraph{});
    no.where = json{{"no", json::object()}};

    // INFO: either veto alone suppresses the original; both must pass through.
    CHECK(RunTags(CompileMutations(original, {&yes, &no}), &conditions) == "");
    CHECK(RunTags(CompileMutations(original, {&no, &yes}), &conditions) == "");
    CHECK(RunTags(CompileMutations(original, {&no}), &conditions) == "O");
}

TEST_CASE("mutation compiler: filter, restriction target and malformed inert") {
    BehaviorGraph original = OneLogGraph();
    MutationDef filter =
        MakeMutation("filter", MakeGraph(json::array({LogNode("f1", "F")})));
    MutationDef unknown =
        MakeMutation("explode", MakeGraph(json::array({LogNode("x1", "X")})));
    MutationDef empty_wrap = MakeMutation("wrap", BehaviorGraph{});
    MutationDef restriction =
        MakeMutation("wrap", MakeGraph(json::array({LogNode("s1", "S")})));
    restriction.target = "mod:restriction_entry";
    MutationDef malformed = MakeMutation("wrap", BehaviorGraph{});
    malformed.replacement.nodes = json::array(
        {json::object(), LogNode("m1", "M")});

    std::vector<const MutationDef*> mutations = {
        nullptr,    &filter,      &unknown,
        &empty_wrap, &restriction, &malformed,
    };
    MutationCompileOptions options;
    options.restriction_targets.insert("mod:restriction_entry");

    BehaviorGraph out = CompileMutations(original, mutations, options);

    CHECK(out.nodes.is_array());
    CHECK(!out.nodes.empty());
    CHECK(out.raw.is_object());
    CHECK(out.raw["nodes"] == out.nodes);
    // INFO: only the well-formed malformed-case node and the original run.
    CHECK(RunTags(out) == "M,O");
}
