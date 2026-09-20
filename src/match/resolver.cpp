#include <match/resolver.hpp>

#include <common/env.hpp>
#include <logger.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

/**
 * @file resolver.cpp
 * @brief Implementation of the behavior-graph walker.
 */

namespace match::resolver {

ResolverConfig ResolverConfig::FromEnv() {
    ResolverConfig config;
    config.chain_budget = static_cast<uint64_t>(
        Env::GetInt("UNI_CHAIN_BUDGET", 10000));
    config.event_budget = static_cast<uint64_t>(
        Env::GetInt("UNI_EVENT_BUDGET", 100000));
    config.must_apply_cap =
        static_cast<uint32_t>(Env::GetInt("UNI_MUST_APPLY_CAP", 4));
    return config;
}

// --- ConditionRegistry -----------------------------------------------------

bool ConditionRegistry::Register(const std::string& keyword, ConditionFn fn) {
    if (keyword.empty() || !fn) return false;
    evaluators_[keyword] = std::move(fn);
    return true;
}

bool ConditionRegistry::Has(const std::string& keyword) const {
    return evaluators_.find(keyword) != evaluators_.end();
}

bool ConditionRegistry::Evaluate(ecs::EntityStore& store,
                                 const nlohmann::json& condition,
                                 ops::OpContext& ctx) const {
    if (condition.is_boolean()) return condition.get<bool>();

    std::string keyword;
    nlohmann::json args = nlohmann::json::object();

    if (condition.is_string()) {
        keyword = condition.get<std::string>();
    } else if (condition.is_object()) {
        // INFO: accept `{keyword, args}`, `{condition, args}`, `{op, args}`
        //       and the single-key `{ "<keyword>": { ...args } }` sugar.
        if (condition.contains("keyword")
            && condition["keyword"].is_string()) {
            keyword = condition["keyword"].get<std::string>();
            if (condition.contains("args")) args = condition["args"];
        } else if (condition.contains("condition")
                   && condition["condition"].is_string()) {
            keyword = condition["condition"].get<std::string>();
            if (condition.contains("args")) args = condition["args"];
        } else if (condition.contains("op") && condition["op"].is_string()) {
            keyword = condition["op"].get<std::string>();
            if (condition.contains("args")) args = condition["args"];
        } else if (condition.size() == 1) {
            auto it = condition.begin();
            keyword = it.key();
            args = it.value();
        }
    } else {
        Logger::Warn("[Resolver] condition is not an object/string/bool");
        return false;
    }

    auto it = evaluators_.find(keyword);
    if (it == evaluators_.end()) {
        Logger::Warn("[Resolver] condition '", keyword,
                     "' has no evaluator");
        return false;
    }
    return it->second(store, args, ctx);
}

// --- Resolver construction -------------------------------------------------

Resolver::Resolver(ecs::EntityStore& store, const ops::OpRuntime& runtime,
                   ecs::EventBus& event_bus, ecs::BudgetLedger& budgets,
                   ConditionRegistry& conditions, ResolverConfig config)
    : store_(store),
      runtime_(runtime),
      event_bus_(event_bus),
      budgets_(budgets),
      conditions_(conditions),
      config_(config) {}

// --- public entry points ---------------------------------------------------

ResolveResult Resolver::Resolve(const modload::BehaviorGraph& graph,
                                const std::string& mod_id,
                                const SelectorContext& context,
                                ops::ResolutionFrame& frame,
                                const std::string& entry_node,
                                bool must_apply,
                                std::vector<std::string> pending) {
    ResolveResult result;
    if (!mod_id.empty() && event_bus_.IsDisarmed(mod_id)) {
        result.skipped_disarmed = true;
        result.disarmed = true;
        return result;
    }

    // INFO: a fresh graph resolution starts a new chain; window resumes share
    //       this counter, schedule resumes re-enter here and reset it.
    budgets_.chain_steps = 0;

    // INFO: a malformed graph (non-object first node) is a structural error,
    //       not an empty no-op; keep the runtime guard the brief requires.
    if (entry_node.empty() && graph.nodes.is_array()
        && !graph.nodes.empty() && !graph.nodes.front().is_object()) {
        result.error = "graph first node is not an object";
        Logger::Error("[Resolver] ", result.error);
        result.status = ResolveStatus::kError;
        return result;
    }

    // INFO: no early return after this point, so a manual release is safe.
    const bool applied = ApplyMustApply(result, must_apply);
    WalkState state = MakeState(graph, mod_id, context, frame, result);
    std::vector<std::string> stack =
        InitialStack(graph, entry_node, pending);
    result.status = Walk(state, std::move(stack));
    ReleaseMustApply(applied);
    return result;
}

ResolveResult Resolver::ResumeWindow(const modload::BehaviorGraph& graph,
                                     const std::string& mod_id,
                                     const SelectorContext& context,
                                     ops::ResolutionFrame& frame,
                                     const ResolveResult& pause,
                                     const std::string& route_node) {
    ResolveResult result;
    if (!mod_id.empty() && event_bus_.IsDisarmed(mod_id)) {
        result.skipped_disarmed = true;
        result.disarmed = true;
        return result;
    }

    SelectorContext window_context = context;
    window_context.in_window = true;
    WalkState state =
        MakeState(graph, mod_id, window_context, frame, result);
    // INFO: preserve the fork continuation captured at the pause, then run the
    //       caller-chosen route on top of it.
    std::vector<std::string> stack;
    if (pause.resume.has_value()) stack = pause.resume->pending;
    if (!route_node.empty()) stack.push_back(route_node);
    result.status = Walk(state, std::move(stack));
    return result;
}

ResolveResult Resolver::ResumeInput(const modload::BehaviorGraph& graph,
                                    const std::string& mod_id,
                                    const SelectorContext& context,
                                    ops::ResolutionFrame& frame,
                                    const ResolveResult& pause,
                                    nlohmann::json value) {
    ResolveResult result;
    if (!mod_id.empty() && event_bus_.IsDisarmed(mod_id)) {
        result.skipped_disarmed = true;
        result.disarmed = true;
        return result;
    }
    if (!pause.resume.has_value()) {
        result.status = ResolveStatus::kError;
        result.error = "resume input: pause carries no resume token";
        return result;
    }

    WalkState state = MakeState(graph, mod_id, context, frame, result);
    if (!pause.resume->prompt.empty()) {
        frame.BindPromptValue(pause.resume->prompt, std::move(value));
    }
    std::vector<std::string> stack = pause.resume->pending;
    if (!pause.resume->node.empty()) stack.push_back(pause.resume->node);
    result.status = Walk(state, std::move(stack));
    return result;
}

std::vector<std::string> Resolver::InitialStack(
    const modload::BehaviorGraph& graph, const std::string& entry,
    const std::vector<std::string>& pending) const {
    std::vector<std::string> stack = pending;
    std::string start = entry;
    if (start.empty() && graph.nodes.is_array() && !graph.nodes.empty()) {
        const nlohmann::json& first = graph.nodes.front();
        if (first.is_object()) start = first.value("id", std::string());
    }
    if (!start.empty()) stack.push_back(std::move(start));
    return stack;
}

// --- state helpers ---------------------------------------------------------

Resolver::WalkState Resolver::MakeState(
    const modload::BehaviorGraph& graph, const std::string& mod_id,
    const SelectorContext& context, ops::ResolutionFrame& frame,
    ResolveResult& result) {
    WalkState state;
    state.graph = &graph;
    state.mod_id = mod_id;
    state.context = context;
    state.frame = &frame;
    state.result = &result;
    if (graph.nodes.is_array()) {
        for (const nlohmann::json& node : graph.nodes) {
            if (!node.is_object()) continue;
            std::string id = node.value("id", std::string());
            if (!id.empty()) state.nodes_by_id.emplace(id, &node);
        }
    }
    BindStandardSelectors(state);
    return state;
}

void Resolver::BindStandardSelectors(WalkState& state) {
    // INFO: guarded selectors (@responder/@card) are bound on demand so an
    //       ordinary graph never records a spurious guard violation.
    static const char* const kStandard[] = {
        "@self",           "@target",     "@current_player",
        "@next_player",    "@prev_player", "@all_players",
        "@others",         "@draw_pile",  "@discard_pile",
        "@match"};
    for (const char* token : kStandard) {
        state.frame->BindSelector(token, ResolveSelector(state, token));
    }
}

void Resolver::BindSelectorArgs(WalkState& state, ops::OpArgs& args) {
    const modload::OpSignature* signature = args.Signature();
    if (signature != nullptr) {
        for (const modload::ArgSpec& spec : signature->args) {
            if (spec.type != modload::ArgType::kSelector) continue;
            const nlohmann::json* value = args.Find(spec.name);
            if (value == nullptr || !value->is_string()) continue;
            std::string token = value->get<std::string>();
            if (token.empty() || token[0] != '@') continue;
            args.BindSelector(spec.name, ResolveSelector(state, token));
        }
    }
    // INFO: also bind any raw arg whose string value is a known selector. This
    //       covers synthetic/test ops and future sugar without a catalog row.
    if (args.Raw().is_object()) {
        for (auto it = args.Raw().begin(); it != args.Raw().end(); ++it) {
            if (args.FindEntities(it.key()) != nullptr) continue;
            if (!it.value().is_string()) continue;
            std::string token = it.value().get<std::string>();
            if (!modload::IsKnownSelector(token)) continue;
            args.BindSelector(it.key(), ResolveSelector(state, token));
        }
    }
}

// --- selector resolution ---------------------------------------------------

std::vector<ecs::Entity> Resolver::PlayersBySeat() const {
    std::vector<ecs::Entity> players = store_.EntitiesWith<ecs::PlayerInfo>();
    std::stable_sort(
        players.begin(), players.end(),
        [this](ecs::Entity a, ecs::Entity b) {
            const ecs::PlayerInfo* pa = store_.Get<ecs::PlayerInfo>(a);
            const ecs::PlayerInfo* pb = store_.Get<ecs::PlayerInfo>(b);
            const uint32_t sa = pa == nullptr ? 0 : pa->seat;
            const uint32_t sb = pb == nullptr ? 0 : pb->seat;
            if (sa != sb) return sa < sb;
            return a.index < b.index;
        });
    return players;
}

std::optional<ecs::Entity> Resolver::FindCurrentPlayer() const {
    for (ecs::Entity entity : store_.EntitiesWith<ecs::TurnState>()) {
        const ecs::TurnState* turn = store_.Get<ecs::TurnState>(entity);
        if (turn != nullptr && turn->is_current) return entity;
    }
    return std::nullopt;
}

std::optional<ecs::Entity> Resolver::FindMatch() const {
    std::vector<ecs::Entity> matches = store_.EntitiesWith<ecs::MatchMeta>();
    if (matches.empty()) return std::nullopt;
    return matches.front();
}

std::optional<ecs::Entity> Resolver::FindPile(ecs::PileKind kind) const {
    for (ecs::Entity entity : store_.EntitiesWith<ecs::PileContents>()) {
        const ecs::PileContents* pile = store_.Get<ecs::PileContents>(entity);
        if (pile != nullptr && pile->kind == kind) return entity;
    }
    return std::nullopt;
}

int Resolver::DirectionStep() const {
    std::optional<ecs::Entity> match = FindMatch();
    if (!match.has_value()) return 1;
    const ecs::MatchMeta* meta = store_.Get<ecs::MatchMeta>(*match);
    if (meta != nullptr && meta->direction == ecs::Direction::kReverse) {
        return -1;
    }
    return 1;
}

std::optional<ecs::Entity> Resolver::Neighbor(
    const SelectorContext& context, int step) const {
    std::vector<ecs::Entity> players = PlayersBySeat();
    if (players.empty()) return std::nullopt;
    std::optional<ecs::Entity> anchor = FindCurrentPlayer();
    if (!anchor.has_value()) anchor = context.self;
    if (!anchor.has_value()) return std::nullopt;

    const int count = static_cast<int>(players.size());
    for (int i = 0; i < count; ++i) {
        if (!(players[static_cast<std::size_t>(i)] == *anchor)) continue;
        int index = (i + step) % count;
        if (index < 0) index += count;
        return players[static_cast<std::size_t>(index)];
    }
    return std::nullopt;
}

std::vector<ecs::Entity> Resolver::ResolveSelector(WalkState& state,
                                                   const std::string& token) {
    const SelectorContext& context = state.context;
    auto one = [](std::optional<ecs::Entity> entity) {
        std::vector<ecs::Entity> out;
        if (entity.has_value()) out.push_back(*entity);
        return out;
    };

    if (token == "@self") return one(context.self);
    if (token == "@target") return one(context.target);
    if (token == "@responder") {
        if (!context.in_window) {
            Guard(state, token, "outside a window route");
            return {};
        }
        return one(context.responder);
    }
    if (token == "@card") {
        if (!context.in_card_context) {
            Guard(state, token, "outside card behavior context");
            return {};
        }
        return one(context.card);
    }
    if (token == "@current_player") return one(FindCurrentPlayer());
    if (token == "@next_player") return one(Neighbor(context, DirectionStep()));
    if (token == "@prev_player") {
        return one(Neighbor(context, -DirectionStep()));
    }
    if (token == "@all_players") return PlayersBySeat();
    if (token == "@others") {
        std::vector<ecs::Entity> others = PlayersBySeat();
        if (context.self.has_value()) {
            others.erase(std::remove(others.begin(), others.end(),
                                     *context.self),
                         others.end());
        }
        return others;
    }
    if (token == "@choose_player") {
        Guard(state, token, "requires a prompt");
        return {};
    }
    if (token == "@draw_pile") return one(FindPile(ecs::PileKind::kDraw));
    if (token == "@discard_pile") {
        return one(FindPile(ecs::PileKind::kDiscard));
    }
    if (token == "@match") return one(FindMatch());
    if (token == "@drawn_card") {
        // INFO: The engine pre-binds `@drawn_card` into the frame for a
        //       draw dispatch (the just-drawn card entity). Unlike `@card` this
        //       is not a guarded card-behavior selector: absent binding simply
        //       yields no entity (fail-safe), with no guard violation.
        const std::vector<ecs::Entity>* bound =
            state.frame->FindSelector("@drawn_card");
        return bound == nullptr ? std::vector<ecs::Entity>() : *bound;
    }
    if (!token.empty() && token[0] == '@') {
        Guard(state, token, "unknown selector");
    }
    return {};
}

void Resolver::Guard(WalkState& state, const std::string& token,
                     const std::string& reason) {
    const std::string message = token + ": " + reason;
    state.result->guard_violations.push_back(message);
    Logger::Warn("[Resolver] selector guard: ", message);
}

// --- walking ---------------------------------------------------------------

ResolveStatus Resolver::Walk(WalkState& state,
                             std::vector<std::string> stack) {
    // INFO: LIFO work stack; `back()` is the next node. `next` routes and fork
    //       branches are pushed rather than recursed, so a pause can capture
    //       the whole remaining continuation (including fork branches).
    while (!stack.empty()) {
        const std::string current = stack.back();
        stack.pop_back();

        if (budgets_.chain_steps >= config_.chain_budget) {
            return Abort(state, current);
        }
        ++budgets_.chain_steps;

        auto it = state.nodes_by_id.find(current);
        if (it == state.nodes_by_id.end()) {
            state.result->error = "dangling node route: " + current;
            Logger::Error("[Resolver] ", state.result->error);
            return ResolveStatus::kError;
        }
        const nlohmann::json& node = *it->second;
        state.result->terminal_node = current;

        WalkCode code;
        if (node.contains("op")) {
            code = StepOp(state, node, stack);
        } else if (node.contains("cases")) {
            code = StepBranch(state, node, stack);
        } else if (node.contains("branches")) {
            code = StepFork(state, node, stack);
        } else if (node.contains("window")) {
            code = StepWindow(state, node, stack);
        } else if (node.contains("schedule")) {
            code = StepSchedule(state, node, stack);
        } else {
            state.result->error = "unknown node kind at: " + current;
            Logger::Error("[Resolver] ", state.result->error);
            return ResolveStatus::kError;
        }

        switch (code) {
            case WalkCode::kContinue:
                break;
            case WalkCode::kPause:
                return state.result->status;
            case WalkCode::kAbort:
                return ResolveStatus::kAborted;
            case WalkCode::kError:
                return ResolveStatus::kError;
        }
    }
    return ResolveStatus::kComplete;
}

Resolver::WalkCode Resolver::StepOp(WalkState& state,
                                    const nlohmann::json& node,
                                    std::vector<std::string>& stack) {
    const std::string op_name = node.value("op", std::string());
    const nlohmann::json raw =
        node.value("args", nlohmann::json::object());

    ops::OpArgs args(op_name, raw);
    BindSelectorArgs(state, args);

    ops::OpContext ctx(event_bus_, budgets_, *state.frame);
    ops::OpResult result = runtime_.Invoke(op_name, store_, args, ctx);

    for (const nlohmann::json& event : result.events) {
        AppendEvent(state, event);
    }
    for (const nlohmann::json& effect : result.effects) {
        state.result->effects.push_back(effect);
    }

    if (result.status == ops::OpStatus::kError) {
        state.result->error = result.error.empty()
                                  ? ("op '" + op_name + "' failed")
                                  : result.error;
        Logger::Error("[Resolver] op '", op_name, "' error: ",
                      state.result->error);
        return WalkCode::kError;
    }

    if (result.status == ops::OpStatus::kNeedsInput) {
        if (ctx.input_request.has_value()) {
            state.result->input_request = *ctx.input_request;
        } else {
            ops::InputRequest request;
            request.kind = result.value.is_object()
                               ? result.value.value("kind", std::string())
                               : std::string();
            request.payload = result.value;
            state.result->input_request = std::move(request);
        }
        ResumeToken token;
        token.prompt = node.value("id", std::string());
        token.node = node.value("next", std::string());
        token.pending = stack;
        state.result->resume = std::move(token);
        state.result->status = ResolveStatus::kNeedsInput;
        return WalkCode::kPause;
    }

    const std::string next = node.value("next", std::string());
    if (!next.empty()) stack.push_back(next);
    return WalkCode::kContinue;
}

Resolver::WalkCode Resolver::StepBranch(WalkState& state,
                                        const nlohmann::json& node,
                                        std::vector<std::string>& stack) {
    bool matched = false;
    std::string chosen;
    if (node.contains("cases") && node["cases"].is_array()) {
        for (const nlohmann::json& branch : node["cases"]) {
            if (!branch.is_object()) continue;
            const nlohmann::json when = branch.contains("when")
                                            ? branch["when"]
                                            : nlohmann::json();
            ops::OpContext ctx(event_bus_, budgets_, *state.frame);
            if (conditions_.Evaluate(store_, when, ctx)) {
                chosen = branch.value("next", std::string());
                matched = true;
                break;
            }
        }
    }
    if (!matched) chosen = node.value("else", std::string());
    if (!chosen.empty()) stack.push_back(chosen);
    return WalkCode::kContinue;
}

Resolver::WalkCode Resolver::StepFork(WalkState& state,
                                      const nlohmann::json& node,
                                      std::vector<std::string>& stack) {
    (void)state;
    // INFO: deterministic sequential fork. Push the fork's `next`
    //       first (bottom), then the branches in reverse so branch[0] runs
    //       first and the fork's `next` runs after all branches. Because the
    //       continuations live on the shared stack, pausing inside a branch
    //       preserves the remaining branches and the fork's `next`.
    const std::string next = node.value("next", std::string());
    if (!next.empty()) stack.push_back(next);
    if (node.contains("branches") && node["branches"].is_array()) {
        const nlohmann::json& branches = node["branches"];
        for (auto it = branches.rbegin(); it != branches.rend(); ++it) {
            if (!it->is_string()) continue;
            const std::string branch_id = it->get<std::string>();
            if (!branch_id.empty()) stack.push_back(branch_id);
        }
    }
    return WalkCode::kContinue;
}

Resolver::WalkCode Resolver::StepWindow(WalkState& state,
                                        const nlohmann::json& node,
                                        std::vector<std::string>& stack) {
    const nlohmann::json* window = nullptr;
    if (node.contains("window") && node["window"].is_object()) {
        window = &node["window"];
    }

    WindowRequest request;
    request.node_id = node.value("id", std::string());
    request.raw = node;
    if (window != nullptr) {
        request.responders_selector =
            window->value("responders", std::string());
        if (window->contains("respond_with")) {
            request.respond_with = (*window)["respond_with"];
        }
        request.duration = window->value("duration", std::string());
        request.filter_digest = window->value("filter_digest", std::string());
    }
    if (request.responders_selector.empty()) {
        request.responders_selector =
            node.value("responders", std::string());
    }
    if (request.respond_with.is_null() && node.contains("respond_with")) {
        request.respond_with = node["respond_with"];
    }
    if (request.duration.empty()) {
        request.duration = node.value("duration", std::string());
    }
    request.default_route = node.value("default", std::string());
    if (request.default_route.empty() && window != nullptr) {
        request.default_route =
            window->value("default_route", std::string());
    }

    const nlohmann::json* on_response = nullptr;
    if (node.contains("on_response")) {
        on_response = &node["on_response"];
    } else if (window != nullptr && window->contains("on_response")) {
        on_response = &(*window)["on_response"];
    }
    if (on_response != nullptr && on_response->is_object()) {
        for (auto it = on_response->begin(); it != on_response->end(); ++it) {
            if (it.value().is_string()) {
                request.on_response[it.key()] = it.value().get<std::string>();
            }
        }
    }

    request.resume_node = request.default_route;
    if (!request.responders_selector.empty()) {
        request.responders =
            ResolveSelector(state, request.responders_selector);
    }

    ResumeToken token;
    token.node = request.default_route;
    token.pending = stack;
    state.result->resume = std::move(token);
    state.result->window = std::move(request);
    state.result->status = ResolveStatus::kWindow;
    return WalkCode::kPause;
}

Resolver::WalkCode Resolver::StepSchedule(WalkState& state,
                                          const nlohmann::json& node,
                                          std::vector<std::string>& stack) {
    ScheduleRequest request;
    request.node_id = node.value("id", std::string());
    request.duration = node.contains("duration")
                           ? node["duration"]
                           : nlohmann::json::object();
    request.resume_node = node.value("next", std::string());
    request.pending = stack;

    ResumeToken token;
    token.node = request.resume_node;
    token.pending = stack;
    state.result->resume = std::move(token);
    state.result->schedule = std::move(request);
    state.result->status = ResolveStatus::kSchedule;
    return WalkCode::kPause;
}

// --- budgets and events ----------------------------------------------------

ResolveStatus Resolver::Abort(WalkState& state, const std::string& node) {
    ResolveResult& result = *state.result;
    result.aborted = true;
    result.aborted_mod = state.mod_id;
    result.aborted_node = node;
    result.status = ResolveStatus::kAborted;

    AppendEvent(state, nlohmann::json{{"type", "chain_aborted"},
                                      {"mod", state.mod_id},
                                      {"node", node}});
    Logger::Error("[Resolver] chain budget ", config_.chain_budget,
                  " exceeded for mod '", state.mod_id, "' at node '", node,
                  "'");

    if (!state.mod_id.empty() && event_bus_.NoteChainAbort(state.mod_id)) {
        result.disarmed = true;
        AppendEvent(state, nlohmann::json{{"type", "mod_disarmed"},
                                          {"mod", state.mod_id}});
        Logger::Error("[Resolver] mod '", state.mod_id, "' disarmed");
    }
    return ResolveStatus::kAborted;
}

void Resolver::AppendEvent(WalkState& state,
                           const nlohmann::json& event) {
    // INFO: the event budget drops only droppable (signal-class) events;
    //       state-changing events are never dropped.
    if (budgets_.events >= config_.event_budget
        && IsDroppableEvent(event)) {
        Logger::Warn("[Resolver] event budget ", config_.event_budget,
                     " reached; dropping signal event");
        return;
    }
    ++budgets_.events;
    state.result->events.push_back(event);
}

bool Resolver::IsDroppableEvent(const nlohmann::json& event) {
    if (!event.is_object()) return false;
    if (event.value("droppable", false)) return true;
    std::string type = event.value("type", std::string());
    if (type.empty()) type = event.value("kind", std::string());
    return type == "emit_signal" || type == "signal";
}

bool Resolver::ApplyMustApply(ResolveResult& result, bool requested) {
    if (!requested) {
        result.must_apply = false;
        return false;
    }
    if (budgets_.must_apply_depth >= config_.must_apply_cap) {
        Logger::Warn("[Resolver] must-apply depth cap ",
                     config_.must_apply_cap,
                     " reached; treating as non-must-apply");
        result.must_apply = false;
        return false;
    }
    ++budgets_.must_apply_depth;
    result.must_apply = true;
    return true;
}

void Resolver::ReleaseMustApply(bool applied) {
    if (applied && budgets_.must_apply_depth > 0) {
        --budgets_.must_apply_depth;
    }
}

}  // namespace match::resolver
