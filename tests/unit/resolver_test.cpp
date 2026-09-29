#include <doctest/doctest.h>

#include <match/resolver.hpp>

#include <common/env.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::MatchMeta;
using match::ecs::PlayerInfo;
using match::modload::BehaviorGraph;
using match::ops::InputRequest;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::ResolutionFrame;
using match::resolver::ConditionRegistry;
using match::resolver::ResolveResult;
using match::resolver::ResolveStatus;
using match::resolver::Resolver;
using match::resolver::ResolverConfig;
using match::resolver::SelectorContext;

using nlohmann::json;

namespace {

/** INFO: scoped env override; guards are captured at construction. */
struct EnvGuard {
    std::string key;
    std::string old;
    bool had = false;

    EnvGuard(std::string k, std::string value) : key(std::move(k)) {
        const char* current = std::getenv(key.c_str());
        if (current != nullptr) {
            had = true;
            old = current;
        }
        Env::SetEnv(key, value);
    }

    ~EnvGuard() {
        Env::SetEnv(key, had ? old : std::string());
    }
};

BehaviorGraph MakeGraph(json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

Entity AddPlayer(EntityStore& store, const std::string& name, uint32_t seat) {
    Entity entity = store.Create();
    PlayerInfo info;
    info.username = name;
    info.seat = seat;
    store.Add(entity, info);
    return entity;
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

OpResult BindProbe(EntityStore&, const OpArgs& args, OpContext&) {
    json event = {{"type", "probe"}};
    auto record = [&event, &args](const char* name) {
        const std::vector<Entity>* found = args.FindEntities(name);
        if (found == nullptr) {
            event[name] = nullptr;
            return;
        }
        json array = json::array();
        for (Entity entity : *found) {
            array.push_back({{"index", entity.index},
                             {"gen", entity.generation}});
        }
        event[name] = array;
    };
    record("target");
    record("viewer");
    record("card");
    OpResult result = OpResult::Resolved();
    result.events.push_back(event);
    return result;
}

OpResult NeedInput(EntityStore&, const OpArgs&, OpContext& ctx) {
    ctx.input_request = InputRequest{};
    ctx.input_request->kind = "choose_color";
    return OpResult::NeedsInput(json{{"kind", "choose_color"}});
}

OpResult PromptProbe(EntityStore&, const OpArgs&, OpContext& ctx) {
    const json* value = ctx.frame.FindPromptValue("n0");
    json out = value != nullptr ? *value : json(nullptr);
    OpResult result = OpResult::Resolved(out);
    result.events.push_back({{"type", "prompt_probe"}, {"value", out}});
    return result;
}

std::string TagString(const ResolveResult& result) {
    std::string joined;
    for (const json& event : result.events) {
        if (event.value("type", std::string()) != "synthetic") continue;
        if (!joined.empty()) joined += ",";
        joined += event.value("tag", std::string());
    }
    return joined;
}

const json* FindEvent(const ResolveResult& result, const std::string& type) {
    for (const json& event : result.events) {
        if (event.value("type", std::string()) == type) return &event;
    }
    return nullptr;
}

bool HasGuard(const ResolveResult& result, const std::string& needle) {
    for (const std::string& violation : result.guard_violations) {
        if (violation.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("resolver: branch routes true/false/else and fork is ordered") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);

    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;

    const json nodes = json::array({
        {{"id", "n0"},
         {"cases", {{{"when", false}, {"next", "a"}},
                    {{"when", true}, {"next", "b"}}}},
         {"else", "e"}},
        {{"id", "a"}, {"op", "synthetic_log"}, {"args", {{"tag", "A"}}},
         {"next", "fork"}},
        {{"id", "b"}, {"op", "synthetic_log"}, {"args", {{"tag", "B"}}},
         {"next", "fork"}},
        {{"id", "e"}, {"op", "synthetic_log"}, {"args", {{"tag", "E"}}},
         {"next", "fork"}},
        {{"id", "fork"}, {"branches", {"f1", "f2"}}},
        {{"id", "f1"}, {"op", "synthetic_log"}, {"args", {{"tag", "F1"}}}},
        {{"id", "f2"}, {"op", "synthetic_log"}, {"args", {{"tag", "F2"}}}},
    });
    BehaviorGraph graph = MakeGraph(nodes);

    ResolveResult result = resolver.Resolve(graph, "m", context, frame);
    CHECK(result.status == ResolveStatus::kComplete);
    CHECK(TagString(result) == "B,F1,F2");
    CHECK(result.guard_violations.empty());

    // INFO: no case matches, so the else route is taken.
    BehaviorGraph else_graph = MakeGraph(json::array({
        {{"id", "n0"}, {"cases", {{{"when", false}, {"next", "a"}}}},
         {"else", "e"}},
        {{"id", "a"}, {"op", "synthetic_log"}, {"args", {{"tag", "A"}}}},
        {{"id", "e"}, {"op", "synthetic_log"}, {"args", {{"tag", "E"}}}},
    }));
    ResolveResult else_result =
        resolver.Resolve(else_graph, "m", context, frame);
    CHECK(else_result.status == ResolveStatus::kComplete);
    CHECK(TagString(else_result) == "E");
}

TEST_CASE("resolver: condition registry seam drives branch routing") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    conditions.Register("synthetic_true",
                        [](EntityStore&, const json&, OpContext&) {
                            return true;
                        });
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);

    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"},
         {"cases", {{{"when", {{"synthetic_true", json::object()}}},
                     {"next", "yes"}},
                    {{"when", {{"unregistered", json::object()}}},
                     {"next", "no"}}}},
         {"else", "no"}},
        {{"id", "yes"}, {"op", "synthetic_log"}, {"args", {{"tag", "YES"}}}},
        {{"id", "no"}, {"op", "synthetic_log"}, {"args", {{"tag", "NO"}}}},
    }));

    ResolveResult result = resolver.Resolve(graph, "m", context, frame);
    CHECK(result.status == ResolveStatus::kComplete);
    CHECK(TagString(result) == "YES");
}

TEST_CASE("resolver: selectors bind and out-of-context selectors guard") {
    EntityStore store;
    Entity self = AddPlayer(store, "self", 0);
    Entity target = AddPlayer(store, "target", 1);
    Entity responder = AddPlayer(store, "responder", 2);
    Entity match = store.Create();
    store.Add(match, MatchMeta{});

    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_bind", &BindProbe);

    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"},
         {"op", "synthetic_bind"},
         {"args", {{"target", "@self"},
                   {"viewer", "@responder"},
                   {"card", "@card"}}},
         {"next", "n1"}},
        {{"id", "n1"},
         {"op", "synthetic_bind"},
         {"args", {{"target", "@target"}}}},
    }));

    SelectorContext context;
    context.self = self;
    context.target = target;
    ResolveResult result = resolver.Resolve(graph, "m", context, frame);

    CHECK(result.status == ResolveStatus::kComplete);
    CHECK(HasGuard(result, "@responder"));
    CHECK(HasGuard(result, "@card"));

    const json* probe = FindEvent(result, "probe");
    REQUIRE(probe != nullptr);
    REQUIRE((*probe)["target"].is_array());
    CHECK((*probe)["target"].size() == 1);
    CHECK((*probe)["target"][0]["index"] == self.index);
    REQUIRE((*probe)["viewer"].is_array());
    CHECK((*probe)["viewer"].empty());
    REQUIRE((*probe)["card"].is_array());
    CHECK((*probe)["card"].empty());

    // INFO: in-context the guarded selectors bind normally.
    ResolutionFrame window_frame;
    SelectorContext window_context;
    window_context.self = self;
    window_context.responder = responder;
    window_context.in_window = true;
    window_context.card = target;
    window_context.in_card_context = true;
    ResolveResult in_context =
        resolver.Resolve(graph, "m", window_context, window_frame);
    CHECK(in_context.status == ResolveStatus::kComplete);
    CHECK(in_context.guard_violations.empty());
    const json* window_probe = FindEvent(in_context, "probe");
    REQUIRE(window_probe != nullptr);
    REQUIRE((*window_probe)["viewer"].is_array());
    CHECK((*window_probe)["viewer"][0]["index"] == responder.index);
    REQUIRE((*window_probe)["card"].is_array());
    CHECK((*window_probe)["card"][0]["index"] == target.index);
}

TEST_CASE("resolver: chain budget aborts cleanly and stays usable") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"modX"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);

    ResolverConfig config;
    config.chain_budget = 3;
    Resolver resolver(store, runtime, bus, ledger, conditions, config);
    ResolutionFrame frame;
    SelectorContext context;

    json nodes = json::array();
    for (int i = 0; i < 6; ++i) {
        json node = {{"id", "n" + std::to_string(i)},
                     {"op", "synthetic_log"},
                     {"args", {{"tag", "N" + std::to_string(i)}}}};
        if (i < 5) node["next"] = "n" + std::to_string(i + 1);
        nodes.push_back(node);
    }
    BehaviorGraph graph = MakeGraph(nodes);

    ResolveResult result = resolver.Resolve(graph, "modX", context, frame);
    CHECK(result.status == ResolveStatus::kAborted);
    CHECK(result.aborted);
    CHECK(result.aborted_mod == "modX");
    CHECK(result.aborted_node == "n3");
    CHECK(ledger.chain_steps == 3);

    const json* aborted = FindEvent(result, "chain_aborted");
    REQUIRE(aborted != nullptr);
    CHECK((*aborted)["mod"] == "modX");
    CHECK((*aborted)["node"] == "n3");

    // INFO: the resolver stays usable; a new chain resets the step counter.
    BehaviorGraph small = MakeGraph(json::array({
        {{"id", "s0"}, {"op", "synthetic_log"}, {"args", {{"tag", "S"}}}},
    }));
    ResolveResult second = resolver.Resolve(small, "modX", context, frame);
    CHECK(second.status == ResolveStatus::kComplete);
    CHECK(TagString(second) == "S");
    CHECK(ledger.chain_steps == 1);
}

TEST_CASE("resolver: chain aborts feed the EventBus disarm threshold") {
    EnvGuard disarm("UNI_MOD_DISARM_THRESHOLD", "1");
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"modY"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);

    ResolverConfig config;
    config.chain_budget = 1;
    Resolver resolver(store, runtime, bus, ledger, conditions, config);
    ResolutionFrame frame;
    SelectorContext context;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"}, {"op", "synthetic_log"}, {"args", {{"tag", "A"}}},
         {"next", "n1"}},
        {{"id", "n1"}, {"op", "synthetic_log"}, {"args", {{"tag", "B"}}}},
    }));

    ResolveResult result = resolver.Resolve(graph, "modY", context, frame);
    CHECK(result.status == ResolveStatus::kAborted);
    CHECK(result.disarmed);
    CHECK(bus.IsDisarmed("modY"));
    CHECK(bus.AbortCount("modY") == 1);
    CHECK(FindEvent(result, "mod_disarmed") != nullptr);

    // INFO: a disarmed mod's systems are neutralized for the match.
    ResolveResult skipped = resolver.Resolve(graph, "modY", context, frame);
    CHECK(skipped.skipped_disarmed);
    CHECK(TagString(skipped).empty());
}

TEST_CASE("resolver: window node returns a request and resumes on a route") {
    EntityStore store;
    Entity self = AddPlayer(store, "self", 0);
    AddPlayer(store, "left", 1);
    AddPlayer(store, "right", 2);

    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;
    context.self = self;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"},
         {"window",
          {{"responders", "@others"},
           {"respond_with", {{"tag", "stackable"}}},
           {"duration", "env"},
           {"filter_digest", "d1"}}},
         {"default", "dflt"},
         {"on_response", {{"winner", "resp"}}}},
        {{"id", "dflt"},
         {"op", "synthetic_log"},
         {"args", {{"tag", "DEFAULT"}}}},
        {{"id", "resp"}, {"op", "synthetic_log"}, {"args", {{"tag", "RESP"}}}},
    }));

    ResolveResult paused = resolver.Resolve(graph, "m", context, frame);
    REQUIRE(paused.status == ResolveStatus::kWindow);
    REQUIRE(paused.window.has_value());
    CHECK(paused.window->node_id == "n0");
    CHECK(paused.window->responders.size() == 2);
    CHECK(paused.window->default_route == "dflt");
    CHECK(paused.window->resume_node == "dflt");
    CHECK(paused.window->duration == "env");
    CHECK(paused.window->filter_digest == "d1");
    CHECK(paused.window->respond_with["tag"] == "stackable");
    REQUIRE(paused.window->on_response.count("winner") == 1);
    CHECK(paused.window->on_response.at("winner") == "resp");
    REQUIRE(paused.resume.has_value());
    CHECK(paused.resume->node == "dflt");

    ResolveResult resumed =
        resolver.ResumeWindow(graph, "m", context, frame, paused, "resp");
    CHECK(resumed.status == ResolveStatus::kComplete);
    CHECK(TagString(resumed) == "RESP");

    // INFO: the default route resumes the timeout path.
    ResolveResult paused2 = resolver.Resolve(graph, "m", context, frame);
    REQUIRE(paused2.status == ResolveStatus::kWindow);
    ResolveResult timed_out = resolver.ResumeWindow(
        graph, "m", context, frame, paused2, paused2.window->default_route);
    CHECK(timed_out.status == ResolveStatus::kComplete);
    CHECK(TagString(timed_out) == "DEFAULT");
}

TEST_CASE("resolver: window duration accepts an integer override") {
    EntityStore store;
    Entity self = AddPlayer(store, "self", 0);
    AddPlayer(store, "left", 1);

    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    SelectorContext context;
    context.self = self;

    const auto pause_with = [&](const json& duration) {
        ResolutionFrame frame;
        BehaviorGraph graph = MakeGraph(json::array({
            {{"id", "n0"},
             {"window",
              {{"responders", "@others"}, {"duration", duration}}},
             {"default", "n0"}},
        }));
        return resolver.Resolve(graph, "m", context, frame);
    };

    ResolveResult integer = pause_with(800);
    REQUIRE(integer.window.has_value());
    CHECK(integer.window->duration_ms == 800);

    ResolveResult zero = pause_with(0);
    REQUIRE(zero.window.has_value());
    CHECK(zero.window->duration_ms == 0);

    ResolveResult env = pause_with("env");
    REQUIRE(env.window.has_value());
    CHECK(env.window->duration == "env");
    CHECK_FALSE(env.window->duration_ms.has_value());

    ResolveResult negative = pause_with(-5);
    REQUIRE(negative.window.has_value());
    CHECK_FALSE(negative.window->duration_ms.has_value());

    ResolveResult fractional = pause_with(1.5);
    REQUIRE(fractional.window.has_value());
    CHECK_FALSE(fractional.window->duration_ms.has_value());

    ResolveResult boolean = pause_with(true);
    REQUIRE(boolean.window.has_value());
    CHECK_FALSE(boolean.window->duration_ms.has_value());
}

TEST_CASE("resolver: window kind is carried, defaulted and validated") {
    EntityStore store;
    Entity self = AddPlayer(store, "self", 0);
    AddPlayer(store, "left", 1);

    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    SelectorContext context;
    context.self = self;

    const auto kind_of = [&](const json& window) {
        ResolutionFrame frame;
        BehaviorGraph graph = MakeGraph(json::array({
            {{"id", "n0"}, {"window", window}, {"default", "n0"}},
        }));
        ResolveResult result = resolver.Resolve(graph, "m", context, frame);
        REQUIRE(result.window.has_value());
        return result.window->kind;
    };

    CHECK(kind_of({{"responders", "@others"}, {"kind", "jump_in"}})
          == "jump_in");
    CHECK(kind_of({{"responders", "@others"}}) == "generic");
    CHECK(kind_of({{"responders", "@others"}, {"kind", "Bad Kind"}})
          == "generic");
    CHECK(kind_of({{"responders", "@others"}, {"kind", ""}}) == "generic");
    CHECK(kind_of({{"responders", "@others"}, {"kind", 5}}) == "generic");
    CHECK(kind_of({{"responders", "@others"},
                   {"kind", std::string(33, 'a')}})
          == "generic");
    CHECK(kind_of({{"responders", "@others"},
                   {"kind", std::string(32, 'a')}})
          == std::string(32, 'a'));
}

TEST_CASE("resolver: schedule node returns its request and fires later") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"},
         {"schedule", true},
         {"next", "n1"},
         {"duration", {{"unit", "turns"}, {"value", 2}}}},
        {{"id", "n1"}, {"op", "synthetic_log"}, {"args", {{"tag", "LATER"}}}},
    }));

    ResolveResult paused = resolver.Resolve(graph, "m", context, frame);
    REQUIRE(paused.status == ResolveStatus::kSchedule);
    REQUIRE(paused.schedule.has_value());
    CHECK(paused.schedule->node_id == "n0");
    CHECK(paused.schedule->resume_node == "n1");
    CHECK(paused.schedule->duration["unit"] == "turns");
    CHECK(paused.schedule->duration["value"] == 2);
    CHECK(TagString(paused).empty());

    // INFO: Re-enters at resume_node once the duration elapses.
    ResolveResult fired =
        resolver.Resolve(graph, "m", context, frame, "n1");
    CHECK(fired.status == ResolveStatus::kComplete);
    CHECK(TagString(fired) == "LATER");
}

TEST_CASE("resolver: op kNeedsInput pauses and resumes with injected input") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_need", &NeedInput);
    runtime.Register("synthetic_prompt_probe", &PromptProbe);
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"}, {"op", "synthetic_need"}, {"args", json::object()},
         {"next", "n1"}},
        {{"id", "n1"}, {"op", "synthetic_prompt_probe"},
         {"args", json::object()}},
    }));

    ResolveResult paused = resolver.Resolve(graph, "m", context, frame);
    REQUIRE(paused.status == ResolveStatus::kNeedsInput);
    REQUIRE(paused.input_request.has_value());
    CHECK(paused.input_request->kind == "choose_color");
    REQUIRE(paused.resume.has_value());
    CHECK(paused.resume->prompt == "n0");
    CHECK(paused.resume->node == "n1");

    ResolveResult resumed =
        resolver.ResumeInput(graph, "m", context, frame, paused, "red");
    CHECK(resumed.status == ResolveStatus::kComplete);
    const json* probe = FindEvent(resumed, "prompt_probe");
    REQUIRE(probe != nullptr);
    CHECK((*probe)["value"] == "red");
}

TEST_CASE("resolver: config reads guards from the environment") {
    EnvGuard budget("UNI_CHAIN_BUDGET", "7");
    EnvGuard events("UNI_EVENT_BUDGET", "13");
    ResolverConfig config = ResolverConfig::FromEnv();
    CHECK(config.chain_budget == 7);
    CHECK(config.event_budget == 13);
    CHECK(config.must_apply_cap == 4);
}

TEST_CASE("resolver: fork keeps later branches across an input pause") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);
    runtime.Register("synthetic_need", &NeedInput);
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"}, {"branches", {"A", "B"}}, {"next", "N"}},
        {{"id", "A"}, {"op", "synthetic_need"}, {"args", json::object()},
         {"next", "A2"}},
        {{"id", "A2"}, {"op", "synthetic_log"}, {"args", {{"tag", "A2"}}}},
        {{"id", "B"}, {"op", "synthetic_log"}, {"args", {{"tag", "B"}}}},
        {{"id", "N"}, {"op", "synthetic_log"}, {"args", {{"tag", "N"}}}},
    }));

    ResolveResult paused = resolver.Resolve(graph, "m", context, frame);
    REQUIRE(paused.status == ResolveStatus::kNeedsInput);
    REQUIRE(paused.resume.has_value());
    CHECK(paused.resume->prompt == "A");
    CHECK(paused.resume->node == "A2");
    REQUIRE(paused.resume->pending.size() == 2);
    CHECK(paused.resume->pending[0] == "N");
    CHECK(paused.resume->pending[1] == "B");

    ResolveResult resumed =
        resolver.ResumeInput(graph, "m", context, frame, paused, "red");
    CHECK(resumed.status == ResolveStatus::kComplete);
    CHECK(TagString(resumed) == "A2,B,N");
}

TEST_CASE("resolver: fork keeps later branches across a window pause") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"}, {"branches", {"A", "B"}}, {"next", "N"}},
        {{"id", "A"}, {"window", {{"responders", "@all_players"}}},
         {"default", "A2"}},
        {{"id", "A2"}, {"op", "synthetic_log"}, {"args", {{"tag", "A2"}}}},
        {{"id", "B"}, {"op", "synthetic_log"}, {"args", {{"tag", "B"}}}},
        {{"id", "N"}, {"op", "synthetic_log"}, {"args", {{"tag", "N"}}}},
    }));

    ResolveResult paused = resolver.Resolve(graph, "m", context, frame);
    REQUIRE(paused.status == ResolveStatus::kWindow);
    REQUIRE(paused.resume.has_value());
    REQUIRE(paused.resume->pending.size() == 2);
    CHECK(paused.resume->pending[0] == "N");
    CHECK(paused.resume->pending[1] == "B");

    ResolveResult resumed =
        resolver.ResumeWindow(graph, "m", context, frame, paused, "A2");
    CHECK(resumed.status == ResolveStatus::kComplete);
    CHECK(TagString(resumed) == "A2,B,N");
}

TEST_CASE("resolver: fork keeps later branches across a schedule pause") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;

    BehaviorGraph graph = MakeGraph(json::array({
        {{"id", "n0"}, {"branches", {"A", "B"}}, {"next", "N"}},
        {{"id", "A"}, {"schedule", true}, {"next", "A2"},
         {"duration", {{"unit", "turns"}, {"value", 1}}}},
        {{"id", "A2"}, {"op", "synthetic_log"}, {"args", {{"tag", "A2"}}}},
        {{"id", "B"}, {"op", "synthetic_log"}, {"args", {{"tag", "B"}}}},
        {{"id", "N"}, {"op", "synthetic_log"}, {"args", {{"tag", "N"}}}},
    }));

    ResolveResult paused = resolver.Resolve(graph, "m", context, frame);
    REQUIRE(paused.status == ResolveStatus::kSchedule);
    REQUIRE(paused.schedule.has_value());
    CHECK(paused.schedule->resume_node == "A2");
    REQUIRE(paused.schedule->pending.size() == 2);
    CHECK(paused.schedule->pending[0] == "N");
    CHECK(paused.schedule->pending[1] == "B");

    // INFO: Fires the deferred subgraph with the preserved continuation.
    ResolveResult fired =
        resolver.Resolve(graph, "m", context, frame,
                         paused.schedule->resume_node, false,
                         paused.schedule->pending);
    CHECK(fired.status == ResolveStatus::kComplete);
    CHECK(TagString(fired) == "A2,B,N");
}

TEST_CASE("resolver: non-object first node is a structural error") {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus({"m"});
    ConditionRegistry conditions;
    OpRuntime runtime;
    runtime.Register("synthetic_log", &LogOp);
    Resolver resolver(store, runtime, bus, ledger, conditions,
                      ResolverConfig{});
    ResolutionFrame frame;
    SelectorContext context;

    // INFO: the first node is an array, not a node object.
    BehaviorGraph malformed;
    malformed.nodes = json::array();
    malformed.nodes.push_back(json::array());
    malformed.raw = json{{"nodes", malformed.nodes}};

    ResolveResult result = resolver.Resolve(malformed, "m", context, frame);
    CHECK(result.status == ResolveStatus::kError);
    CHECK(result.error.find("not an object") != std::string::npos);

    // INFO: the resolver stays usable after a structural error.
    BehaviorGraph ok = MakeGraph(json::array({
        {{"id", "s0"}, {"op", "synthetic_log"}, {"args", {{"tag", "S"}}}},
    }));
    ResolveResult second = resolver.Resolve(ok, "m", context, frame);
    CHECK(second.status == ResolveStatus::kComplete);
    CHECK(TagString(second) == "S");
}
