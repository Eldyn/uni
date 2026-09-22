#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

/**
 * @file vocabulary.hpp
 * @brief Phase-1 vocabulary tables (ops, conditions, hooks, selectors).
 *
 * The validator owns this file because the semantic validator (check 4) needs
 * op signatures before the runtime op catalog exists. The Resolver implements
 * the runtime op catalog keyed by these same names; the op layer implements
 * condition evaluation for the same keywords. This header is the single source
 * of truth for the phase-1 vocabulary; do not redefine it elsewhere.
 *
 * Sources: (node types / selectors), 8 (hooks), 10.1-10.10 (ops),
 * (conditions), (stack policies), (duration units),
 * (visibility aspects), (index-space bounds).
 */

namespace match::modload {

/**
 * Behavior-graph node shapes as the validator reads them. The Resolver and the
 * op layer and the content content must emit exactly these keys; every node is
 * `{ "id": <string>, ... }` and every route value is a node id string.
 *
 * - `op` node: `{ "op": <catalog name>, "args": { ... }, "next"?: <node id> }`.
 * - `window` node: `{ "window": { ... }, "default": <node id>,
 *   "on_response"?: { <filter key>: <node id> }, "reopen"?: <bool> }`.
 *   `reopen` opts the window into engine-owned chaining: after a
 *   winning response route drains, the engine re-opens the same window for
 *   the next responder on the same budget ledger.
 * - `branch` node: `{ "cases": [ { "when": <condition>, "next": <node id> } ],
 *   "else": <node id> }`.
 * - `fork` node: `{ "branches": [ <node id>, ... ] }` (sequential in ).
 * - `schedule` node: `{ "schedule": <any marker>, "next": <node id>,
 *   "duration": { "unit": ..., "value": ... } | [ ... ] }` — defers `next`
 *   until the duration elapses; `duration` accepts the same object/array
 *   form as a duration arg (compound array = AND).
 */

/**
 * @note `auto_trigger` is a card-level field, not an op or
 *       node type: it carries `{condition, graph, must_apply}` and the loader
 *       parses it into `modload::AutoTriggerDef`. It is absent from the op
 *       catalog below by design.
 */

/**
 * @enum ArgType
 * @brief Declared type of an op/condition argument.
 */
enum class ArgType {
    kSelector,          /**< context selector, e.g. "@self". */
    kInt,               /**< integer, optionally bounded. */
    kNumber,            /**< any JSON number. */
    kBool,              /**< JSON boolean. */
    kString,            /**< any JSON string. */
    kEnum,              /**< string from an explicit token set. */
    kDuration,          /**< duration spec, object or array. */
    kAspectMask,        /**< visibility aspect or array of aspects. */
    kCondition,         /**< condition object. */
    kKindRef,           /**< card kind ref: local id or `namespace:id`. */
    kStatusRef,         /**< status ref: local id or `namespace:id`. */
    kTagRef,            /**< free-form tag token. */
    kRestrictionRef,    /**< restriction entry id. */
    kNodeRef,           /**< graph node id (resolved within the graph). */
    kChoiceSpec,        /**< random | chosen | tag:x | kind:x. */
    kRollSpec,          /**< {sides, count, keep?}. */
    kRestrictionEntry,  /**< {id, phase, condition}. */
    kZone,              /**< hand | draw_pile | discard_pile | limbo. */
    kPileRef,           /**< draw | discard | @draw_pile | @discard_pile. */
    kComparison,        /**< comparison token (lt/lte/eq/ne/gt/gte). */
    kStackPolicy,       /**< replace|accumulate|independent|cap:N. */
    kObject,            /**< JSON object. */
    kArray,             /**< JSON array. */
    kAny,               /**< any JSON value. */
};

/**
 * @struct ArgSpec
 * @brief One declared argument of an op or condition.
 */
struct ArgSpec {
    std::string name;
    ArgType type = ArgType::kAny;
    bool required = true;
    bool has_bounds = false;        /**< kInt only. */
    double min_value = 0.0;
    double max_value = 0.0;
    std::vector<std::string> enum_values;  /**< kEnum only. */
};

/**
 * @struct OpSignature
 * @brief One row of the 10.x op vocabulary.
 */
struct OpSignature {
    std::string name;
    std::vector<ArgSpec> args;
    /** Exactly one arg name from each group must be present. */
    std::vector<std::vector<std::string>> either_of;
    bool allows_extra_args = false;
};

/**
 * @struct ConditionSignature
 * @brief One row of the condition vocabulary.
 */
struct ConditionSignature {
    std::string keyword;
    std::vector<ArgSpec> args;
    std::vector<std::vector<std::string>> either_of;
    /**
     * True when this keyword is a play-context predicate. Such
     * predicates are evaluated against a `PlayAttempt` by the restriction
     * pipeline (`PlayConditionMatcher`, `play_conditions.hpp`), never against
     * the entity store: the generic condition registry has no attempt and
     * binds them to fail-safe false.
     */
    bool play_context = false;
};

/**
 * @struct HookSpec
 * @brief One row of the section 8 hook catalog (bare name + phases).
 */
struct HookSpec {
    std::string name;
    bool before = true;
    bool after = true;
};

// --- builder helpers -------------------------------------------------------

/** @brief Build a plain required/optional arg spec. */
inline ArgSpec Arg(const std::string& name, ArgType type, bool required = true) {
    ArgSpec spec;
    spec.name = name;
    spec.type = type;
    spec.required = required;
    return spec;
}

/** @brief Build a bounded integer arg spec. */
inline ArgSpec IntArg(const std::string& name,
                      int lo,
                      int hi,
                      bool required = true) {
    ArgSpec spec = Arg(name, ArgType::kInt, required);
    spec.has_bounds = true;
    spec.min_value = lo;
    spec.max_value = hi;
    return spec;
}

/** @brief Build an enumerated-string arg spec. */
inline ArgSpec EnumArg(const std::string& name,
                       std::vector<std::string> values,
                       bool required = true) {
    ArgSpec spec = Arg(name, ArgType::kEnum, required);
    spec.enum_values = std::move(values);
    return spec;
}

// --- op catalog

/** @brief The full phase-1 op catalog. */
inline const std::vector<OpSignature>& OpCatalog() {
    static const std::vector<OpSignature> kOps = [] {
        std::vector<OpSignature> ops;

        auto add = [&ops](OpSignature sig) { ops.push_back(std::move(sig)); };

        // Card and pile ops.
        {
            // INFO: exactly one of `n` / `n_from_debt`.
            //       `n_from_debt` draws the target's accumulated
            //       `vanilla:draw_debt` status magnitude (0 when absent).
            OpSignature s;
            s.name = "draw_cards";
            s.args = {Arg("target", ArgType::kSelector),
                      IntArg("n", 0, 1000, false),
                      Arg("from", ArgType::kPileRef, false),
                      Arg("filter", ArgType::kCondition, false),
                      Arg("n_from_debt", ArgType::kBool, false)};
            s.either_of = {{"n", "n_from_debt"}};
            add(std::move(s));
        }
        {
            // INFO: Progressive capability. Draws from `from` (default
            //       draw pile) one card at a time until the freshly drawn card
            //       is playable under the `drawn_card_playable` rules,
            //       or the source is exhausted (the shared draw path
            //       reshuffles the discard). Bounded by the pile/card counts,
            //       never a balance cap; emits `cards_drawn` (and `reshuffle`).
            OpSignature s;
            s.name = "draw_until_playable";
            s.args = {Arg("target", ArgType::kSelector),
                      Arg("from", ArgType::kPileRef, false)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "move_card";
            s.args = {Arg("card", ArgType::kSelector), Arg("to_zone", ArgType::kZone)};
            add(std::move(s));
        }
        {
            // INFO: Force-play capability. The op emits a `play_card`
            //       effect; the engine routes it through the normal play
            //       pipeline (restriction -> before:play -> move -> after:play)
            //       rather than mutating the store directly. `player` defaults
            //       to the card's hand owner / `@self` / the current player.
            OpSignature s;
            s.name = "play_card";
            s.args = {Arg("card", ArgType::kSelector),
                      Arg("player", ArgType::kSelector, false)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "transfer_card";
            s.args = {Arg("from_player", ArgType::kSelector),
                      Arg("to_player", ArgType::kSelector),
                      Arg("selector", ArgType::kChoiceSpec)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "pass_hands";
            s.args = {EnumArg("direction", {"forward", "backward"})};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "swap_hands";
            s.args = {Arg("a", ArgType::kSelector), Arg("b", ArgType::kSelector)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "redistribute_hands";
            s.args = {EnumArg("mode", {"even"})};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "materialize_card";
            s.args = {Arg("target_player", ArgType::kSelector),
                      Arg("kind", ArgType::kKindRef),
                      Arg("zone", ArgType::kZone, false)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "remove_card";
            s.args = {Arg("card", ArgType::kSelector)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "replace_card";
            s.args = {Arg("card", ArgType::kSelector),
                      Arg("kind", ArgType::kKindRef)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "peek_pile";
            s.args = {Arg("viewer", ArgType::kSelector),
                      Arg("pile", ArgType::kPileRef),
                      IntArg("n", 0, 1000)};
            add(std::move(s));
        }

        // Turn and flow ops.
        {
            OpSignature s;
            s.name = "advance_turn";
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "skip_turn";
            s.args = {Arg("target", ArgType::kSelector)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "reverse_direction";
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "extra_turn";
            s.args = {Arg("target", ArgType::kSelector)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "redirect_turn";
            s.args = {Arg("target", ArgType::kSelector)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "set_turn_timer";
            s.args = {Arg("target", ArgType::kSelector),
                      Arg("duration", ArgType::kDuration)};
            add(std::move(s));
        }

        // Status ops.
        {
            OpSignature s;
            s.name = "apply_status";
            s.args = {Arg("target", ArgType::kSelector),
                      Arg("status_kind", ArgType::kStatusRef),
                      Arg("params", ArgType::kObject, false),
                      Arg("duration", ArgType::kDuration, false),
                      Arg("stack_policy", ArgType::kStackPolicy, false)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "remove_status";
            s.args = {Arg("target", ArgType::kSelector),
                      Arg("status_kind", ArgType::kStatusRef, false),
                      Arg("instance", ArgType::kInt, false)};
            s.either_of = {{"status_kind", "instance"}};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "modify_status";
            s.args = {Arg("target", ArgType::kSelector),
                      Arg("kind", ArgType::kStatusRef),
                      IntArg("delta", -1000, 1000)};
            add(std::move(s));
        }

        // Prompt op.
        {
            OpSignature s;
            s.name = "prompt";
            s.args = {Arg("kind", ArgType::kString),
                      Arg("target", ArgType::kSelector),
                      Arg("payload", ArgType::kObject, false),
                      IntArg("timeout", 0, 600000, false),
                      Arg("default", ArgType::kAny, false)};
            add(std::move(s));
        }

        // Randomness.
        {
            OpSignature s;
            s.name = "roll";
            s.args = {Arg("spec", ArgType::kRollSpec)};
            add(std::move(s));
        }

        // Visibility ops.
        {
            OpSignature s;
            s.name = "grant_visibility";
            s.args = {Arg("viewer", ArgType::kSelector),
                      Arg("target", ArgType::kSelector),
                      Arg("aspects", ArgType::kAspectMask),
                      Arg("duration", ArgType::kDuration, false)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "revoke_visibility";
            s.args = {Arg("viewer", ArgType::kSelector),
                      Arg("target", ArgType::kSelector),
                      Arg("aspects", ArgType::kAspectMask, false)};
            add(std::move(s));
        }

        // Type and restriction ops.
        {
            OpSignature s;
            s.name = "set_active_type";
            s.args = {Arg("type", ArgType::kString, false),
                      Arg("from_prompt", ArgType::kNodeRef, false)};
            s.either_of = {{"type", "from_prompt"}};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "clear_active_type";
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "add_restriction";
            s.args = {Arg("entry_def", ArgType::kRestrictionEntry)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "remove_restriction";
            s.args = {Arg("entry_id", ArgType::kRestrictionRef)};
            add(std::move(s));
        }

        // Win ops.
        {
            OpSignature s;
            s.name = "check_win";
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "declare_winner";
            s.args = {Arg("player", ArgType::kSelector),
                      EnumArg("kind", {"normal", "special"})};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "add_placement";
            s.args = {Arg("player", ArgType::kSelector)};
            add(std::move(s));
        }

        // Scheduling and meta.
        {
            OpSignature s;
            s.name = "emit_signal";
            s.args = {Arg("name", ArgType::kString),
                      Arg("payload", ArgType::kObject, false)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "call_original";
            add(std::move(s));
        }

        return ops;
    }();
    return kOps;
}

/** @brief Find an op signature by name; nullptr when unknown. */
inline const OpSignature* FindOp(const std::string& name) {
    for (const auto& op : OpCatalog()) {
        if (op.name == name) return &op;
    }
    return nullptr;
}

// --- condition catalog

/** @brief The full phase-1 condition vocabulary. */
inline const std::vector<ConditionSignature>& ConditionCatalog() {
    static const std::vector<ConditionSignature> kConditions = [] {
        std::vector<ConditionSignature> out;
        auto add = [&out](ConditionSignature sig) { out.push_back(std::move(sig)); };

        {
            ConditionSignature c;
            c.keyword = "has_card_kind";
            c.args = {Arg("target", ArgType::kSelector),
                      Arg("kind", ArgType::kKindRef)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "has_card_tag";
            c.args = {Arg("target", ArgType::kSelector),
                      Arg("tag", ArgType::kTagRef)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "hand_size";
            c.args = {Arg("target", ArgType::kSelector),
                      Arg("cmp", ArgType::kComparison),
                      IntArg("n", 0, 1000)};
            add(std::move(c));
        }
        {
            // INFO: Seated (live) player count compared with `cmp`/`n`.
            //       Vanilla reverse uses it to add the 2-player extra advance
            //       (legacy ReverseEffect::Resolve).
            ConditionSignature c;
            c.keyword = "player_count";
            c.args = {Arg("cmp", ArgType::kComparison), IntArg("n", 0, 1000)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "active_type_is";
            c.args = {Arg("type", ArgType::kString)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "top_of_discard";
            c.args = {Arg("kind", ArgType::kKindRef, false),
                      Arg("tag", ArgType::kTagRef, false),
                      Arg("type", ArgType::kString, false)};
            c.either_of = {{"kind", "tag", "type"}};
            add(std::move(c));
        }
        {
            // INFO: True when the just-drawn card (frame selector
            //       `@drawn_card`, bound by the engine on a draw hook) is
            //       playable under the vanilla colour/value rules: wild, or
            //       colour == active type, or value == discard-top value.
            //       Fail-safe false when no card is bound or facts are unknown.
            ConditionSignature c;
            c.keyword = "drawn_card_playable";
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "status_active";
            c.args = {Arg("target", ArgType::kSelector),
                      Arg("status_kind", ArgType::kStatusRef)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "draw_debt";
            c.args = {Arg("target", ArgType::kSelector),
                      Arg("cmp", ArgType::kComparison),
                      IntArg("n", 0, 1000)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "rolled";
            c.args = {Arg("cmp", ArgType::kComparison), IntArg("n", 0, 1000)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "is_direction";
            c.args = {EnumArg("direction", {"fwd", "rev"})};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "round";
            c.args = {Arg("cmp", ArgType::kComparison), IntArg("n", 0, 1000)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "turns_elapsed";
            c.args = {Arg("cmp", ArgType::kComparison), IntArg("n", 0, 1000)};
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "always";
            add(std::move(c));
        }
        {
            ConditionSignature c;
            c.keyword = "never";
            add(std::move(c));
        }

        // --- play-context predicates
        // Evaluated against a PlayAttempt by PlayConditionMatcher
        // (play_conditions.hpp), NOT against the entity store. Derived from
        // the legacy legality predicates in src/match/rules/*.cpp; the
        // generic condition registry binds every one of these to fail-safe
        // false because it never carries a PlayAttempt.
        auto add_play = [&out](const std::string& keyword,
                               std::vector<ArgSpec> args = {}) {
            ConditionSignature c;
            c.keyword = keyword;
            c.play_context = true;
            c.args = std::move(args);
            out.push_back(std::move(c));
        };

        // standard.cpp:8 (`if (event.is_out_of_turn) invalid`).
        add_play("in_turn");
        add_play("plays_out_of_turn");
        // standard.cpp:16/23/27-34: wild is always legal; otherwise colour
        // must equal the active type or value must equal the discard top.
        add_play("plays_matches_active");
        add_play("plays_matches_top");
        add_play("plays_mismatch");
        // jump_in.cpp:14-15: same type AND same value as the discard top.
        add_play("plays_identical_to_top");
        // Attempted-card identity (seven_zero value=7/0; no_bluffing +4).
        add_play("plays_kind", {Arg("kind", ArgType::kKindRef)});
        add_play("plays_tag", {Arg("tag", ArgType::kTagRef)});
        add_play("plays_color", {Arg("color", ArgType::kString)});
        add_play("plays_value", {Arg("value", ArgType::kString)});
        // Hand ownership (`must_own_card`); `plays_unowned` is the violation
        // form (a deny entry cannot express `not`).
        add_play("owns_card");
        add_play("plays_unowned");
        // no_bluffing.cpp:12-30: playing `value` while holding a card whose
        // colour equals the active type.
        add_play("plays_bluffing", {Arg("value", ArgType::kString)});

        return out;
    }();
    return kConditions;
}

/** @brief Find a condition signature by keyword; nullptr when unknown. */
inline const ConditionSignature* FindCondition(const std::string& keyword) {
    for (const auto& c : ConditionCatalog()) {
        if (c.keyword == keyword) return &c;
    }
    return nullptr;
}

// --- hook catalog

/** @brief The full phase-1 hook catalog (bare names). */
inline const std::vector<HookSpec>& HookCatalog() {
    static const std::vector<HookSpec> kHooks = {
        {"match_start", true, true},     {"match_end", true, true},
        {"round_start", true, true},     {"round_end", true, true},
        {"turn_start", true, true},      {"turn_end", true, true},
        {"play", true, true},            {"draw_attempt", true, true},
        {"draw", true, true},            {"shuffle", true, true},
        {"pile_empty", true, true},      {"hand_empty", true, true},
        {"win_check", true, true},       {"status_applied", true, true},
        {"status_removed", true, true},  {"window_open", true, true},
        {"window_close", true, true},    {"effect_applied", true, true},
        {"card_entered_zone", true, true},
        {"card_left_zone", true, true},  {"roll", true, true},
        {"visibility_granted", true, true},
        {"visibility_revoked", true, true},
    };
    return kHooks;
}

/** @brief True when `name` is a bare hook name in the catalog. */
inline bool IsKnownHookName(const std::string& name) {
    for (const auto& h : HookCatalog()) {
        if (h.name == name) return true;
    }
    return false;
}

/**
 * @brief Resolve a hook token to (bare name, phase).
 *
 * Accepts `before:X`, `after:X`, the card alias `on_play` (= `after:play`),
 * and a bare catalog name (phase left empty for the caller to fill).
 *
 * @return false when the name is not in the catalog or a phase is illegal.
 */
inline bool ResolveHook(const std::string& token,
                        std::string& name,
                        std::string& phase) {
    auto colon = token.find(':');
    if (colon != std::string::npos) {
        std::string prefix = token.substr(0, colon);
        if (prefix != "before" && prefix != "after") return false;
        name = token.substr(colon + 1);
        if (!IsKnownHookName(name)) return false;
        phase = prefix;
        return true;
    }
    if (token == "on_play") {
        name = "play";
        phase = "after";
        return true;
    }
    if (!IsKnownHookName(token)) return false;
    name = token;
    phase.clear();
    return true;
}

// --- selectors, aspects, durations, policies ---------------

/** @brief Context selector DSL tokens. */
inline const std::vector<std::string>& ContextSelectors() {
    static const std::vector<std::string> kSelectors = {
        "@self",         "@target",       "@responder",
        "@current_player", "@next_player", "@prev_player",
        "@all_players",  "@others",       "@choose_player",
        "@draw_pile",    "@discard_pile", "@match",
        "@card",         "@drawn_card",
    };
    return kSelectors;
}

/** @brief True when `token` is one of the context selector tokens. */
inline bool IsKnownSelector(const std::string& token) {
    for (const auto& s : ContextSelectors()) {
        if (s == token) return true;
    }
    return false;
}

/** @brief True when `token` resolves to a player set (seat-order iteration). */
inline bool IsSetSelector(const std::string& token) {
    return token == "@all_players" || token == "@others";
}

/** @brief Visibility aspects. */
inline const std::vector<std::string>& VisibilityAspects() {
    static const std::vector<std::string> kAspects = {
        "count", "color", "value", "identity", "position"};
    return kAspects;
}

/** @brief True when `token` is a visibility aspect. */
inline bool IsVisibilityAspect(const std::string& token) {
    for (const auto& a : VisibilityAspects()) {
        if (a == token) return true;
    }
    return false;
}

/** @brief Duration units. */
inline const std::vector<std::string>& DurationUnits() {
    static const std::vector<std::string> kUnits = {
        "ms", "turns", "rounds", "cards_played"};
    return kUnits;
}

/** @brief True when `token` is a duration unit. */
inline bool IsDurationUnit(const std::string& token) {
    for (const auto& u : DurationUnits()) {
        if (u == token) return true;
    }
    return false;
}

/**
 * @brief True when `policy` is `replace|accumulate|independent|cap:N`.
 */
inline bool IsValidStackPolicy(const std::string& policy) {
    if (policy == "replace" || policy == "accumulate"
        || policy == "independent") {
        return true;
    }
    if (policy.rfind("cap:", 0) == 0) {
        std::string n = policy.substr(4);
        if (n.empty()) return false;
        for (char c : n) {
            if (c < '0' || c > '9') return false;
        }
        return true;
    }
    return false;
}

/** @brief Comparison tokens accepted in condition args. */
inline const std::vector<std::string>& ComparisonTokens() {
    static const std::vector<std::string> kTokens = {
        "lt", "lte", "eq", "ne", "gt", "gte",
        "<",  "<=",  "==", "!=", ">",  ">="};
    return kTokens;
}

/** @brief True when `token` is a comparison token. */
inline bool IsComparisonToken(const std::string& token) {
    for (const auto& t : ComparisonTokens()) {
        if (t == token) return true;
    }
    return false;
}

/** @brief Zone tokens matching the ECS `ZoneKind` component names. */
inline const std::vector<std::string>& ZoneTokens() {
    static const std::vector<std::string> kZones = {
        "hand", "draw_pile", "discard_pile", "limbo"};
    return kZones;
}

/** @brief True when `token` is a zone token. */
inline bool IsZoneToken(const std::string& token) {
    for (const auto& z : ZoneTokens()) {
        if (z == token) return true;
    }
    return false;
}

// --- index-space bounds

inline constexpr int kMaxMods = 256;
inline constexpr int kMaxKindsPerMod = 4096;
inline constexpr int kMaxCopiesPerKind = 4096;

}  // namespace match::modload
