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
            OpSignature s;
            s.name = "draw_cards";
            s.args = {Arg("target", ArgType::kSelector),
                      IntArg("n", 0, 1000),
                      Arg("from", ArgType::kPileRef, false),
                      Arg("filter", ArgType::kCondition, false)};
            add(std::move(s));
        }
        {
            OpSignature s;
            s.name = "move_card";
            s.args = {Arg("card", ArgType::kSelector), Arg("to_zone", ArgType::kZone)};
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
                      Arg("stack_policy", ArgType::kString, false)};
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
        "@card",
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
