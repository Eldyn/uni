#include "match/modload/semantic_validator.hpp"

#include "logger.hpp"
#include "match/modload/base_mod.hpp"
#include "match/modload/binding_definition_keys.hpp"
#include "match/modload/derived_definition_keys.hpp"
#include "match/modload/event_definition_keys.hpp"
#include "match/modload/mod_loader.hpp"
#include "match/modload/streak_definition_keys.hpp"
#include "match/modload/glossary_entries.hpp"
#include "match/modload/texts_limits.hpp"
#include "match/streak/state_values.hpp"
#include "match/view/event_audience.hpp"
#include "match/modload/vocabulary.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace match::modload {

namespace {

bool IsValidWindowKind(const nlohmann::json& kind) {
    static const std::regex pattern("^[a-z0-9_:.-]{1,32}$");
    return kind.is_string()
           && std::regex_match(kind.get<std::string>(), pattern);
}

LoadError Err(const std::string& check,
              const std::string& artifact,
              const std::string& path,
              const std::string& message) {
    return LoadError{check, artifact, path, message};
}

std::string Join(const std::string& base, const std::string& key) {
    if (base.empty()) return "/" + key;
    return base + "/" + key;
}

/* INFO: same-document JSON-pointer resolution for `$ref` (#/...). External
 *       refs and non-pointer refs are compile errors. */
bool ResolvePointer(const nlohmann::json& root,
                    const std::string& ref,
                    nlohmann::json& out) {
    if (ref.empty() || ref[0] != '#') return false;
    std::string ptr = ref.substr(1);
    if (ptr.empty()) {
        out = root;
        return true;
    }
    if (ptr[0] != '/') return false;

    const nlohmann::json* cur = &root;
    std::size_t i = 1;
    while (i <= ptr.size()) {
        std::size_t j = ptr.find('/', i);
        std::string token =
            ptr.substr(i, j == std::string::npos ? std::string::npos : j - i);
        std::string unescaped;
        for (std::size_t k = 0; k < token.size(); ++k) {
            if (token[k] == '~' && k + 1 < token.size()
                && (token[k + 1] == '0' || token[k + 1] == '1')) {
                unescaped.push_back(token[k + 1] == '0' ? '~' : '/');
                ++k;
                continue;
            }
            unescaped.push_back(token[k]);
        }

        if (cur->is_object()) {
            auto it = cur->find(unescaped);
            if (it == cur->end()) return false;
            cur = &*it;
        } else if (cur->is_array()) {
            if (unescaped.empty()) return false;
            for (char c : unescaped) {
                if (c < '0' || c > '9') return false;
            }
            std::size_t index = static_cast<std::size_t>(std::stoul(unescaped));
            if (index >= cur->size()) return false;
            cur = &(*cur)[index];
        } else {
            return false;
        }

        if (j == std::string::npos) break;
        i = j + 1;
    }
    out = *cur;
    return true;
}

/* INFO: normalize a kind/status/restriction ref: a bare local id is scoped to
 *       the owning mod's namespace. */
std::string NormalizeRef(const std::string& raw, const std::string& ns) {
    if (raw.find(':') == std::string::npos) return ns + ":" + raw;
    return raw;
}

std::string RefNamespace(const std::string& full) {
    auto colon = full.find(':');
    if (colon == std::string::npos) return full;
    return full.substr(0, colon);
}

bool IsKnownType(const std::string& type) {
    static const std::set<std::string> kTypes = {
        "object", "array", "string", "integer",
        "number", "boolean", "null"};
    return kTypes.count(type) != 0;
}

const std::set<std::string>& StructuralKeywords() {
    static const std::set<std::string> kKeywords = {
        "$schema", "$id", "$defs", "title", "description", "default",
        "examples", "deprecated", "readOnly", "writeOnly", "$comment"};
    return kKeywords;
}

const std::set<std::string>& ValidationKeywords() {
    static const std::set<std::string> kKeywords = {
        "type", "required", "properties", "additionalProperties", "items",
        "enum", "const", "pattern", "minLength", "maxLength", "minimum",
        "maximum", "oneOf", "propertyNames", "$ref"};
    return kKeywords;
}

}  // namespace

// --- JSON Schema subset validator ------------------------------------------

const std::vector<std::string>& JsonSchemaSubsetValidator::SupportedKeywords() {
    static const std::vector<std::string> kKeywords = [] {
        std::vector<std::string> out(ValidationKeywords().begin(),
                                     ValidationKeywords().end());
        out.insert(out.end(), StructuralKeywords().begin(),
                   StructuralKeywords().end());
        return out;
    }();
    return kKeywords;
}

namespace {

bool CompileNode(const nlohmann::json& schema,
                 const nlohmann::json& root,
                 const std::string& path,
                 std::string& error) {
    if (schema.is_boolean()) return true;
    if (!schema.is_object()) {
        error = "schema node at '" + path + "' must be an object or boolean";
        return false;
    }

    for (auto it = schema.begin(); it != schema.end(); ++it) {
        const std::string& key = it.key();
        const nlohmann::json& value = it.value();

        if (StructuralKeywords().count(key)) {
            if (key == "$defs") {
                if (!value.is_object()) {
                    error = "$defs must be an object at '" + path + "'";
                    return false;
                }
                for (auto def = value.begin(); def != value.end(); ++def) {
                    if (!CompileNode(def.value(), root,
                                     path + "/$defs/" + def.key(), error)) {
                        return false;
                    }
                }
            }
            continue;
        }

        if (!ValidationKeywords().count(key)) {
            error = "unsupported schema keyword '" + key + "' at '" + path
                    + "'";
            return false;
        }

        if (key == "$ref") {
            if (!value.is_string() || value.get_ref<const std::string&>().empty()
                || value.get_ref<const std::string&>()[0] != '#') {
                error = "$ref must be a same-document '#' pointer at '" + path
                        + "'";
                return false;
            }
            nlohmann::json unused;
            if (!ResolvePointer(root, value.get<std::string>(), unused)) {
                error = "unresolved $ref '" + value.get<std::string>()
                        + "' at '" + path + "'";
                return false;
            }
        } else if (key == "type") {
            if (value.is_string()) {
                if (!IsKnownType(value.get<std::string>())) {
                    error = "unknown type '" + value.get<std::string>()
                            + "' at '" + path + "'";
                    return false;
                }
            } else if (value.is_array()) {
                for (const auto& t : value) {
                    if (!t.is_string() || !IsKnownType(t.get<std::string>())) {
                        error = "unknown type in type array at '" + path + "'";
                        return false;
                    }
                }
            } else {
                error = "type must be a string or array at '" + path + "'";
                return false;
            }
        } else if (key == "required") {
            if (!value.is_array()) {
                error = "required must be an array at '" + path + "'";
                return false;
            }
            for (const auto& r : value) {
                if (!r.is_string()) {
                    error = "required entries must be strings at '" + path
                            + "'";
                    return false;
                }
            }
        } else if (key == "properties") {
            if (!value.is_object()) {
                error = "properties must be an object at '" + path + "'";
                return false;
            }
            for (auto p = value.begin(); p != value.end(); ++p) {
                if (!CompileNode(p.value(), root,
                                 path + "/properties/" + p.key(), error)) {
                    return false;
                }
            }
        } else if (key == "additionalProperties") {
            if (!CompileNode(value, root, path + "/additionalProperties",
                             error)) {
                return false;
            }
        } else if (key == "items") {
            if (value.is_array()) {
                error = "array-form items is unsupported at '" + path + "'";
                return false;
            }
            if (!CompileNode(value, root, path + "/items", error)) return false;
        } else if (key == "enum") {
            if (!value.is_array()) {
                error = "enum must be an array at '" + path + "'";
                return false;
            }
        } else if (key == "const") {
            // any JSON value is a valid const
        } else if (key == "pattern") {
            if (!value.is_string()) {
                error = "pattern must be a string at '" + path + "'";
                return false;
            }
            try {
                std::regex re(value.get<std::string>());
                (void)re;
            } catch (const std::regex_error&) {
                error = "invalid pattern at '" + path + "'";
                return false;
            }
        } else if (key == "minLength" || key == "maxLength") {
            if (!value.is_number_integer() || value.get<std::int64_t>() < 0) {
                error = key + " must be a non-negative integer at '" + path
                        + "'";
                return false;
            }
        } else if (key == "minimum" || key == "maximum") {
            if (!value.is_number()) {
                error = key + " must be a number at '" + path + "'";
                return false;
            }
        } else if (key == "oneOf") {
            if (!value.is_array() || value.empty()) {
                error = "oneOf must be a non-empty array at '" + path + "'";
                return false;
            }
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (!CompileNode(value[i], root,
                                 path + "/oneOf/" + std::to_string(i),
                                 error)) {
                    return false;
                }
            }
        } else if (key == "propertyNames") {
            if (!CompileNode(value, root, path + "/propertyNames", error)) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

namespace {

bool TypeMatches(const std::string& type, const nlohmann::json& instance) {
    if (type == "object") return instance.is_object();
    if (type == "array") return instance.is_array();
    if (type == "string") return instance.is_string();
    if (type == "integer") return instance.is_number_integer();
    if (type == "number") return instance.is_number();
    if (type == "boolean") return instance.is_boolean();
    if (type == "null") return instance.is_null();
    return false;
}

void ValidateNode(const nlohmann::json& schema,
                  const nlohmann::json& instance,
                  const std::string& path,
                  const nlohmann::json& root,
                  std::vector<SchemaViolation>& out,
                  int depth) {
    if (depth > 64) {
        out.push_back({path, "schema recursion depth exceeded"});
        return;
    }
    if (schema.is_boolean()) {
        if (!schema.get<bool>()) out.push_back({path, "schema forbids any value"});
        return;
    }
    if (!schema.is_object()) return;

    if (schema.contains("$ref")) {
        nlohmann::json target;
        if (!ResolvePointer(root, schema["$ref"].get<std::string>(), target)) {
            out.push_back({path, "unresolved $ref"});
            return;
        }
        ValidateNode(target, instance, path, root, out, depth + 1);
        return;
    }

    if (schema.contains("const") && instance != schema["const"]) {
        out.push_back({path, "value must equal const"});
    }
    if (schema.contains("enum")) {
        bool found = false;
        for (const auto& allowed : schema["enum"]) {
            if (instance == allowed) {
                found = true;
                break;
            }
        }
        if (!found) out.push_back({path, "value not in enum"});
    }

    if (schema.contains("type")) {
        bool matched = false;
        if (schema["type"].is_string()) {
            matched = TypeMatches(schema["type"].get<std::string>(), instance);
        } else if (schema["type"].is_array()) {
            for (const auto& t : schema["type"]) {
                if (t.is_string()
                    && TypeMatches(t.get<std::string>(), instance)) {
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            out.push_back({path, "wrong JSON type"});
            return;
        }
    }

    if (schema.contains("oneOf")) {
        int matches = 0;
        for (const auto& branch : schema["oneOf"]) {
            std::vector<SchemaViolation> local;
            ValidateNode(branch, instance, path, root, local, depth + 1);
            if (local.empty()) ++matches;
        }
        if (matches != 1) {
            out.push_back({path, "value must match exactly one oneOf branch"});
        }
    }

    if (instance.is_object()) {
        if (schema.contains("required")) {
            for (const auto& key : schema["required"]) {
                if (!instance.contains(key.get<std::string>())) {
                    out.push_back({Join(path, key.get<std::string>()),
                                   "required property missing"});
                }
            }
        }
        if (schema.contains("propertyNames")) {
            for (auto it = instance.begin(); it != instance.end(); ++it) {
                ValidateNode(schema["propertyNames"], it.key(),
                             Join(path, it.key()), root, out, depth + 1);
            }
        }
        if (schema.contains("properties")) {
            for (auto it = instance.begin(); it != instance.end(); ++it) {
                auto prop = schema["properties"].find(it.key());
                if (prop != schema["properties"].end()) {
                    ValidateNode(*prop, it.value(), Join(path, it.key()), root,
                                 out, depth + 1);
                }
            }
        }
        if (schema.contains("additionalProperties")) {
            const auto& extra = schema["additionalProperties"];
            for (auto it = instance.begin(); it != instance.end(); ++it) {
                if (schema.contains("properties")
                    && schema["properties"].contains(it.key())) {
                    continue;
                }
                if (extra.is_boolean()) {
                    if (!extra.get<bool>()) {
                        out.push_back({Join(path, it.key()),
                                       "additional property not allowed"});
                    }
                } else {
                    ValidateNode(extra, it.value(), Join(path, it.key()), root,
                                 out, depth + 1);
                }
            }
        }
    }

    if (instance.is_array() && schema.contains("items")) {
        for (std::size_t i = 0; i < instance.size(); ++i) {
            ValidateNode(schema["items"], instance[i],
                         path + "/" + std::to_string(i), root, out, depth + 1);
        }
    }

    if (instance.is_string()) {
        const std::string& text = instance.get_ref<const std::string&>();
        if (schema.contains("minLength")
            && text.size() < static_cast<std::size_t>(
                   schema["minLength"].get<std::int64_t>())) {
            out.push_back({path, "string shorter than minLength"});
        }
        if (schema.contains("maxLength")
            && text.size() > static_cast<std::size_t>(
                   schema["maxLength"].get<std::int64_t>())) {
            out.push_back({path, "string longer than maxLength"});
        }
        if (schema.contains("pattern")) {
            try {
                std::regex re(schema["pattern"].get<std::string>());
                if (!std::regex_search(text, re)) {
                    out.push_back({path, "string does not match pattern"});
                }
            } catch (const std::regex_error&) {
                out.push_back({path, "schema pattern failed to compile"});
            }
        }
    }

    if (instance.is_number()) {
        double value = instance.get<double>();
        if (schema.contains("minimum")
            && value < schema["minimum"].get<double>()) {
            out.push_back({path, "number below minimum"});
        }
        if (schema.contains("maximum")
            && value > schema["maximum"].get<double>()) {
            out.push_back({path, "number above maximum"});
        }
    }
}

}  // namespace

bool JsonSchemaSubsetValidator::Compile(const nlohmann::json& schema,
                                        JsonSchemaSubsetValidator& out,
                                        std::string& error) {
    if (!CompileNode(schema, schema, "", error)) return false;
    out.root_ = schema;
    return true;
}

std::vector<SchemaViolation> JsonSchemaSubsetValidator::Validate(
    const nlohmann::json& instance) const {
    std::vector<SchemaViolation> out;
    ValidateNode(root_, instance, "", root_, out, 0);
    return out;
}

// --- semantic checker ------------------------------------------------------

namespace {

/* INFO: component -> family table. One entity may declare at most one
 *       component per family. Engine-owned: it is not read from content. */
const std::unordered_map<std::string, std::string>& ComponentFamilies() {
    static const std::unordered_map<std::string, std::string> k{
        {"visual.model", "visual"},      {"visual.shader", "visual"},
        {"visual.sprite", "visual"},     {"visual.emoji", "visual"},
        {"movement.arc", "movement"},    {"movement.linear", "movement"},
        {"movement.ballistic", "movement"},
        {"movement.static", "movement"}, {"motion.oscillate", "motion"},
        {"motion.jitter", "motion"},     {"motion.spin", "motion"},
        {"motion.spring", "motion"},     {"motion.follow", "motion"},
        {"emitter.particles", "emitter"}, {"audio.sound", "audio"},
        {"outcome.resolver", "outcome"}, {"camera.shake", "camera"},
        {"camera.zoom", "camera"},       {"camera.punch", "camera"},
        {"screen.effect", "screen"},     {"lifetime.ttl", "lifetime"},
        {"lifetime.untilDespawn", "lifetime"}};
    return k;
}

/* INFO: modifier kind -> merge mode table. Engine-owned, so a
 *       presentation never carries `mode` in content. */
const std::unordered_map<std::string, std::string>& ModifierMergeModes() {
    static const std::unordered_map<std::string, std::string> k{
        {"face.override", "override"},   {"material.param", "override"},
        {"screen.effect", "additive"},   {"particles.emitter", "additive"},
        {"audio.loop", "exclusive"}};
    return k;
}

/* INFO: the `match_event` wire types a presentation trigger may key on. The
 *       six core types mirror `frontend/src/lib/stores/matchEventMap.ts`; the
 *       seventh, `play_rejected`, is a real server-emitted match event
 *       (`match_instance.cpp`, asyncapi `x-packet-types`) so the shipped lsd
 *       sample can install on it. */
const std::set<std::string>& KnownMatchEventTypes() {
    static const std::set<std::string> k{
        "card_played", "auto_played", "cards_drawn", "reshuffle",
        "turn_advance", "status_applied", "play_rejected"};
    return k;
}

const std::set<std::string>& NoWhereFields() {
    static const std::set<std::string> k;
    return k;
}

/* INFO: payload fields a trigger `where` may test, per source. Derived from the
 *       asyncapi `*Payload` schemas. `status:<id>` sources evaluate against the
 *       `status_applied` payload. The field sets are authoritative: a phantom
 *       key (e.g. `reason` instead of `play_rejected`'s `reason_id`) is
 *       rejected rather than silently never firing. */
const std::set<std::string>& EventWhereFields(const std::string& type) {
    static const std::map<std::string, std::set<std::string>> k{
        {"card_played", {"player", "card", "from_zone_ordinal"}},
        {"auto_played", {"player", "card", "trigger_summary"}},
        {"cards_drawn",
         {"target", "player", "count", "hand_size_after", "from_debt",
          "source_pile", "cards"}},
        {"reshuffle", {"draw_size", "discard_size"}},
        {"turn_advance", {"from", "to", "direction", "deadline_ms"}},
        {"status_applied",
         {"target", "status_kind", "magnitude", "duration_unit",
          "instance_id"}},
        {"play_rejected", {"player", "reason_id"}}};
    auto it = k.find(type);
    return it == k.end() ? NoWhereFields() : it->second;
}

/* INFO: target grammar `hand|discard|table|card:<id>|seat:<user>|
 *       entity:<id>`. */
bool IsValidPresentationTarget(const std::string& target) {
    if (target == "hand" || target == "discard" || target == "table") {
        return true;
    }
    auto prefixed = [&target](const char* prefix) {
        const std::string p(prefix);
        return target.size() > p.size()
               && target.compare(0, p.size(), p) == 0;
    };
    return prefixed("card:") || prefixed("seat:") || prefixed("entity:");
}

/* INFO: a component that names an asset slot value. `slot` is the
 *       bundle slot name; `param` is the component param carrying the value. */
struct AssetRef {
    const char* slot;
    const char* param;
};

const AssetRef* AssetRefForComponent(const std::string& name) {
    static const std::map<std::string, AssetRef> k{
        {"audio.sound", {"sound", "clip"}},
        {"visual.shader", {"shader", "shader"}},
        {"screen.effect", {"shader", "shader"}},
        {"visual.sprite", {"sprite", "sprite"}},
        {"visual.model", {"model", "model"}}};
    auto it = k.find(name);
    return it == k.end() ? nullptr : &it->second;
}

struct Buckets {
    std::vector<LoadError> ids;
    std::vector<LoadError> refs;
    std::vector<LoadError> ops;
    std::vector<LoadError> graph;
    std::vector<LoadError> conflicts;
    std::vector<LoadError> deck;
    std::vector<LoadError> selector;
    std::vector<LoadError> tables;
    std::vector<LoadError> entities;
    std::vector<LoadError> events;
    std::vector<LoadError> streaks;
    std::vector<LoadError> derived;
    std::vector<LoadError> presentations;
    std::vector<LoadError> texts;
    std::vector<LoadWarning> warnings;
    std::vector<LoadError> outcome;
    std::vector<LoadError> asset;
};

std::string DeckPath(const DeckDef& deck);

struct GraphCtx {
    std::string ns;
    std::string artifact;
    std::string path;
    bool card_behavior = false;
    bool responder_ctx = false;
    bool allow_call_original = false;
    const std::set<std::string>* node_ids = nullptr;
};

enum class NodeType { kOp, kWindow, kBranch, kFork, kSchedule, kUnknown };

struct NodeInfo {
    std::string id;
    NodeType type = NodeType::kUnknown;
    nlohmann::json raw;
    std::vector<std::string> targets;
    bool responder = false;
};

class Checker {
   public:
    Checker(std::vector<const LoadedMod*> mods, bool match_set,
            const DeckDef* deck)
        : mods_(std::move(mods)), match_set_(match_set), deck_(deck) {}

    void Run() {
        CollectDeclarations();
        CheckIds();
        CheckTables();
        WalkAllGraphs();
        CheckConflicts();
        CheckDeck();
        CheckEntities();
        CheckEvents();
        CheckStreaks();
        CheckDerived();
        CheckPresentations();
        CheckBindings();
        CheckTexts();
        CheckOutcomeResolvers();
        CheckAssetDependencies();
    }

    const Buckets& buckets() const { return buckets_; }

   private:
    std::string ArtifactPathFor(const std::string& ns,
                                const std::string& filename) const {
        for (const LoadedMod* mod : mods_) {
            if (mod->manifest.id == ns) {
                if (mod->path.empty()) return filename;
                return mod->path + "/" + filename;
            }
        }
        return filename;
    }

    void CollectDeclarations() {
        for (const LoadedMod* mod : mods_) {
            const std::string& ns = mod->manifest.id;
            for (const auto& card : mod->cards) {
                kind_ids_.insert(card.kind_id);
                for (const auto& tag : card.tags) tags_.insert(tag);
                for (const auto& be : card.behaviors) {
                    CollectGraphDecls(be.graph, ns, "cards");
                }
            }
            for (const auto& status : mod->statuses) {
                status_ids_.insert(status.status_id);
            }
            for (const auto& rule : mod->rules) {
                rule_ids_.insert(rule.rule_id);
                for (const auto& hook : rule.hooks) {
                    CollectGraphDecls(hook.graph, ns, "rules");
                }
            }
            for (const auto& mut : mod->mutations) {
                CollectGraphDecls(mut.replacement, ns, "mutations");
                mutations_by_target_[mut.target].push_back(&mut);
            }
            for (const auto& table : mod->tables) {
                table_ids_.insert(table.table_id);
            }
            for (const auto& event : mod->events) {
                event_ids_.insert(event.event_id);
            }
            for (const auto& streak : mod->streaks) {
                streak_ids_.insert(streak.streak_id);
            }
        }
    }

    void CollectGraphDecls(const BehaviorGraph& graph,
                           const std::string& ns,
                           const std::string& artifact) {
        for (const auto& node : graph.nodes) {
            if (!node.is_object()) continue;
            if (!node.contains("op") || !node["op"].is_string()) continue;
            const std::string op = node["op"].get<std::string>();
            auto args = node.find("args");
            if (args == node.end() || !args->is_object()) continue;
            if (op == "add_restriction") {
                auto entry = args->find("entry_def");
                if (entry == args->end() || !entry->is_object()) continue;
                auto id = entry->find("id");
                if (id == entry->end() || !id->is_string()) continue;
                const std::string entry_id = id->get<std::string>();
                /* INFO: syntax is reported by CheckRestrictionEntry. */
                if (!IsValidKindId(entry_id)) continue;
                const std::string violation =
                    NamespaceRuleViolation(ns, entry_id);
                if (!violation.empty()) {
                    buckets_.ids.push_back(Err(
                        "id.namespace", artifact,
                        ArtifactPathFor(ns, artifact + ".json"), violation));
                }
                if (!restriction_ids_.insert(entry_id).second) {
                    buckets_.ids.push_back(Err(
                        "id.duplicate", artifact,
                        ArtifactPathFor(ns, artifact + ".json"),
                        "duplicate restriction entry id '" + entry_id + "'"));
                }
            } else if (op == "remove_restriction") {
                auto id = args->find("entry_id");
                if (id == args->end() || !id->is_string()) continue;
                removed_restrictions_.push_back(
                    {ns, NormalizeRef(id->get<std::string>(), ns)});
            }
        }
    }

    void CheckIds() {
        for (const LoadedMod* mod : mods_) {
            if (!IsValidLocalId(mod->manifest.id)) {
                buckets_.ids.push_back(Err("id.syntax", "mod", mod->path,
                                           "invalid mod id '" + mod->manifest.id
                                               + "'"));
            }
            std::set<std::string> seen;
            for (const auto& card : mod->cards) {
                if (!IsValidLocalId(card.id)) {
                    buckets_.ids.push_back(Err("id.syntax", "cards", mod->path,
                                               "invalid card id '" + card.id
                                                   + "'"));
                }
                if (!seen.insert(card.kind_id).second) {
                    buckets_.ids.push_back(Err(
                        "id.duplicate", "cards", mod->path,
                        "duplicate kind id '" + card.kind_id + "'"));
                }
            }
            for (const auto& rule : mod->rules) {
                if (!IsValidLocalId(rule.id)) {
                    buckets_.ids.push_back(Err("id.syntax", "rules", mod->path,
                                               "invalid rule id '" + rule.id
                                                   + "'"));
                }
            }
            for (const auto& status : mod->statuses) {
                if (!IsValidLocalId(status.id)) {
                    buckets_.ids.push_back(Err("id.syntax", "rules", mod->path,
                                               "invalid status id '" + status.id
                                                   + "'"));
                }
            }
            for (const auto& mut : mod->mutations) {
                if (!IsValidLocalId(mut.id)) {
                    buckets_.ids.push_back(Err(
                        "id.syntax", "mutations", mod->path,
                        "invalid mutation id '" + mut.id + "'"));
                }
            }
            for (const auto& decl : mod->manifest.settings) {
                if (!IsValidLocalId(decl.id)) {
                    buckets_.ids.push_back(Err(
                        "id.syntax", "mod", mod->path,
                        "invalid setting id '" + decl.id + "'"));
                }
            }
            for (const auto& table : mod->tables) {
                if (!IsValidLocalId(table.id)) {
                    buckets_.ids.push_back(Err(
                        "id.syntax", "tables", mod->path,
                        "invalid table id '" + table.id + "'"));
                }
            }
        }
    }

    /* INFO: outcome-table checks. Run after the reference bucket (S13);
     *       duplicate table ids are reported by the loader's scan. */
    void CheckTables() {
        for (const LoadedMod* mod : mods_) {
            const std::string& ns = mod->manifest.id;
            for (const auto& table : mod->tables) {
                const std::string path =
                    mod->path + "/tables/" + table.id + ".json";
                if (table.entries.empty()) {
                    buckets_.tables.push_back(Err(
                        "table.entry", "tables", path,
                        "table '" + table.table_id + "' has no entries"));
                    continue;
                }
                std::set<std::string> keys;
                for (const auto& entry : table.entries) {
                    if (!keys.insert(entry.key).second) {
                        buckets_.tables.push_back(Err(
                            "table.key.duplicate", "tables", path,
                            "table '" + table.table_id + "' declares key '"
                                + entry.key + "' twice"));
                    }
                    if (entry.weight < 1 || entry.weight > 1000000) {
                        buckets_.tables.push_back(Err(
                            "table.weight", "tables", path,
                            "table '" + table.table_id + "' entry '"
                                + entry.key + "' weight must be 1..1000000"));
                    }
                    if (entry.value.has_value() && entry.roll.has_value()) {
                        buckets_.tables.push_back(Err(
                            "table.value_roll", "tables", path,
                            "table '" + table.table_id + "' entry '"
                                + entry.key
                                + "' declares both 'value' and 'roll'"));
                    }
                    if (entry.roll.has_value()) {
                        CheckNestedRoll(*entry.roll, table, path);
                    }
                    if (entry.where.has_value()) {
                        if (!entry.where->is_object()) {
                            buckets_.tables.push_back(Err(
                                "table.condition", "tables", path,
                                "table '" + table.table_id + "' entry '"
                                    + entry.key + "' where must be an object"));
                        } else {
                            GraphCtx ctx{ns, "tables", path, false, false,
                                         false, nullptr};
                            CheckCondition(*entry.where, ctx);
                        }
                    }
                }
            }
        }
    }

    void CheckNestedRoll(const nlohmann::json& roll,
                         const TableDef& table,
                         const std::string& path) {
        auto bad = [&]() {
            buckets_.tables.push_back(Err(
                "table.roll", "tables", path,
                "table '" + table.table_id + "' has an invalid nested roll"));
        };
        if (!roll.is_object()) {
            bad();
            return;
        }
        auto sides = roll.find("sides");
        auto count = roll.find("count");
        if (sides == roll.end() || !sides->is_number_integer()
            || sides->get<long long>() < 1
            || sides->get<long long>() > 1000000
            || count == roll.end() || !count->is_number_integer()
            || count->get<long long>() < 1
            || count->get<long long>() > 1000) {
            bad();
            return;
        }
        auto keep = roll.find("keep");
        if (keep != roll.end()
            && (!keep->is_number_integer() || keep->get<long long>() < 1
                || keep->get<long long>() > count->get<long long>())) {
            bad();
        }
    }

    /* INFO: one component per family per entity; unknown component
     *       names and empty component sets are errors. */
    void CheckEntities() {
        for (const LoadedMod* mod : mods_) {
            for (const auto& entity : mod->entities) {
                const std::string path =
                    mod->path + "/entities/" + entity.id + ".json";
                if (entity.components.empty()) {
                    buckets_.entities.push_back(Err(
                        "entity.components.empty", "entities", path,
                        "entity '" + entity.entity_id
                            + "' declares no components"));
                }
                std::map<std::string, std::string> family_owner;
                for (const auto& component : entity.components) {
                    auto family = ComponentFamilies().find(component.name);
                    if (family == ComponentFamilies().end()) {
                        buckets_.entities.push_back(Err(
                            "entity.component.unknown", "entities", path,
                            "entity '" + entity.entity_id
                                + "' declares unknown component '"
                                + component.name + "'"));
                        continue;
                    }
                    auto inserted =
                        family_owner.emplace(family->second, component.name);
                    if (!inserted.second) {
                        buckets_.entities.push_back(Err(
                            "entity.component.family", "entities", path,
                            "entity '" + entity.entity_id + "' declares two '"
                                + family->second + "' components ('"
                                + inserted.first->second + "' and '"
                                + component.name + "')"));
                    }
                }
            }
        }
    }

    /* INFO: the namespace rule, unique ids and non-empty field names. The
     *       closed word lists are the schema's job (check 1). */
    void CheckEvents() {
        std::set<std::string> seen;
        for (const LoadedMod* mod : mods_) {
            for (const auto& event : mod->events) {
                const std::string path =
                    mod->path + "/events/" + event.id + ".json";
                const std::string violation =
                    NamespaceRuleViolation(mod->manifest.id, event.event_id);
                if (!violation.empty()) {
                    buckets_.events.push_back(
                        Err("event.id.namespace", "events", path, violation));
                }
                if (!seen.insert(event.event_id).second) {
                    buckets_.events.push_back(Err(
                        "event.id.duplicate", "events", path,
                        "duplicate event id '" + event.event_id + "'"));
                }
                for (const auto& field : event.fields) {
                    if ((field.name == event_keys::kTargetRole
                         || field.name == event_keys::kOwnerRole)
                        && field.type != event_keys::kSeatType) {
                        buckets_.events.push_back(Err(
                            "event.field.role_type", "events", path,
                            "event '" + event.event_id + "' field '"
                                + field.name
                                + "' names a seat role and must be a seat"));
                    }
                    if (IsValidLocalId(field.name)) continue;
                    buckets_.events.push_back(Err(
                        "event.field.name", "events", path,
                        "event '" + event.event_id + "' field '" + field.name
                            + "' must match [a-z0-9_]+ (1-32)"));
                }
            }
        }
    }

    /* INFO: the result of looking up one payload field of a client event. */
    enum class FieldLookup { kDeferred, kNoEvent, kNoField, kHidden, kPublic };

    const EventDef* FindModEvent(const std::string& event_id) const {
        for (const LoadedMod* mod : mods_) {
            for (const EventDef& event : mod->events) {
                if (event.event_id == event_id) return &event;
            }
        }
        return nullptr;
    }

    bool ModInSet(const std::string& mod_id) const {
        for (const LoadedMod* mod : mods_) {
            if (mod->manifest.id == mod_id) return true;
        }
        return false;
    }

    /* INFO: an empty `field` asks for the event only. A reference to a mod
     *       that is not in the set is deferred while one mod is validated
     *       alone, and unknown once the match set is complete. */
    FieldLookup LookupEventField(const std::string& event_id,
                                 const std::string& field) const {
        const std::size_t colon = event_id.find(':');
        if (colon == std::string::npos) {
            if (!match::view::IsStreakFeedable(event_id)) {
                return FieldLookup::kNoEvent;
            }
            if (field.empty()) return FieldLookup::kPublic;
            const match::view::EventRule* rule =
                match::view::FindEventRule(event_id);
            const match::view::FieldRule* declared =
                rule == nullptr ? nullptr
                                : match::view::FindFieldRule(*rule, field);
            if (declared == nullptr) return FieldLookup::kNoField;
            return declared->audience == match::view::Audience::kAll
                       ? FieldLookup::kPublic
                       : FieldLookup::kHidden;
        }
        const EventDef* definition = FindModEvent(event_id);
        if (definition == nullptr) {
            const bool owner_known = ModInSet(event_id.substr(0, colon));
            return (owner_known || match_set_) ? FieldLookup::kNoEvent
                                               : FieldLookup::kDeferred;
        }
        if (field.empty()) return FieldLookup::kPublic;
        for (const EventFieldDef& declared : definition->fields) {
            if (declared.name != field) continue;
            return declared.audience == "all" ? FieldLookup::kPublic
                                              : FieldLookup::kHidden;
        }
        return FieldLookup::kNoField;
    }

    /* INFO: who owns a matcher in a message: a streak or a derived event.
     *       The two kinds share the matcher checks and report to their own
     *       stage. */
    struct MatcherOwner {
        std::string streak_id;
        bool derived = false;
        bool binding = false;
    };

    static MatcherOwner OwnerOf(const StreakDef& streak) {
        return MatcherOwner{streak.streak_id, false};
    }

    std::vector<LoadError>& Bucket(const MatcherOwner& owner) {
        if (owner.binding) return buckets_.presentations;
        return owner.derived ? buckets_.derived : buckets_.streaks;
    }

    static std::string CheckId(const MatcherOwner& owner,
                               const std::string& name) {
        if (owner.binding) return "binding." + name;
        return (owner.derived ? "derived." : "streak.") + name;
    }

    static const char* StageOf(const MatcherOwner& owner) {
        if (owner.binding) return "presentations";
        return owner.derived ? "derived" : "streaks";
    }

    static std::string Label(const MatcherOwner& owner) {
        if (owner.binding) return "binding";
        return owner.derived ? "derived event" : "streak";
    }

    void CheckStreakField(const std::string& path, const MatcherOwner& streak,
                          const std::string& event_id,
                          const std::string& field, const char* role) {
        switch (LookupEventField(event_id, field)) {
            case FieldLookup::kNoField:
                Bucket(streak).push_back(Err(
                    CheckId(streak, "field.unknown"), StageOf(streak), path,
                    Label(streak) + " '" + streak.streak_id + "' " + role + " '" + field
                        + "' is not a payload field of '" + event_id + "'"));
                break;
            case FieldLookup::kHidden:
                Bucket(streak).push_back(Err(
                    CheckId(streak, "field.audience"), StageOf(streak), path,
                    Label(streak) + " '" + streak.streak_id + "' " + role + " '" + field
                        + "' of '" + event_id
                        + "' does not have the audience 'all'; a streak "
                          "value is public"));
                break;
            default:
                break;
        }
    }

    bool TagExists(const std::string& tag) const {
        return tags_.count(tag) != 0;
    }

    void CheckStreakTag(const std::string& path, const MatcherOwner& streak,
                        const std::string& tag) {
        const std::size_t colon = tag.find(':');
        const bool well_formed =
            colon != std::string::npos && colon > 0
            && IsValidLocalId(tag.substr(0, colon))
            && IsValidLocalId(tag.substr(colon + 1));
        if (!well_formed) {
            Bucket(streak).push_back(Err(
                CheckId(streak, "tag.syntax"), StageOf(streak), path,
                Label(streak) + " '" + streak.streak_id + "' tag '" + tag
                    + "' must be 'namespace:name'"));
            return;
        }
        const bool owner_known = ModInSet(tag.substr(0, colon));
        if ((owner_known || match_set_) && !TagExists(tag)) {
            Bucket(streak).push_back(Err(
                CheckId(streak, "tag.unknown"), StageOf(streak), path,
                Label(streak) + " '" + streak.streak_id + "' tag '" + tag
                    + "' is carried by no card kind"));
        }
    }

    void CheckStreakMatcher(const std::string& path, const MatcherOwner& streak,
                            const StreakMatcherDef& matcher) {
        const FieldLookup event = LookupEventField(matcher.event, "");
        if (event == FieldLookup::kNoEvent) {
            Bucket(streak).push_back(Err(
                CheckId(streak, "event.unknown"), StageOf(streak), path,
                Label(streak) + " '" + streak.streak_id + "' matches '" + matcher.event
                    + "', which is no client event that a streak may read"));
            return;
        }
        for (const StreakTestDef& test : matcher.tests) {
            namespace keys = streak_keys;
            const bool known =
                std::find(keys::kMatcherTests.begin(),
                          keys::kMatcherTests.end(), test.test)
                != keys::kMatcherTests.end();
            if (!known) {
                Bucket(streak).push_back(Err(
                    CheckId(streak, "test.unknown"), StageOf(streak), path,
                    Label(streak) + " '" + streak.streak_id + "' uses the unknown test '"
                        + test.test + "'"));
                continue;
            }
            if (test.test == keys::kRelationTest) {
                Bucket(streak).push_back(Err(
                    CheckId(streak, "test.relation"), StageOf(streak), path,
                    Label(streak) + " '" + streak.streak_id
                        + "' uses 'relation'; it depends on the viewer and "
                          "is for presentations only"));
                continue;
            }
            if (test.test == keys::kHasTag) {
                CheckStreakField(path, streak, matcher.event,
                                 std::string(keys::kTagsField), "tag source");
                CheckStreakTag(path, streak, test.tag);
                continue;
            }
            if (test.field.empty() == test.state.empty()) {
                Bucket(streak).push_back(Err(
                    CheckId(streak, "test.shape"), StageOf(streak), path,
                    Label(streak) + " '" + streak.streak_id + "' test '" + test.test
                        + "' needs exactly one of a field and a state value"));
                continue;
            }
            if (!test.state.empty()) {
                if (!match::streak::IsStateValueName(test.state)) {
                    Bucket(streak).push_back(Err(
                        CheckId(streak, "state.unknown"), StageOf(streak), path,
                        Label(streak) + " '" + streak.streak_id
                            + "' reads the unknown state value '" + test.state
                            + "'"));
                }
                continue;
            }
            CheckStreakField(path, streak, matcher.event, test.field,
                             "test field");
        }
    }

    /* INFO: the streak stage: ids, type, matcher, field audiences, and the
     *       resolution of the `uni:context` names of every event. */
    void CheckStreaks() {
        std::set<std::string> seen;
        for (const LoadedMod* mod : mods_) {
            for (const StreakDef& streak : mod->streaks) {
                const std::string path =
                    mod->path + "/streaks/" + streak.id + ".json";
                const std::string violation =
                    NamespaceRuleViolation(mod->manifest.id, streak.streak_id);
                if (!violation.empty()) {
                    buckets_.streaks.push_back(Err(
                        "streak.id.namespace", "streaks", path, violation));
                }
                if (!seen.insert(streak.streak_id).second) {
                    buckets_.streaks.push_back(Err(
                        "streak.id.duplicate", "streaks", path,
                        "duplicate streak id '" + streak.streak_id + "'"));
                }
                CheckStreakDefinition(path, streak);
            }
            for (const EventDef& event : mod->events) {
                for (const std::string& name : event.context) {
                    CheckContextName(
                        mod->path + "/events/" + event.id + ".json", event,
                        name);
                }
            }
        }
    }

    /* INFO: the derived stage: ids, the emitted core event, the base events
     *       and fields that the matchers read, and the window of a block.
     *       The matcher checks are the streak checks; they report to this
     *       stage for a derived event. */
    void CheckDerived() {
        std::set<std::string> seen;
        std::set<std::string> emitted;
        std::map<std::string, const DerivedEventDef*> windows;
        for (const LoadedMod* mod : mods_) {
            for (const DerivedEventDef& derived : mod->derived_events) {
                if (derived.kind == derived_keys::kWindowKind) {
                    windows[derived.derived_id] = &derived;
                }
            }
        }
        for (const LoadedMod* mod : mods_) {
            for (const DerivedEventDef& derived : mod->derived_events) {
                const std::string path =
                    mod->path + "/derived/" + derived.id + ".json";
                if (!seen.insert(derived.derived_id).second) {
                    buckets_.derived.push_back(Err(
                        "derived.id.duplicate", "derived", path,
                        "duplicate derived event id '" + derived.derived_id
                            + "'"));
                }
                if (!match::view::IsDerivedEvent(derived.emits)) {
                    buckets_.derived.push_back(Err(
                        "derived.emits.unknown", "derived", path,
                        "derived event '" + derived.derived_id + "' emits '"
                            + derived.emits
                            + "', which is no derived core event"));
                } else if (!emitted.insert(derived.emits).second) {
                    buckets_.derived.push_back(Err(
                        "derived.emits.duplicate", "derived", path,
                        "derived event '" + derived.derived_id + "' emits '"
                            + derived.emits
                            + "', which another definition emits too"));
                }
                if (derived.kind == derived_keys::kBlockKind) {
                    const auto window = windows.find(derived.window_of);
                    if (window == windows.end()) {
                        buckets_.derived.push_back(Err(
                            "derived.window.unknown", "derived", path,
                            "derived event '" + derived.derived_id
                                + "' names the window '" + derived.window_of
                                + "', which no loaded mod defines as a "
                                  "window"));
                    }
                }
                const MatcherOwner owner{derived.derived_id, true};
                for (const auto* list :
                     {&derived.open_on, &derived.close_on,
                      &derived.block_on}) {
                    for (const DerivedSeatMatcherDef& seat_matcher : *list) {
                        CheckStreakMatcher(path, owner, seat_matcher.matcher);
                        CheckStreakField(path, owner,
                                         seat_matcher.matcher.event,
                                         seat_matcher.seat_field,
                                         "seat field");
                    }
                }
            }
        }
    }

    void CheckStreakDefinition(const std::string& path,
                               const StreakDef& streak) {
        namespace keys = streak_keys;
        const bool known_type =
            std::find(keys::kStreakTypes.begin(), keys::kStreakTypes.end(),
                      streak.type)
            != keys::kStreakTypes.end();
        if (!known_type) {
            buckets_.streaks.push_back(Err(
                "streak.type.unknown", "streaks", path,
                "streak '" + streak.streak_id + "' has the unknown type '"
                    + streak.type + "'"));
        }
        if (streak.type == keys::kLastValue && streak.value_field.empty()) {
            buckets_.streaks.push_back(Err(
                "streak.value_field.missing", "streaks", path,
                "streak '" + streak.streak_id
                    + "' has the type last_value and needs a value field"));
        }
        if (streak.type != keys::kLastValue && !streak.value_field.empty()) {
            buckets_.streaks.push_back(Err(
                "streak.value_field.unexpected", "streaks", path,
                "streak '" + streak.streak_id
                    + "' sets a value field, but only last_value reads one"));
        }
        if (streak.type != keys::kRun && !streak.reset_on.empty()) {
            buckets_.streaks.push_back(Err(
                "streak.reset_on.unexpected", "streaks", path,
                "streak '" + streak.streak_id
                    + "' sets reset events, but only a run resets"));
        }
        CheckStreakMatcher(path, OwnerOf(streak), streak.matcher);
        if (!streak.group_by.empty()) {
            CheckStreakField(path, OwnerOf(streak), streak.matcher.event,
                             streak.group_by, "group field");
        }
        if (!streak.value_field.empty()) {
            CheckStreakField(path, OwnerOf(streak), streak.matcher.event,
                             streak.value_field, "value field");
        }
        for (const StreakMatcherDef& reset : streak.reset_on) {
            CheckStreakMatcher(path, OwnerOf(streak), reset);
        }
    }

    void CheckContextName(const std::string& path, const EventDef& event,
                          const std::string& name) {
        if (streak_ids_.count(name) != 0) return;
        const std::size_t colon = name.find(':');
        const bool owner_known =
            colon != std::string::npos && ModInSet(name.substr(0, colon));
        if (!owner_known && !match_set_) return;
        buckets_.streaks.push_back(Err(
            "streak.context.unresolved", "events", path,
            "event '" + event.event_id + "' asks for the context '" + name
                + "', which no loaded mod defines as a streak"));
    }

    /* INFO: the result of looking up one payload field of a bound event, as
     *       the viewer receives it. */
    enum class BoundField { kDeferred, kNoEvent, kNoField, kNone, kPartial,
                            kPublic };

    BoundField LookupBoundField(const std::string& event_id,
                                const std::string& field) const {
        using match::view::Audience;
        if (event_id.find(':') == std::string::npos) {
            const match::view::EventRule* rule =
                match::view::FindEventRule(event_id);
            if (rule == nullptr) return BoundField::kNoEvent;
            if (field.empty()) return BoundField::kPublic;
            const match::view::FieldRule* declared =
                match::view::FindFieldRule(*rule, field);
            if (declared == nullptr) return BoundField::kNoField;
            if (declared->audience == Audience::kAll) {
                return BoundField::kPublic;
            }
            return declared->audience == Audience::kNone
                       ? BoundField::kNone
                       : BoundField::kPartial;
        }
        const EventDef* definition = FindModEvent(event_id);
        if (definition == nullptr) {
            const std::string owner = event_id.substr(0, event_id.find(':'));
            return (ModInSet(owner) || match_set_) ? BoundField::kNoEvent
                                                   : BoundField::kDeferred;
        }
        if (field.empty()) return BoundField::kPublic;
        for (const EventFieldDef& declared : definition->fields) {
            if (declared.name != field) continue;
            if (declared.audience == "all") return BoundField::kPublic;
            return declared.audience == "none" ? BoundField::kNone
                                               : BoundField::kPartial;
        }
        return BoundField::kNoField;
    }

    static MatcherOwner OwnerOf(const BindingDef& binding) {
        return MatcherOwner{binding.binding_id, false, true};
    }

    /* INFO: the matcher runs on the client, on the event as the server
     *       projected it. A field that no viewer receives cannot be read. A
     *       field that some viewers receive can be absent. */
    void CheckBoundField(const std::string& path, const BindingDef& binding,
                         const std::string& field, const std::string& role) {
        CheckBoundFieldOf(path, binding, binding.event, field, role);
    }

    void CheckBoundFieldOf(const std::string& path, const BindingDef& binding,
                           const std::string& event_id,
                           const std::string& field, const std::string& role) {
        const std::string subject = "binding '" + binding.binding_id + "' "
                                    + role + " '" + field + "' of '"
                                    + event_id + "'";
        switch (LookupBoundField(event_id, field)) {
            case BoundField::kNoField:
                buckets_.presentations.push_back(Err(
                    "binding.field.unknown", "presentations", path,
                    subject + " is not a payload field"));
                break;
            case BoundField::kNone:
                buckets_.presentations.push_back(Err(
                    "binding.field.audience", "presentations", path,
                    subject + " has the audience 'none'; no viewer "
                              "receives it"));
                break;
            case BoundField::kPartial:
                buckets_.warnings.push_back(LoadWarning{
                    "binding.field.audience.partial", "presentations", path,
                    subject + " does not have the audience 'all'; it is "
                              "absent for some viewers"});
                break;
            default:
                break;
        }
    }

    void CheckBindingRelation(const std::string& path,
                              const BindingDef& binding,
                              const StreakTestDef& test) {
        namespace keys = binding_keys;
        const std::string label = "binding '" + binding.binding_id + "'";
        const bool role_ok =
            test.role == keys::kActorRole || test.role == keys::kTargetRole;
        if (!role_ok) {
            buckets_.presentations.push_back(Err(
                "binding.relation.role", "presentations", path,
                label + " relation needs the role 'actor' or 'target'"));
        }
        if (test.relation != keys::kSelf && test.relation != keys::kOther) {
            buckets_.presentations.push_back(Err(
                "binding.relation.value", "presentations", path,
                label + " relation needs the value 'self' or 'other'"));
        }
        if (test.role != keys::kTargetRole) return;
        const BoundField target =
            LookupBoundField(binding.event, std::string(keys::kTargetRole));
        if (target == BoundField::kNoField) {
            buckets_.presentations.push_back(Err(
                "binding.relation.target", "presentations", path,
                label + " relation uses the role 'target', but '"
                    + binding.event + "' has no payload field 'target'"));
            return;
        }
        CheckBoundField(path, binding, std::string(keys::kTargetRole),
                        "relation role");
    }

    void CheckBindingTest(const std::string& path, const BindingDef& binding,
                          const StreakTestDef& test) {
        namespace keys = streak_keys;
        namespace bkeys = binding_keys;
        const std::string label = "binding '" + binding.binding_id + "'";
        const bool known =
            std::find(bkeys::kBindingTests.begin(), bkeys::kBindingTests.end(),
                      test.test) != bkeys::kBindingTests.end();
        if (!known) {
            buckets_.presentations.push_back(Err(
                "binding.test.unknown", "presentations", path,
                label + " uses the unknown test '" + test.test + "'"));
            return;
        }
        const bool reads_streak = !test.streak.empty() || !test.key.empty()
                                  || !test.key_field.empty();
        if (!test.tag.empty() && test.test != keys::kHasTag
            && test.test != bkeys::kCausedBy) {
            buckets_.presentations.push_back(Err(
                "binding.test.shape", "presentations", path,
                label + " test '" + test.test
                    + "' has a tag; only has_tag and caused_by use one"));
            return;
        }
        if (test.actor) {
            const bool word_fits = test.test == keys::kExists
                                   || test.test == keys::kEquals
                                   || test.test == keys::kIn;
            if (!word_fits || reads_streak || !test.field.empty()
                || !test.state.empty()
                || (test.test == keys::kIn && test.values.empty())) {
                buckets_.presentations.push_back(Err(
                    "binding.test.shape", "presentations", path,
                    label + " test '" + test.test
                        + "' has a bad actor source: only exists, equals "
                          "and in can read the actor, with no field, "
                          "streak, key or state; 'in' needs values"));
            }
            return;
        }
        if (test.test == bkeys::kCausedBy) {
            const bool shaped =
                test.value.is_string() && !test.value.get<std::string>().empty()
                && LookupBoundField(test.value.get<std::string>(), "")
                       != BoundField::kNoEvent;
            if (!shaped || reads_streak || !test.field.empty()
                || !test.state.empty()) {
                buckets_.presentations.push_back(Err(
                    "binding.test.shape", "presentations", path,
                    label + " test 'caused_by' needs the id of a known "
                            "event as its value, and no field, streak, "
                            "key or state"));
            }
            return;
        }
        if (test.test == keys::kRelationTest || test.test == keys::kHasTag) {
            if (reads_streak) {
                buckets_.presentations.push_back(Err(
                    "binding.test.shape", "presentations", path,
                    label + " test '" + test.test
                        + "' cannot read a streak or use key and "
                          "key_field"));
                return;
            }
        }
        if (test.test == keys::kRelationTest) {
            CheckBindingRelation(path, binding, test);
            return;
        }
        if (test.test == keys::kHasTag) {
            CheckBoundField(path, binding, std::string(keys::kTagsField),
                            "tag source");
            CheckStreakTag(path, OwnerOf(binding), test.tag);
            return;
        }
        const bool numeric = test.test == bkeys::kGt || test.test == bkeys::kGte
                             || test.test == bkeys::kLt
                             || test.test == bkeys::kLte;
        const bool from_streak = !test.streak.empty();
        if ((numeric && !test.value.is_number())
            || (from_streak && (!test.field.empty() || !IsLogKey(test.streak)))
            || (!test.key.empty() && !test.key_field.empty())
            || (!from_streak && (!test.key.empty() || !test.key_field.empty()))) {
            buckets_.presentations.push_back(Err(
                "binding.test.shape", "presentations", path,
                label + " test '" + test.test
                    + "' has a bad source: numeric words need a number "
                      "value; a streak test has no payload field; key and "
                      "key_field are for streak tests and exclusive"));
            return;
        }
        if (test.test == keys::kIn && test.values.empty()) {
            buckets_.presentations.push_back(Err(
                "binding.test.shape", "presentations", path,
                label + " test 'in' needs a non-empty list of values"));
            return;
        }
        if (!test.key_field.empty()) {
            CheckBoundField(path, binding, test.key_field,
                            "streak key field");
        }
        if (from_streak) return;
        if (test.field.empty() || !test.state.empty()) {
            buckets_.presentations.push_back(Err(
                "binding.test.shape", "presentations", path,
                label + " test '" + test.test
                    + "' needs a payload field; state values are for "
                      "streaks"));
            return;
        }
        CheckBoundField(path, binding, test.field, "test field");
    }

    void CheckBindingBeats(const std::string& path, const BindingDef& binding,
                           const BindingCaseDef& binding_case) {
        namespace keys = binding_keys;
        const std::string label = "binding '" + binding.binding_id + "'";
        for (const nlohmann::json& beat : binding_case.beats) {
            if (!beat.is_array() || beat.empty()) {
                buckets_.presentations.push_back(Err(
                    "binding.beat.shape", "presentations", path,
                    label + " has a beat that is not a non-empty list of "
                            "steps"));
                continue;
            }
            for (const nlohmann::json& step : beat) {
                const bool shaped =
                    step.is_object() && step.contains("op")
                    && step["op"].is_string() && step.contains("target")
                    && step["target"].is_string();
                if (!shaped) {
                    buckets_.presentations.push_back(Err(
                        "binding.beat.shape", "presentations", path,
                        label + " has a step without a string 'op' and "
                                "'target'"));
                    continue;
                }
                const std::string op = step["op"].get<std::string>();
                if (std::find(keys::kBeatOps.begin(), keys::kBeatOps.end(),
                              op) == keys::kBeatOps.end()) {
                    buckets_.presentations.push_back(Err(
                        "binding.beat.op", "presentations", path,
                        label + " uses the unknown beat op '" + op + "'"));
                }
                const std::string target = step["target"].get<std::string>();
                if (target.rfind(keys::kPayloadTargetPrefix, 0) == 0) {
                    CheckBoundField(
                        path, binding,
                        target.substr(keys::kPayloadTargetPrefix.size()),
                        "step target");
                }
            }
        }
    }

    /* INFO: the sound values of the `sound` slots that the mod ships. */
    static std::set<std::string> SoundValuesOf(const LoadedMod& mod) {
        std::set<std::string> values;
        for (const auto& bundle : mod.assets) {
            for (const auto& slot : bundle.slots) {
                if (slot.name != "sound") continue;
                for (const auto& variant : slot.variants) {
                    if (variant.value.has_value()) {
                        values.insert(*variant.value);
                    }
                    if (!variant.file.has_value()) continue;
                    const std::string& file = *variant.file;
                    values.insert(file);
                    const auto slash = file.find_last_of('/');
                    const std::string base = slash == std::string::npos
                                                 ? file
                                                 : file.substr(slash + 1);
                    values.insert(base);
                    const auto dot = base.find_last_of('.');
                    if (dot != std::string::npos) {
                        values.insert(base.substr(0, dot));
                    }
                }
            }
        }
        return values;
    }

    void CheckBindingSounds(const std::string& path, const LoadedMod& mod,
                            const BindingDef& binding,
                            const BindingCaseDef& binding_case) {
        namespace keys = binding_keys;
        const std::set<std::string> own_sounds = SoundValuesOf(mod);
        for (const BindingSoundDef& sound : binding_case.sounds) {
            const bool core =
                std::find(keys::kCoreSoundIds.begin(),
                          keys::kCoreSoundIds.end(), sound.sound)
                != keys::kCoreSoundIds.end();
            if (core || own_sounds.count(sound.sound) != 0) continue;
            buckets_.presentations.push_back(Err(
                "binding.sound.unknown", "presentations", path,
                "binding '" + binding.binding_id + "' uses the sound '"
                    + sound.sound
                    + "', which is no core sound and no sound value of an "
                      "asset bundle of this mod"));
        }
    }

    static bool IsLogKey(const std::string& key) {
        if (key.empty() || key.size() > 64) return false;
        return std::all_of(key.begin(), key.end(), [](unsigned char c) {
            return std::isalnum(c) != 0 || c == '_' || c == '.' || c == ':'
                   || c == '-';
        });
    }

    static bool IsGlossaryKey(const std::string& key) {
        const std::size_t colon = key.find(':');
        if (colon == std::string::npos) return IsValidLocalId(key);
        return IsValidLocalId(key.substr(0, colon))
               && IsValidLocalId(key.substr(colon + 1));
    }

    void CheckBindingLog(const std::string& path, const BindingDef& binding,
                         const BindingLogDef& log,
                         const std::vector<StreakTestDef>& tests) {
        const auto cause_test = std::find_if(
            tests.begin(), tests.end(), [](const StreakTestDef& test) {
                return test.test == binding_keys::kCausedBy;
            });
        const std::string label = "binding '" + binding.binding_id + "'";
        std::set<std::string> seen;
        bool keys_ok = !log.variants.empty()
                       && log.variants.size() <= binding_keys::kMaxVariants;
        for (const std::string& key : log.variants) {
            keys_ok = keys_ok && IsLogKey(key) && seen.insert(key).second;
        }
        if (!keys_ok) {
            buckets_.presentations.push_back(Err(
                "binding.log.key", "presentations", path,
                label + " log line needs 1 to "
                    + std::to_string(binding_keys::kMaxVariants)
                    + " distinct text keys of [A-Za-z0-9_.:-] (1-64)"));
        }
        for (const auto& [name, param] : log.params) {
            const int sources = (param.field.empty() ? 0 : 1)
                                + (param.value.has_value() ? 1 : 0)
                                + (param.actor ? 1 : 0)
                                + (param.streak.empty() ? 0 : 1);
            const bool key_misused =
                (param.streak.empty()
                 && (!param.key.empty() || !param.key_field.empty()))
                || (!param.key.empty() && !param.key_field.empty());
            if (key_misused) {
                buckets_.presentations.push_back(Err(
                    "binding.log.param", "presentations", path,
                    label + " param '" + name
                        + "': key and key_field need a streak source and "
                          "are exclusive"));
                continue;
            }
            if (param.default_value.has_value()
                && !param.default_value->is_string()
                && !param.default_value->is_number()) {
                buckets_.presentations.push_back(Err(
                    "binding.log.param", "presentations", path,
                    label + " log parameter '" + name
                        + "': default must be a string or a number"));
                continue;
            }
            if (!param.card_part.empty()
                && (param.field.empty() || sources != 1
                    || (param.card_part != binding_keys::kCardPartKind
                        && param.card_part != binding_keys::kCardPartColor))) {
                buckets_.presentations.push_back(Err(
                    "binding.log.param", "presentations", path,
                    label + " log parameter '" + name
                        + "': card_part needs a field source and must be "
                          "'kind' or 'color'"));
                continue;
            }
            if (param.cause
                && (param.field.empty() || sources != 1)) {
                buckets_.presentations.push_back(Err(
                    "binding.log.param", "presentations", path,
                    label + " log parameter '" + name
                        + "': cause needs a field source"));
                continue;
            }
            if (param.cause && cause_test == tests.end()) {
                buckets_.presentations.push_back(Err(
                    "binding.log.param", "presentations", path,
                    label + " log parameter '" + name
                        + "': a cause param needs a caused_by test in the "
                          "case"));
                continue;
            }
            if (sources != 1) {
                buckets_.presentations.push_back(Err(
                    "binding.log.param", "presentations", path,
                    label + " log parameter '" + name
                        + "' needs exactly one source: a field, a value, "
                          "the actor, or a streak"));
                continue;
            }
            if (!param.streak.empty() && !IsLogKey(param.streak)) {
                buckets_.presentations.push_back(Err(
                    "binding.log.param", "presentations", path,
                    label + " log parameter '" + name
                        + "' has a streak id outside [A-Za-z0-9_.:-] "
                          "(1-64)"));
                continue;
            }
            if (!param.key_field.empty()) {
                CheckBoundField(path, binding, param.key_field,
                                "streak key field");
            }
            if (param.cause) {
                /* INFO: a bad caused_by value has its own shape error. */
                if (!cause_test->value.is_string()) continue;
                CheckBoundFieldOf(path, binding,
                                  cause_test->value.get<std::string>(),
                                  param.field, "log parameter cause field");
            } else if (!param.field.empty()) {
                CheckBoundField(path, binding, param.field,
                                "log parameter field");
            }
        }
    }

    void CheckBindingCase(const std::string& path, const LoadedMod& mod,
                          const BindingDef& binding,
                          const BindingCaseDef& binding_case) {
        for (const StreakTestDef& test : binding_case.tests) {
            CheckBindingTest(path, binding, test);
        }
        CheckBindingBeats(path, binding, binding_case);
        CheckBindingSounds(path, mod, binding, binding_case);
        if (binding_case.log.has_value()) {
            CheckBindingLog(path, binding, *binding_case.log,
                            binding_case.tests);
        }
        if (!binding_case.glossary.empty()
            && !IsGlossaryKey(binding_case.glossary)) {
            buckets_.presentations.push_back(Err(
                "binding.glossary.key", "presentations", path,
                "binding '" + binding.binding_id + "' glossary key '"
                    + binding_case.glossary + "' must be 'namespace:name'"));
        }
        if (binding_case.beats.empty() && binding_case.sounds.empty()
            && !binding_case.log.has_value()
            && binding_case.glossary.empty()) {
            buckets_.presentations.push_back(Err(
                "binding.empty", "presentations", path,
                "binding '" + binding.binding_id
                    + "' has a case that presents nothing"));
        }
    }

    /* INFO: the keys a mod uses by its own files, in its own namespace.
     *       A key without the namespace of the mod belongs to the core
     *       catalog of the interface; it is not checked here.
     *       The keys of the glossary entries join the used keys. */
    static std::set<std::string> UsedTextKeys(const LoadedMod& mod) {
        const std::string prefix = mod.manifest.id + ":";
        std::set<std::string> used;
        for (const BindingDef& binding : mod.bindings) {
            for (const BindingCaseDef& binding_case : binding.cases) {
                if (!binding_case.log.has_value()) continue;
                for (const std::string& key : binding_case.log->variants) {
                    if (key.rfind(prefix, 0) == 0) used.insert(key);
                }
            }
        }
        for (const std::string& key : UsedGlossaryKeys(mod)) {
            if (IsValidTextKey(key) && key.rfind(prefix, 0) == 0) {
                used.insert(key);
            }
        }
        return used;
    }

    void CheckTextsFile(const LoadedMod& mod, const TextsFileDef& file) {
        const std::string label = "texts file '" + file.locale + "'";
        if (!IsValidLocaleTag(file.locale)) {
            const bool reserved = file.locale == kPseudoLocale;
            buckets_.texts.push_back(Err(
                "texts.locale", "texts", file.path,
                label
                    + (reserved
                           ? " is the run-time locale; the client makes it "
                             "from 'en', so a mod does not ship it"
                           : " is no locale tag (a language of 2 or 3 "
                             "lowercase letters, then subtags)")));
        }
        if (file.size > texts_limits::kMaxTextsFileBytes) {
            buckets_.texts.push_back(Err(
                "texts.size", "texts", file.path,
                label + " is " + std::to_string(file.size)
                    + " bytes; the limit is "
                    + std::to_string(texts_limits::kMaxTextsFileBytes)));
            return;
        }
        if (!file.read_error.empty() || !file.content.is_object()) {
            buckets_.texts.push_back(Err(
                "texts.json", "texts", file.path,
                label + ": "
                    + (file.read_error.empty()
                           ? std::string("the top level must be an object")
                           : file.read_error)));
            return;
        }
        if (file.content.size() > texts_limits::kMaxTextKeys) {
            buckets_.texts.push_back(Err(
                "texts.keys", "texts", file.path,
                label + " has " + std::to_string(file.content.size())
                    + " keys; the limit is "
                    + std::to_string(texts_limits::kMaxTextKeys)));
            return;
        }
        const std::string prefix = mod.manifest.id + ":";
        for (auto it = file.content.begin(); it != file.content.end(); ++it) {
            const std::string& key = it.key();
            if (key.size() <= prefix.size() || key.rfind(prefix, 0) != 0) {
                buckets_.texts.push_back(Err(
                    "texts.key.namespace", "texts", file.path,
                    label + " key '" + key + "' is outside the namespace '"
                        + prefix + "' of the mod; only the mod '"
                        + std::string(kBaseModId) + "' defines '"
                        + std::string(kBaseModId) + ":' keys"));
            }
            if (!it.value().is_string()) {
                buckets_.texts.push_back(Err(
                    "texts.value", "texts", file.path,
                    label + " key '" + key + "' must have a string value"));
            } else if (it.value().get_ref<const std::string&>().size()
                       > texts_limits::kMaxTextValueBytes) {
                buckets_.texts.push_back(Err(
                    "texts.value", "texts", file.path,
                    label + " key '" + key + "' is longer than "
                        + std::to_string(texts_limits::kMaxTextValueBytes)
                        + " bytes"));
            }
        }
    }

    void CheckTexts() {
        for (const LoadedMod* mod : mods_) {
            for (const TextsFileDef& file : mod->texts) {
                CheckTextsFile(*mod, file);
            }
            CheckGlossaryKeys(*mod);
            CheckUsedTextKeys(*mod);
        }
    }

    void CheckGlossaryKey(const LoadedMod& mod, const std::string& path,
                          const std::string& owner, const std::string& field,
                          const std::string& key) {
        if (!IsValidTextKey(key)) {
            buckets_.texts.push_back(Err(
                "glossary.key.syntax", "glossary", path,
                owner + " " + field + " is no text key: a key is "
                    "'namespace:word' with words of [a-z0-9_] split by dots, "
                    "at most "
                    + std::to_string(glossary_keys::kMaxKeyBytes)
                    + " bytes; an entry holds keys, never texts"));
            return;
        }
        const std::string prefix = mod.manifest.id + ":";
        if (key.rfind(prefix, 0) != 0) {
            buckets_.texts.push_back(Err(
                "glossary.key.namespace", "glossary", path,
                owner + " " + field + " '" + key
                    + "' is outside the namespace '" + prefix
                    + "' of the mod"));
        }
    }

    /* INFO: an explicit entry and a card with declared keys must hold text
     *       keys of the own namespace. A card that an explicit entry replaces
     *       is not checked: its keys are not used. A derived key that is
     *       missing in `en` only warns. */
    void CheckGlossaryKeys(const LoadedMod& mod) {
        std::set<std::string> replaced;
        for (const GlossaryEntryDef& entry : mod.glossary) {
            replaced.insert(entry.entry_id);
            const std::string owner = "glossary entry '" + entry.entry_id + "'";
            CheckGlossaryKey(mod, entry.path, owner, "label key",
                             entry.label_key);
            CheckGlossaryKey(mod, entry.path, owner, "description key",
                             entry.description_key);
        }
        const TextsFileDef* english = nullptr;
        for (const TextsFileDef& file : mod.texts) {
            if (file.locale == "en") english = &file;
        }
        for (const CardDef& card : mod.cards) {
            if (replaced.count(card.kind_id) != 0) continue;
            const std::string path = mod.path + "/cards/" + card.id + ".json";
            const std::string owner = "card '" + card.kind_id + "'";
            for (const auto& [declared, field, suffix] :
                 {std::tuple<const std::string&, const char*,
                             std::string_view>{
                      card.label_key, "label key",
                      glossary_keys::kLabelSuffix},
                  std::tuple<const std::string&, const char*,
                             std::string_view>{
                      card.description_key, "description key",
                      glossary_keys::kDescriptionSuffix}}) {
                if (!declared.empty()) {
                    CheckGlossaryKey(mod, path, owner, field, declared);
                    continue;
                }
                const std::string derived = card.kind_id + std::string(suffix);
                if (english != nullptr && english->content.is_object()
                    && !english->content.contains(derived)) {
                    buckets_.warnings.push_back(LoadWarning{
                        "glossary.auto.key.missing", "glossary", english->path,
                        "the derived " + std::string(field) + " '" + derived
                            + "' of " + owner + " is missing in en.json"});
                }
            }
        }
    }

    /* INFO: a used key missing in `en` rejects the mod. A key missing in
     *       another locale only warns. */
    void CheckUsedTextKeys(const LoadedMod& mod) {
        const std::set<std::string> used = UsedTextKeys(mod);
        if (used.empty()) return;
        const auto has_key = [](const TextsFileDef& file,
                                const std::string& key) {
            return file.content.is_object() && file.content.contains(key);
        };
        const TextsFileDef* english = nullptr;
        for (const TextsFileDef& file : mod.texts) {
            if (file.locale == "en") english = &file;
        }
        const std::string english_path = mod.path + "/texts/en.json";
        for (const std::string& key : used) {
            if (english == nullptr || !has_key(*english, key)) {
                buckets_.texts.push_back(Err(
                    "texts.key.missing", "texts", english_path,
                    "the key '" + key
                        + "' is used by the mod but missing in en.json"));
            }
        }
        for (const TextsFileDef& file : mod.texts) {
            if (file.locale == "en" || !file.content.is_object()) continue;
            for (const std::string& key : used) {
                if (has_key(file, key)) continue;
                buckets_.warnings.push_back(LoadWarning{
                    "texts.key.missing.locale", "texts", file.path,
                    "the key '" + key + "' is missing in the locale '"
                        + file.locale + "'; the client uses 'en'"});
            }
        }
    }

    /* INFO: false when the event cannot be checked further. A mod binds to
     *       its own events and to core events and `uni:` events. */
    bool CheckBindingEvent(const std::string& path, const LoadedMod& mod,
                           const BindingDef& binding) {
        const std::size_t colon = binding.event.find(':');
        if (colon != std::string::npos) {
            const std::string owner = binding.event.substr(0, colon);
            if (owner != mod.manifest.id && !IsBaseModId(owner)) {
                buckets_.presentations.push_back(Err(
                    "binding.event.foreign", "presentations", path,
                    "binding '" + binding.binding_id + "' binds to '"
                        + binding.event
                        + "', an event of another mod; a mod binds to its "
                          "own events and to core events"));
                return false;
            }
        }
        const BoundField event = LookupBoundField(binding.event, "");
        if (event == BoundField::kNoEvent) {
            buckets_.presentations.push_back(Err(
                "binding.event.unknown", "presentations", path,
                "binding '" + binding.binding_id + "' binds to '"
                    + binding.event + "', which is no known client event"));
            return false;
        }
        return event != BoundField::kDeferred;
    }

    /* INFO: the binding stage: ids, the event, the matcher with the audience
     *       rules, and the outputs. */
    void CheckBindings() {
        std::set<std::string> seen;
        for (const LoadedMod* mod : mods_) {
            for (const BindingDef& binding : mod->bindings) {
                const std::string path = mod->path + "/presentations/"
                                         + binding.id + ".json";
                const std::string violation = NamespaceRuleViolation(
                    mod->manifest.id, binding.binding_id);
                if (!violation.empty()) {
                    buckets_.presentations.push_back(Err(
                        "binding.id.namespace", "presentations", path,
                        violation));
                }
                if (!seen.insert(binding.binding_id).second) {
                    buckets_.presentations.push_back(Err(
                        "binding.id.duplicate", "presentations", path,
                        "duplicate binding id '" + binding.binding_id + "'"));
                }
                if (binding.select != binding_keys::kSelectAll
                    && binding.select != binding_keys::kSelectFirst) {
                    buckets_.presentations.push_back(Err(
                        "binding.select", "presentations", path,
                        "binding '" + binding.binding_id + "' has '"
                            + std::string(binding_keys::kSelect)
                            + "' that is not 'all' or 'first'"));
                }
                if (!CheckBindingEvent(path, *mod, binding)) continue;
                for (const BindingCaseDef& binding_case : binding.cases) {
                    CheckBindingCase(path, *mod, binding, binding_case);
                }
            }
        }
    }

    /* INFO: modifier kinds, forbidden `mode`, target grammar and
     *       trigger source/where validation. */
    void CheckPresentations() {
        for (const LoadedMod* mod : mods_) {
            const std::string& ns = mod->manifest.id;
            for (const auto& presentation : mod->presentations) {
                const std::string path = mod->path + "/presentations/"
                                         + presentation.id + ".json";
                if (!IsValidPresentationTarget(presentation.target)) {
                    buckets_.presentations.push_back(Err(
                        "presentation.target", "presentations", path,
                        "presentation '" + presentation.presentation_id
                            + "' has invalid target '" + presentation.target
                            + "'"));
                }
                if (presentation.modifiers.empty()) {
                    buckets_.presentations.push_back(Err(
                        "presentation.modifiers.empty", "presentations", path,
                        "presentation '" + presentation.presentation_id
                            + "' declares no modifiers"));
                }
                for (const auto& modifier : presentation.modifiers) {
                    if (ModifierMergeModes().count(modifier.kind) == 0) {
                        buckets_.presentations.push_back(Err(
                            "presentation.kind.unknown", "presentations", path,
                            "presentation '" + presentation.presentation_id
                                + "' declares unknown modifier kind '"
                                + modifier.kind + "'"));
                    }
                    if (modifier.params.is_object()
                        && modifier.params.contains("mode")) {
                        buckets_.presentations.push_back(Err(
                            "presentation.mode.forbidden", "presentations",
                            path,
                            "presentation '" + presentation.presentation_id
                                + "' modifier '" + modifier.kind
                                + "' carries a forbidden 'mode' field"));
                    }
                }
                for (const auto& trigger : presentation.triggers) {
                    CheckPresentationTrigger(trigger, presentation, ns, path);
                }
            }
        }
    }

    void CheckPresentationTrigger(const TriggerDef& trigger,
                                  const PresentationDef& presentation,
                                  const std::string& ns,
                                  const std::string& path) {
        const std::set<std::string>* fields = nullptr;
        if (!trigger.on.has_value() || *trigger.on == "always") {
            fields = &NoWhereFields();
        } else if (trigger.on->rfind("status:", 0) == 0) {
            const std::string status = trigger.on->substr(7);
            if (status.empty()) {
                buckets_.presentations.push_back(Err(
                    "presentation.trigger.source", "presentations", path,
                    "presentation '" + presentation.presentation_id
                        + "' has an empty status trigger source"));
            } else {
                const std::string full = NormalizeRef(status, ns);
                if (!status_ids_.count(full)
                    && (match_set_ || RefNamespace(full) == ns)) {
                    buckets_.presentations.push_back(Err(
                        "presentation.trigger.source", "presentations", path,
                        "presentation '" + presentation.presentation_id
                            + "' trigger source '" + *trigger.on
                            + "' does not resolve to a known status"));
                }
            }
            fields = &EventWhereFields("status_applied");
        } else {
            std::string event = *trigger.on;
            if (event.rfind("match:", 0) == 0) event = event.substr(6);
            if (KnownMatchEventTypes().count(event) == 0) {
                buckets_.presentations.push_back(Err(
                    "presentation.trigger.source", "presentations", path,
                    "presentation '" + presentation.presentation_id
                        + "' declares unknown trigger source '" + *trigger.on
                        + "'"));
                return;
            }
            fields = &EventWhereFields(event);
        }

        if (!trigger.where.is_object() || trigger.where.empty()) return;
        for (auto it = trigger.where.begin(); it != trigger.where.end(); ++it) {
            if (fields->count(it.key()) == 0) {
                buckets_.presentations.push_back(Err(
                    "presentation.trigger.where", "presentations", path,
                    "presentation '" + presentation.presentation_id
                        + "' trigger where field '" + it.key()
                        + "' is not on the source payload"));
            }
        }
    }

    /* INFO: every key a table can emit must have an entity
     *       `outcome.resolver` entry (exact `ns:key`) or a `*` catch-all
     *       anywhere in the match set. */
    void CheckOutcomeResolvers() {
        std::set<std::string> resolvable;
        bool has_star = false;
        for (const LoadedMod* mod : mods_) {
            const std::string& ns = mod->manifest.id;
            for (const auto& entity : mod->entities) {
                for (const auto& component : entity.components) {
                    if (component.name != "outcome.resolver"
                        || !component.params.is_object()) {
                        continue;
                    }
                    for (auto it = component.params.begin();
                         it != component.params.end(); ++it) {
                        if (it.key() == "*") {
                            has_star = true;
                        } else {
                            resolvable.insert(NormalizeRef(it.key(), ns));
                        }
                    }
                }
            }
        }
        if (has_star) return;

        for (const LoadedMod* mod : mods_) {
            const std::string& ns = mod->manifest.id;
            for (const auto& table : mod->tables) {
                const std::string path =
                    mod->path + "/tables/" + table.id + ".json";
                for (const auto& entry : table.entries) {
                    const std::string key = ns + ":" + entry.key;
                    if (resolvable.count(key)) continue;
                    buckets_.outcome.push_back(Err(
                        "outcome.resolver.missing", "entities", path,
                        "outcome key '" + key + "' emitted by table '"
                            + table.table_id
                            + "' has no entity resolver and no '*' catch-all"));
                }
            }
        }
    }

    /* INFO: entity component references into asset slots must
     *       resolve in an asset bundle the mod ships. */
    void CheckAssetDependencies() {
        for (const LoadedMod* mod : mods_) {
            std::map<std::string, std::set<std::string>> slots;
            for (const auto& bundle : mod->assets) {
                for (const auto& slot : bundle.slots) {
                    auto& values = slots[slot.name];
                    for (const auto& variant : slot.variants) {
                        if (variant.value.has_value()) {
                            values.insert(*variant.value);
                        }
                        if (!variant.file.has_value()) continue;
                        const std::string& file = *variant.file;
                        values.insert(file);
                        const auto slash = file.find_last_of('/');
                        const std::string base =
                            slash == std::string::npos
                                ? file
                                : file.substr(slash + 1);
                        values.insert(base);
                        const auto dot = base.find_last_of('.');
                        if (dot != std::string::npos) {
                            values.insert(base.substr(0, dot));
                        }
                    }
                }
            }

            for (const auto& entity : mod->entities) {
                const std::string path =
                    mod->path + "/entities/" + entity.id + ".json";
                for (const auto& component : entity.components) {
                    const AssetRef* ref =
                        AssetRefForComponent(component.name);
                    if (ref == nullptr || !component.params.is_object()) {
                        continue;
                    }
                    auto value = component.params.find(ref->param);
                    if (value == component.params.end()
                        || !value->is_string()) {
                        continue;
                    }
                    const std::string token = value->get<std::string>();
                    auto slot = slots.find(ref->slot);
                    if (slot == slots.end()
                        || slot->second.count(token) == 0) {
                        buckets_.asset.push_back(Err(
                            "asset.dep.missing", "entities", path,
                            "entity '" + entity.entity_id + "' component '"
                                + component.name + "' references "
                                + ref->slot + " '" + token
                                + "' which no asset bundle declares"));
                    }
                }
            }
        }
    }

    void WalkAllGraphs() {
        for (const LoadedMod* mod : mods_) {
            const std::string& ns = mod->manifest.id;
            for (const auto& card : mod->cards) {
                for (const auto& be : card.behaviors) {
                    CheckHook(be, ns, "cards");
                    WalkGraph(be.graph,
                              GraphCtx{ns, "cards", mod->path + "/cards.json",
                                       true, false, false, nullptr});
                    if (be.where) {
                        CheckCondition(*be.where,
                                       GraphCtx{ns, "cards",
                                                mod->path + "/cards.json", true,
                                                false, false, nullptr});
                    }
                }
                if (card.window) {
                    CheckCardWindow(*card.window, card, ns, mod->path);
                }
                if (card.auto_trigger) {
                    /* INFO: must-apply auto_trigger reuses the
                     *       card-behavior context: same namespace, artifact
                     *       and card-context flags. */
                    GraphCtx ctx{ns, "cards", mod->path + "/cards.json",
                                 true, false, false, nullptr};
                    CheckCondition(card.auto_trigger->condition, ctx);
                    BehaviorGraph graph;
                    graph.raw = card.auto_trigger->graph;
                    auto nodes = graph.raw.find("nodes");
                    if (nodes != graph.raw.end() && nodes->is_array()) {
                        graph.nodes = *nodes;
                    }
                    WalkGraph(graph, ctx);
                }
            }
            for (const auto& rule : mod->rules) {
                for (const auto& hook : rule.hooks) {
                    CheckHook(hook, ns, "rules");
                    WalkGraph(hook.graph,
                              GraphCtx{ns, "rules", mod->path + "/rules.json",
                                       false, false, false, nullptr});
                    if (hook.where) {
                        CheckCondition(*hook.where,
                                       GraphCtx{ns, "rules",
                                                mod->path + "/rules.json",
                                                false, false, false, nullptr});
                    }
                }
            }
            for (const auto& mut : mod->mutations) {
                CheckMutationTarget(mut, ns, mod->path + "/mutations.json");
                CheckMutation(mut, ns, mod->path + "/mutations.json");
            }
        }
    }

    void CheckMutationTarget(const MutationDef& mut,
                             const std::string& ns,
                             const std::string& path) {
        const std::string& target = mut.target;
        if (!IsValidKindId(target)) {
            buckets_.refs.push_back(Err(
                "ref.kind", "mutations", path,
                "mutation target '" + target
                    + "' must be a `namespace:local` kind id"));
            return;
        }
        if (kind_ids_.count(target)) return;
        // INFO: a mutation may also target a rule hook graph; the
        //       assembler compiles that rule's graphs at assembly.
        if (rule_ids_.count(target)) return;
        if (restriction_ids_.count(target)) return;
        if (!match_set_ && RefNamespace(target) != ns) return;
        buckets_.refs.push_back(Err("ref.kind", "mutations", path,
                                    "unresolved mutation target '" + target
                                        + "'"));
    }

    void CheckHook(const BehaviorEntry& entry,
                   const std::string& ns,
                   const std::string& artifact) {
        std::string name;
        std::string phase;
        if (!ResolveHook(entry.hook, name, phase)) {
            buckets_.ops.push_back(Err("hook.name", artifact,
                                       ArtifactPathFor(ns, artifact + ".json"),
                                       "unknown hook '" + entry.hook + "'"));
        }
        if (entry.phase && *entry.phase != "before" && *entry.phase != "after") {
            buckets_.ops.push_back(Err("hook.name", artifact,
                                       ArtifactPathFor(ns, artifact + ".json"),
                                       "hook phase must be 'before' or 'after'"));
        }
    }

    void CheckMutation(const MutationDef& mut,
                       const std::string& ns,
                       const std::string& path) {
        const std::string artifact = "mutations";
        if (mut.mode != "replace" && mut.mode != "wrap" && mut.mode != "veto"
            && mut.mode != "filter") {
            buckets_.ops.push_back(Err("op.type", artifact, path,
                                       "unknown mutation mode '" + mut.mode
                                           + "'"));
        }
        if (mut.position && *mut.position != "before"
            && *mut.position != "after") {
            buckets_.ops.push_back(Err(
                "op.type", artifact, path,
                "mutation position must be 'before' or 'after'"));
        }
        const bool needs_graph = mut.mode == "replace" || mut.mode == "wrap"
                                 || mut.mode == "filter";
        if (needs_graph
            && (!mut.replacement.raw.is_object()
                || !mut.replacement.raw.contains("nodes")
                || !mut.replacement.raw["nodes"].is_array()
                || mut.replacement.raw["nodes"].empty())) {
            buckets_.ops.push_back(Err(
                "op.arity", artifact, path,
                "mutation mode '" + mut.mode + "' requires a replacement graph"));
        }
        if (mut.mode == "veto" && !mut.where) {
            buckets_.ops.push_back(Err(
                "op.arity", artifact, path,
                "veto mutation requires a 'where' condition"));
        }

        WalkGraph(mut.replacement,
                  GraphCtx{ns, artifact, path, false, false,
                           mut.mode == "wrap", nullptr});
        if (mut.where) {
            CheckCondition(*mut.where,
                           GraphCtx{ns, artifact, path, false, false, false,
                                    nullptr});
        }
    }

    void CheckCardWindow(const WindowSpec& window,
                         const CardDef& card,
                         const std::string& ns,
                         const std::string& mod_path) {
        const std::string path = mod_path + "/cards.json";
        CheckSelectorValue(window.responders,
                           GraphCtx{ns, "cards", path, true, false, false,
                                    nullptr},
                           "window responders");
        if (window.respond_with) {
            ScanTagRefs(*window.respond_with, path, "cards");
        }
        if (window.duration.empty()) {
            buckets_.ops.push_back(
                Err("op.arity", "cards", path, "window duration is required"));
        } else if (window.duration != "env") {
            buckets_.ops.push_back(Err(
                "op.type", "cards", path,
                "window duration must be 'env' or a duration object"));
        }

        /* INFO: window.when_played is sugar for a window node at the head of
         *       the card's on_play graph, so its routes resolve against that
         *       graph's node ids. */
        std::set<std::string> on_play_nodes;
        for (const auto& be : card.behaviors) {
            std::string name;
            std::string phase;
            if (!ResolveHook(be.hook, name, phase) || name != "play") continue;
            for (const auto& node : be.graph.nodes) {
                if (!node.is_object()) continue;
                auto id = node.find("id");
                if (id != node.end() && id->is_string()) {
                    on_play_nodes.insert(id->get<std::string>());
                }
            }
        }

        if (window.default_route.empty()) {
            buckets_.graph.push_back(
                Err("window.default", "cards", path,
                    "card window requires a default route"));
        } else if (!on_play_nodes.count(window.default_route)) {
            buckets_.refs.push_back(Err(
                "ref.graph", "cards", path,
                "card window default route '" + window.default_route
                    + "' does not resolve to a node in the on_play graph"));
        }
        if (!window.on_response.empty()
            && !on_play_nodes.count(window.on_response)) {
            buckets_.refs.push_back(Err(
                "ref.graph", "cards", path,
                "card window on_response route '" + window.on_response
                    + "' does not resolve to a node in the on_play graph"));
        }
    }

    static NodeType DetectType(const nlohmann::json& raw) {
        if (raw.contains("op")) return NodeType::kOp;
        if (raw.contains("window")) return NodeType::kWindow;
        if (raw.contains("cases")) return NodeType::kBranch;
        if (raw.contains("branches")) return NodeType::kFork;
        if (raw.contains("schedule")) return NodeType::kSchedule;
        return NodeType::kUnknown;
    }

    static void ExtractTargets(NodeInfo& info) {
        auto add = [&info](const nlohmann::json& value) {
            if (value.is_string()) info.targets.push_back(value.get<std::string>());
        };
        if (info.type == NodeType::kOp) {
            add(info.raw.value("next", nlohmann::json()));
        } else if (info.type == NodeType::kWindow) {
            add(info.raw.value("default", nlohmann::json()));
            const nlohmann::json* window = nullptr;
            auto win = info.raw.find("window");
            if (win != info.raw.end() && win->is_object()) window = &*win;
            if (window != nullptr) {
                add(window->value("default_route", nlohmann::json()));
            }
            const nlohmann::json* on_response = nullptr;
            auto node_or = info.raw.find("on_response");
            if (node_or != info.raw.end()) {
                on_response = &*node_or;
            } else if (window != nullptr) {
                auto win_or = window->find("on_response");
                if (win_or != window->end()) on_response = &*win_or;
            }
            if (on_response != nullptr && on_response->is_object()) {
                for (auto it = on_response->begin(); it != on_response->end();
                     ++it) {
                    add(it.value());
                }
            }
        } else if (info.type == NodeType::kBranch) {
            auto cases = info.raw.find("cases");
            if (cases != info.raw.end() && cases->is_array()) {
                for (const auto& c : *cases) {
                    if (c.is_object()) add(c.value("next", nlohmann::json()));
                }
            }
            add(info.raw.value("else", nlohmann::json()));
        } else if (info.type == NodeType::kFork) {
            auto branches = info.raw.find("branches");
            if (branches != info.raw.end() && branches->is_array()) {
                for (const auto& b : *branches) add(b);
            }
        } else if (info.type == NodeType::kSchedule) {
            add(info.raw.value("next", nlohmann::json()));
        }
    }

    void WalkGraph(const BehaviorGraph& graph, const GraphCtx& ctx) {
        if (graph.nodes.empty()) return;

        std::vector<NodeInfo> infos;
        std::set<std::string> node_ids;
        std::map<std::string, std::size_t> index_by_id;

        for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
            const auto& raw = graph.nodes[i];
            NodeInfo info;
            info.raw = raw;
            if (!raw.is_object()) {
                buckets_.ops.push_back(Err("graph.node", ctx.artifact, ctx.path,
                                           "graph node must be an object"));
                infos.push_back(std::move(info));
                continue;
            }
            auto id = raw.find("id");
            if (id == raw.end() || !id->is_string()) {
                buckets_.ops.push_back(Err("graph.node", ctx.artifact, ctx.path,
                                           "graph node requires a string id"));
                infos.push_back(std::move(info));
                continue;
            }
            info.id = id->get<std::string>();
            if (!node_ids.insert(info.id).second) {
                buckets_.ops.push_back(Err("graph.node", ctx.artifact, ctx.path,
                                           "duplicate node id '" + info.id
                                               + "'"));
            }
            info.type = DetectType(raw);
            if (info.type == NodeType::kUnknown) {
                buckets_.ops.push_back(Err("graph.node", ctx.artifact, ctx.path,
                                           "unknown node type for '" + info.id
                                               + "'"));
            }
            ExtractTargets(info);
            index_by_id[info.id] = i;
            infos.push_back(std::move(info));
        }

        std::set<std::string> responder_nodes;
        {
            std::vector<std::size_t> roots;
            for (const auto& info : infos) {
                if (info.type != NodeType::kWindow) continue;
                const nlohmann::json* mapping = nullptr;
                auto node_or = info.raw.find("on_response");
                if (node_or != info.raw.end()) {
                    mapping = &*node_or;
                } else {
                    auto win = info.raw.find("window");
                    if (win != info.raw.end() && win->is_object()) {
                        auto win_or = win->find("on_response");
                        if (win_or != win->end()) mapping = &*win_or;
                    }
                }
                if (mapping == nullptr || !mapping->is_object()) continue;
                for (auto it = mapping->begin(); it != mapping->end(); ++it) {
                    if (it.value().is_string()) {
                        auto found =
                            index_by_id.find(it.value().get<std::string>());
                        if (found != index_by_id.end()) {
                            roots.push_back(found->second);
                        }
                    }
                }
            }
            std::vector<std::size_t> stack = roots;
            while (!stack.empty()) {
                std::size_t i = stack.back();
                stack.pop_back();
                if (i >= infos.size()) continue;
                if (!responder_nodes.insert(infos[i].id).second) continue;
                for (const auto& target : infos[i].targets) {
                    auto it = index_by_id.find(target);
                    if (it != index_by_id.end()) stack.push_back(it->second);
                }
            }
        }

        std::set<std::string> reachable;
        const bool entry_valid = !infos.empty() && !infos[0].id.empty();
        if (entry_valid) {
            std::vector<std::size_t> stack = {0};
            while (!stack.empty()) {
                std::size_t i = stack.back();
                stack.pop_back();
                if (i >= infos.size()) continue;
                if (!reachable.insert(infos[i].id).second) continue;
                for (const auto& target : infos[i].targets) {
                    auto it = index_by_id.find(target);
                    if (it != index_by_id.end()) stack.push_back(it->second);
                }
            }
        }

        for (auto& info : infos) {
            info.responder = responder_nodes.count(info.id) != 0;
        }

        GraphCtx node_ctx = ctx;
        node_ctx.node_ids = &node_ids;
        for (const auto& info : infos) {
            CheckNode(info, node_ctx);
        }

        for (const auto& info : infos) {
            for (const auto& target : info.targets) {
                if (!node_ids.count(target)) {
                    buckets_.refs.push_back(Err(
                        "ref.graph", ctx.artifact, ctx.path,
                        "node '" + info.id + "' routes to unknown node '"
                            + target + "'"));
                }
            }
        }

        CheckStructure(infos, index_by_id, reachable, ctx);
    }

    void CheckStructure(
        const std::vector<NodeInfo>& infos,
        const std::map<std::string, std::size_t>& index_by_id,
        const std::set<std::string>& reachable,
        const GraphCtx& ctx) {
        std::map<std::size_t, int> color;
        bool cycle = false;
        std::function<void(std::size_t)> visit = [&](std::size_t i) {
            if (cycle) return;
            color[i] = 1;
            for (const auto& target : infos[i].targets) {
                auto it = index_by_id.find(target);
                if (it == index_by_id.end()) continue;
                std::size_t j = it->second;
                if (color[j] == 1) {
                    cycle = true;
                    return;
                }
                if (color[j] == 0) visit(j);
                if (cycle) return;
            }
            color[i] = 2;
        };
        for (std::size_t i = 0; i < infos.size(); ++i) {
            if (color[i] == 0) visit(i);
            if (cycle) break;
        }
        if (cycle) {
            buckets_.graph.push_back(
                Err("graph.cycle", ctx.artifact, ctx.path,
                    "graph contains a cycle"));
        }

        const bool entry_valid = !infos.empty() && !infos[0].id.empty();
        for (const auto& info : infos) {
            if (info.id.empty()) continue;
            if (!entry_valid) continue;
            if (!reachable.count(info.id)) {
                buckets_.graph.push_back(Err(
                    "graph.unreachable", ctx.artifact, ctx.path,
                    "node '" + info.id
                        + "' is unreachable from the entry node"));
            }
        }
    }

    /**
     * @brief Validates the declared fields of a graph window node.
     *
     * A window node carries `responders`, `respond_with` and `duration` either
     * inside its `window` object or at the node top level (the Resolver reads
     * both). Only declared fields are checked: a missing `responders` or
     * `duration` is not an error (the artifact model does not require them on
     * the graph-node form). Response routes (`default`, `on_response`) are
     * resolved by WalkGraph via ExtractTargets.
     */
    void CheckWindowFields(const nlohmann::json& node,
                           const nlohmann::json* window,
                           const GraphCtx& ctx) {
        const auto field = [&](const char* key) -> const nlohmann::json* {
            if (window != nullptr) {
                auto it = window->find(key);
                if (it != window->end()) return &*it;
            }
            auto it = node.find(key);
            if (it != node.end()) return &*it;
            return nullptr;
        };

        const nlohmann::json* responders = field("responders");
        if (responders != nullptr) {
            if (!responders->is_string()) {
                buckets_.ops.push_back(Err(
                    "op.type", ctx.artifact, ctx.path,
                    "window responders must be a selector string"));
            } else {
                CheckSelectorValue(responders->get<std::string>(), ctx,
                                   "window responders");
            }
        }

        const nlohmann::json* kind = field("kind");
        if (kind != nullptr
            && !IsValidWindowKind(*kind)) {
            buckets_.ops.push_back(Err(
                "op.type", ctx.artifact, ctx.path,
                "window kind must match ^[a-z0-9_:.-]{1,32}$"));
        }

        const nlohmann::json* duration = field("duration");
        if (duration != nullptr) {
            if (duration->is_number_integer()) {
                if (duration->get<int64_t>() < 0) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        "window duration must not be negative"));
                }
            } else if (!duration->is_string()) {
                buckets_.ops.push_back(Err(
                    "op.type", ctx.artifact, ctx.path,
                    "window duration must be 'env' or a duration unit"));
            } else {
                const std::string token = duration->get<std::string>();
                if (token != "env" && !IsDurationUnit(token)) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        "window duration must be 'env' or a duration unit"));
                }
            }
        }
    }

    void CheckNode(const NodeInfo& info, const GraphCtx& base_ctx) {
        GraphCtx ctx = base_ctx;
        ctx.responder_ctx = info.responder;

        if (info.type == NodeType::kOp) {
            auto op = info.raw.find("op");
            if (op == info.raw.end() || !op->is_string()) {
                buckets_.ops.push_back(Err("graph.node", ctx.artifact, ctx.path,
                                           "op node requires a string 'op'"));
                return;
            }
            const std::string op_name = op->get<std::string>();
            if (op_name == "call_original" && !ctx.allow_call_original) {
                buckets_.graph.push_back(Err(
                    "graph.call_original", ctx.artifact, ctx.path,
                    "call_original is only legal inside a wrap mutation"));
            }
            const OpSignature* sig = FindOp(op_name);
            if (sig == nullptr) {
                buckets_.ops.push_back(Err("op.unknown", ctx.artifact, ctx.path,
                                           "unknown op '" + op_name + "'"));
                return;
            }
            CheckArgs(ctx, op_name, info.raw, sig->args, sig->either_of,
                      sig->allows_extra_args);
        } else if (info.type == NodeType::kWindow) {
            const nlohmann::json* window = nullptr;
            auto win = info.raw.find("window");
            if (win != info.raw.end() && win->is_object()) window = &*win;

            CheckWindowFields(info.raw, window, ctx);
            auto def = info.raw.find("default");
            if (def == info.raw.end() || !def->is_string()) {
                buckets_.graph.push_back(Err(
                    "window.default", ctx.artifact, ctx.path,
                    "window node '" + info.id + "' requires a default route"));
            }
            const nlohmann::json* on_response = nullptr;
            auto node_or = info.raw.find("on_response");
            if (node_or != info.raw.end()) {
                on_response = &*node_or;
            } else if (window != nullptr) {
                auto win_or = window->find("on_response");
                if (win_or != window->end()) on_response = &*win_or;
            }
            if (on_response != nullptr) {
                if (!on_response->is_object()) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        "window on_response must be an object mapping"));
                } else {
                    for (auto it = on_response->begin();
                         it != on_response->end(); ++it) {
                        if (!it.value().is_string()) {
                            buckets_.ops.push_back(Err(
                                "op.type", ctx.artifact, ctx.path,
                                "window on_response values must be node ids"));
                        }
                    }
                }
            }
        } else if (info.type == NodeType::kBranch) {
            auto cases = info.raw.find("cases");
            if (cases == info.raw.end() || !cases->is_array()) {
                buckets_.ops.push_back(Err(
                    "op.arity", ctx.artifact, ctx.path,
                    "branch node requires a 'cases' array"));
            } else {
                for (const auto& c : *cases) {
                    if (!c.is_object() || !c.contains("when")
                        || !c["when"].is_object() || !c.contains("next")
                        || !c["next"].is_string()) {
                        buckets_.ops.push_back(Err(
                            "op.arity", ctx.artifact, ctx.path,
                            "branch case requires 'when' and 'next'"));
                        continue;
                    }
                    CheckCondition(c["when"], ctx);
                }
            }
            auto else_route = info.raw.find("else");
            if (else_route == info.raw.end() || !else_route->is_string()) {
                buckets_.ops.push_back(Err(
                    "op.arity", ctx.artifact, ctx.path,
                    "branch node requires an 'else' route"));
            }
        } else if (info.type == NodeType::kFork) {
            auto branches = info.raw.find("branches");
            if (branches == info.raw.end() || !branches->is_array()
                || branches->empty()) {
                buckets_.ops.push_back(Err(
                    "op.arity", ctx.artifact, ctx.path,
                    "fork node requires a non-empty 'branches' array"));
            }
        } else if (info.type == NodeType::kSchedule) {
            auto next = info.raw.find("next");
            if (next == info.raw.end() || !next->is_string()) {
                buckets_.ops.push_back(Err(
                    "op.arity", ctx.artifact, ctx.path,
                    "schedule node requires a 'next' route"));
            }
            auto duration = info.raw.find("duration");
            if (duration == info.raw.end()) {
                buckets_.ops.push_back(Err(
                    "op.arity", ctx.artifact, ctx.path,
                    "schedule node requires a 'duration'"));
            } else {
                CheckDuration(*duration, ctx, "schedule");
            }
        }
    }

    void CheckArgs(const GraphCtx& ctx,
                   const std::string& owner,
                   const nlohmann::json& node,
                   const std::vector<ArgSpec>& specs,
                   const std::vector<std::vector<std::string>>& either_of,
                   bool allow_extra) {
        auto args_it = node.find("args");
        nlohmann::json empty = nlohmann::json::object();
        const nlohmann::json& args =
            (args_it == node.end()) ? empty : *args_it;
        if (!args.is_object()) {
            buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                       owner + " args must be an object"));
            return;
        }

        std::set<std::string> known;
        for (const auto& spec : specs) known.insert(spec.name);

        for (const auto& spec : specs) {
            auto it = args.find(spec.name);
            if (it == args.end()) {
                if (spec.required) {
                    buckets_.ops.push_back(Err(
                        "op.arity", ctx.artifact, ctx.path,
                        owner + " is missing required arg '" + spec.name + "'"));
                }
                continue;
            }
            CheckArgValue(ctx, owner, spec, *it);
        }

        if (!allow_extra) {
            for (auto it = args.begin(); it != args.end(); ++it) {
                if (!known.count(it.key())) {
                    buckets_.ops.push_back(Err(
                        "op.arity", ctx.artifact, ctx.path,
                        owner + " has unknown arg '" + it.key() + "'"));
                }
            }
        }

        for (const auto& group : either_of) {
            int present = 0;
            for (const auto& name : group) {
                if (args.contains(name)) ++present;
            }
            if (present != 1) {
                std::string names;
                for (const auto& name : group) {
                    if (!names.empty()) names += "|";
                    names += name;
                }
                buckets_.ops.push_back(Err(
                    "op.arity", ctx.artifact, ctx.path,
                    owner + " requires exactly one of '" + names + "'"));
            }
        }
    }

    void CheckArgValue(const GraphCtx& ctx,
                       const std::string& owner,
                       const ArgSpec& spec,
                       const nlohmann::json& value) {
        const std::string where = owner + " arg '" + spec.name + "'";
        switch (spec.type) {
            case ArgType::kSelector:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a selector"));
                } else {
                    CheckSelectorValue(value.get<std::string>(), ctx, where);
                }
                break;
            case ArgType::kInt:
                if (!value.is_number_integer()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be an integer"));
                    break;
                }
                if (spec.has_bounds) {
                    std::int64_t n = value.get<std::int64_t>();
                    if (n < static_cast<std::int64_t>(spec.min_value)
                        || n > static_cast<std::int64_t>(spec.max_value)) {
                        buckets_.ops.push_back(Err("op.bounds", ctx.artifact,
                                                   ctx.path,
                                                   where + " out of range"));
                    }
                }
                break;
            case ArgType::kNumber:
                if (!value.is_number()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a number"));
                }
                break;
            case ArgType::kBool:
                if (!value.is_boolean()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a boolean"));
                }
                break;
            case ArgType::kString:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a string"));
                }
                break;
            case ArgType::kEnum: {
                bool ok = value.is_string();
                if (ok) {
                    ok = std::find(spec.enum_values.begin(),
                                   spec.enum_values.end(),
                                   value.get_ref<const std::string&>())
                         != spec.enum_values.end();
                }
                if (!ok) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " is not an allowed token"));
                }
                break;
            }
            case ArgType::kDuration:
                CheckDuration(value, ctx, where);
                break;
            case ArgType::kAspectMask:
                CheckAspectMask(value, ctx, where);
                break;
            case ArgType::kCondition:
                CheckCondition(value, ctx);
                break;
            case ArgType::kKindRef:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a kind ref"));
                } else {
                    CheckKindRef(value.get<std::string>(), ctx.ns, ctx);
                }
                break;
            case ArgType::kStatusRef:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a status ref"));
                } else {
                    CheckStatusRef(value.get<std::string>(), ctx.ns, ctx);
                }
                break;
            case ArgType::kTagRef:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a tag"));
                } else if (match_set_) {
                    CheckTagRef(value.get<std::string>(), ctx);
                }
                break;
            case ArgType::kRestrictionRef:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " must be a restriction id"));
                } else {
                    CheckRestrictionRef(value.get<std::string>(), ctx.ns, ctx);
                }
                break;
            case ArgType::kTableRef:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " must be a table ref"));
                } else {
                    CheckTableRef(value.get<std::string>(), ctx.ns, ctx);
                }
                break;
            case ArgType::kEventRef:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " must be an event id"));
                } else {
                    CheckEventRef(value.get<std::string>(), ctx);
                }
                break;
            case ArgType::kBindingName: {
                // INFO: `^\$?[a-z0-9_]+$`; the leading `$` is
                //       optional (a bare name is still a frame key, never a
                //       node id, because node ids are consumed by routing).
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " must be a binding key"));
                    break;
                }
                std::string token = value.get<std::string>();
                if (!token.empty() && token[0] == '$') token.erase(0, 1);
                if (token.empty() || !IsValidLocalId(token)) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " must be a `$name` binding key"));
                }
                break;
            }
            case ArgType::kNodeRef:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a node id"));
                } else if (ctx.node_ids
                           && !ctx.node_ids->count(value.get<std::string>())) {
                    buckets_.refs.push_back(Err(
                        "ref.graph", ctx.artifact, ctx.path,
                        where + " references unknown node '"
                            + value.get<std::string>() + "'"));
                }
                break;
            case ArgType::kChoiceSpec:
                if (!value.is_string()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be a choice spec"));
                } else {
                    CheckChoiceSpec(value.get<std::string>(), ctx, where);
                }
                break;
            case ArgType::kRollSpec:
                CheckRollSpec(value, ctx, where);
                break;
            case ArgType::kRestrictionEntry:
                CheckRestrictionEntry(value, ctx);
                break;
            case ArgType::kZone:
                if (!value.is_string()
                    || !IsZoneToken(value.get<std::string>())) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " is not a zone"));
                }
                break;
            case ArgType::kPileRef:
                if (!value.is_string()
                    || !IsPileRef(value.get<std::string>())) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " is not a pile reference"));
                }
                break;
            case ArgType::kComparison:
                if (!value.is_string()
                    || !IsComparisonToken(value.get<std::string>())) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " is not a comparison token"));
                }
                break;
            case ArgType::kStackPolicy:
                if (!value.is_string()
                    || !IsValidStackPolicy(value.get<std::string>())) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " is not a valid stack policy"));
                }
                break;
            case ArgType::kObject:
                if (!value.is_object()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be an object"));
                }
                break;
            case ArgType::kArray:
                if (!value.is_array()) {
                    buckets_.ops.push_back(Err("op.type", ctx.artifact,
                                               ctx.path,
                                               where + " must be an array"));
                }
                break;
            case ArgType::kAny:
                break;
        }
    }

    static bool IsPileRef(const std::string& token) {
        return token == "draw" || token == "discard" || token == "draw_pile"
               || token == "discard_pile" || token == "@draw_pile"
               || token == "@discard_pile";
    }

    void CheckChoiceSpec(const std::string& token,
                         const GraphCtx& ctx,
                         const std::string& where) {
        if (token == "random" || token == "chosen") return;
        if (token.rfind("tag:", 0) == 0 && token.size() > 4) return;
        if (token.rfind("kind:", 0) == 0 && token.size() > 5) return;
        buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                   where + " is not a valid choice spec"));
    }

    void CheckRollSpec(const nlohmann::json& value,
                       const GraphCtx& ctx,
                       const std::string& where) {
        if (!value.is_object()) {
            buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                       where + " must be an object"));
            return;
        }
        auto sides = value.find("sides");
        auto count = value.find("count");
        bool ok = sides != value.end() && sides->is_number_integer()
                  && sides->get<std::int64_t>() >= 1 && count != value.end()
                  && count->is_number_integer() && count->get<std::int64_t>() >= 1;
        auto keep = value.find("keep");
        if (keep != value.end()
            && (!keep->is_number_integer() || keep->get<std::int64_t>() < 0)) {
            ok = false;
        }
        if (!ok) {
            buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                       where + " has an invalid roll spec"));
        }
    }

    void CheckAspectMask(const nlohmann::json& value,
                         const GraphCtx& ctx,
                         const std::string& where) {
        auto bad = [&]() {
            buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                       where + " has an unknown aspect"));
        };
        if (value.is_string()) {
            if (!IsVisibilityAspect(value.get<std::string>())) bad();
            return;
        }
        if (value.is_array()) {
            for (const auto& item : value) {
                if (!item.is_string()
                    || !IsVisibilityAspect(item.get<std::string>())) {
                    bad();
                    return;
                }
            }
            return;
        }
        buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                   where + " must be an aspect or array"));
    }

    void CheckDuration(const nlohmann::json& value,
                       const GraphCtx& ctx,
                       const std::string& where) {
        if (value.is_object()) {
            CheckDurationUnit(value, ctx, where);
            return;
        }
        if (value.is_array()) {
            if (value.empty()) {
                buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                           where + " duration list is empty"));
                return;
            }
            for (const auto& item : value) {
                if (!item.is_object()) {
                    buckets_.ops.push_back(Err(
                        "op.type", ctx.artifact, ctx.path,
                        where + " duration list items must be objects"));
                    return;
                }
                CheckDurationUnit(item, ctx, where);
            }
            return;
        }
        buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                   where + " must be a duration spec"));
    }

    void CheckDurationUnit(const nlohmann::json& value,
                           const GraphCtx& ctx,
                           const std::string& where) {
        auto unit = value.find("unit");
        auto amount = value.find("value");
        bool ok = unit != value.end() && unit->is_string()
                  && IsDurationUnit(unit->get<std::string>())
                  && amount != value.end() && amount->is_number_integer()
                  && amount->get<std::int64_t>() >= 0;
        if (!ok) {
            buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                       where + " has an invalid duration"));
        }
    }

    void CheckSelectorValue(const std::string& token,
                            const GraphCtx& ctx,
                            const std::string& where) {
        if (token.empty() || token[0] != '@' || !IsKnownSelector(token)) {
            buckets_.selector.push_back(Err(
                "selector.unknown", ctx.artifact, ctx.path,
                where + " is unknown selector '" + token + "'"));
            return;
        }
        if (token == "@responder" && !ctx.responder_ctx) {
            buckets_.selector.push_back(Err(
                "selector.scope", ctx.artifact, ctx.path,
                where + " uses @responder outside a window route"));
        }
        if (token == "@card" && !ctx.card_behavior) {
            buckets_.selector.push_back(Err(
                "selector.scope", ctx.artifact, ctx.path,
                where + " uses @card outside card behavior"));
        }
    }

    void CheckCondition(const nlohmann::json& condition, const GraphCtx& ctx) {
        if (!condition.is_object()) {
            buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                       "condition must be an object"));
            return;
        }
        if (condition.empty() || condition.size() != 1) {
            buckets_.ops.push_back(Err(
                "op.arity", ctx.artifact, ctx.path,
                "condition must name exactly one keyword"));
            return;
        }
        auto it = condition.begin();
        const ConditionSignature* sig = FindCondition(it.key());
        if (sig == nullptr) {
            buckets_.ops.push_back(Err("op.unknown", ctx.artifact, ctx.path,
                                       "unknown condition '" + it.key() + "'"));
            return;
        }
        const nlohmann::json& body = it.value();
        if (sig->args.empty()) {
            if (!body.is_null() && (!body.is_object() || !body.empty())) {
                buckets_.ops.push_back(Err(
                    "op.arity", ctx.artifact, ctx.path,
                    "condition '" + it.key() + "' takes no args"));
            }
            return;
        }
        if (body.is_null()) {
            for (const auto& spec : sig->args) {
                if (spec.required) {
                    buckets_.ops.push_back(Err(
                        "op.arity", ctx.artifact, ctx.path,
                        "condition '" + it.key() + "' is missing required arg '"
                            + spec.name + "'"));
                }
            }
            return;
        }
        nlohmann::json wrapper = {{"args", body}};
        CheckArgs(ctx, it.key(), wrapper, sig->args, sig->either_of, false);
    }

    void CheckRestrictionEntry(const nlohmann::json& value,
                               const GraphCtx& ctx) {
        if (!value.is_object()) {
            buckets_.ops.push_back(Err("op.type", ctx.artifact, ctx.path,
                                       "restriction entry must be an object"));
            return;
        }
        auto id = value.find("id");
        if (id == value.end() || !id->is_string()
            || !IsValidKindId(id->get<std::string>())) {
            buckets_.ids.push_back(Err(
                "id.syntax", ctx.artifact, ctx.path,
                "restriction entry id must be a `namespace:local` kind id"));
        }
        auto phase = value.find("phase");
        if (phase == value.end() || !phase->is_string()
            || (phase->get<std::string>() != "allow"
                && phase->get<std::string>() != "deny")) {
            buckets_.ops.push_back(Err(
                "op.type", ctx.artifact, ctx.path,
                "restriction entry phase must be 'allow' or 'deny'"));
        }
        auto condition = value.find("condition");
        if (condition == value.end()) {
            buckets_.ops.push_back(Err(
                "op.arity", ctx.artifact, ctx.path,
                "restriction entry requires a 'condition'"));
        } else {
            CheckCondition(*condition, ctx);
        }
    }

    void ScanTagRefs(const nlohmann::json& value,
                     const std::string& path,
                     const std::string& artifact) {
        if (!match_set_) return;
        GraphCtx ctx{"", artifact, path, false, false, false, nullptr};
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (it.key() == "tag" && it.value().is_string()) {
                    CheckTagRef(it.value().get<std::string>(), ctx);
                } else if (it.key() == "any_tag") {
                    if (it.value().is_string()) {
                        CheckTagRef(it.value().get<std::string>(), ctx);
                    } else if (it.value().is_array()) {
                        for (const auto& tag : it.value()) {
                            if (tag.is_string()) {
                                CheckTagRef(tag.get<std::string>(), ctx);
                            }
                        }
                    }
                } else {
                    ScanTagRefs(it.value(), path, artifact);
                }
            }
        } else if (value.is_array()) {
            for (const auto& item : value) ScanTagRefs(item, path, artifact);
        }
    }

    void CheckKindRef(const std::string& raw,
                      const std::string& ns,
                      const GraphCtx& ctx) {
        CheckReference(raw, ns, kind_ids_, "ref.kind", "kind", ctx);
    }

    void CheckStatusRef(const std::string& raw,
                        const std::string& ns,
                        const GraphCtx& ctx) {
        CheckReference(raw, ns, status_ids_, "ref.status", "status", ctx);
    }

    void CheckReference(const std::string& raw,
                        const std::string& ns,
                        const std::set<std::string>& known,
                        const std::string& check,
                        const std::string& noun,
                        const GraphCtx& ctx) {
        if (!IsValidKindId(raw) && !IsValidLocalId(raw)) {
            buckets_.refs.push_back(Err(check, ctx.artifact, ctx.path,
                                        "malformed " + noun + " ref '" + raw
                                            + "'"));
            return;
        }
        const std::string full = NormalizeRef(raw, ns);
        if (known.count(full)) return;
        if (!match_set_ && RefNamespace(full) != ns) return;
        buckets_.refs.push_back(Err(check, ctx.artifact, ctx.path,
                                    "unresolved " + noun + " ref '" + full
                                        + "'"));
    }

    void CheckTagRef(const std::string& tag, const GraphCtx& ctx) {
        if (!match_set_) return;
        if (!tags_.count(tag)) {
            buckets_.refs.push_back(Err("ref.tag", ctx.artifact, ctx.path,
                                        "unresolved tag ref '" + tag + "'"));
        }
    }

    void CheckRestrictionRef(const std::string& raw,
                             const std::string& ns,
                             const GraphCtx& ctx) {
        const std::string full = NormalizeRef(raw, ns);
        if (restriction_ids_.count(full)) return;
        if (!match_set_ && RefNamespace(full) != ns) return;
        buckets_.refs.push_back(Err("ref.restriction", ctx.artifact, ctx.path,
                                    "unresolved restriction ref '" + full
                                        + "'"));
    }

    void CheckTableRef(const std::string& raw,
                       const std::string& ns,
                       const GraphCtx& ctx) {
        CheckReference(raw, ns, table_ids_, "ref.table", "table", ctx);
    }

    /* INFO: a mod emits only events of its own namespace, so a rule cannot
     *       forge a `uni:` event. The id must be defined in the active set. */
    void CheckEventRef(const std::string& raw, const GraphCtx& ctx) {
        if (!IsValidKindId(raw)) {
            buckets_.refs.push_back(Err("ref.event", ctx.artifact, ctx.path,
                                        "malformed event ref '" + raw + "'"));
            return;
        }
        if (RefNamespace(raw) != ctx.ns) {
            buckets_.refs.push_back(Err(
                "ref.event.namespace", ctx.artifact, ctx.path,
                "mod '" + ctx.ns + "' cannot emit event '" + raw
                    + "' of another namespace"));
            return;
        }
        if (event_ids_.count(raw)) return;
        buckets_.refs.push_back(Err("ref.event", ctx.artifact, ctx.path,
                                    "unresolved event ref '" + raw + "'"));
    }

    void CheckConflicts() {
        for (const auto& [target, list] : mutations_by_target_) {
            const MutationDef* repl = nullptr;
            for (const MutationDef* m : list) {
                if (m->mode == "replace") {
                    repl = m;
                    break;
                }
            }
            if (repl == nullptr) continue;
            for (const MutationDef* m : list) {
                if (m == repl) continue;
                if (m->mode != "replace" && m->mode != "wrap"
                    && m->mode != "filter") {
                    continue;
                }
                std::string message;
                if (m->namespace_id == repl->namespace_id) {
                    message = "mutation conflict on target '" + target
                              + "': mod '" + repl->namespace_id
                              + "' mutations '" + repl->mutation_id
                              + "' (replace) and '" + m->mutation_id + "' ("
                              + m->mode + ")";
                } else {
                    message = "mutation conflict on target '" + target
                              + "': mod '" + repl->namespace_id
                              + "' (replace) conflicts with mod '"
                              + m->namespace_id + "' (" + m->mode + ")";
                }
                buckets_.conflicts.push_back(Err(
                    "mutation.conflict", "mutations",
                    repl->namespace_id + "/mutations.json", message));
                break;
            }
        }

        for (const auto& [target, list] : mutations_by_target_) {
            for (const MutationDef* m : list) {
                for (const auto& [remover_ns, removed] :
                     removed_restrictions_) {
                    if (removed != target) continue;
                    if (remover_ns == m->namespace_id) continue;
                    buckets_.conflicts.push_back(Err(
                        "mutation.conflict", "mutations",
                        m->namespace_id + "/mutations.json",
                        "mutation conflict on target '" + target + "': mod '"
                            + m->namespace_id + "' mutates an entry mod '"
                            + remover_ns + "' removes"));
                    break;
                }
            }
        }
    }

    void CheckDeck() {
        if (deck_ == nullptr) return;
        const std::string path = DeckPath(*deck_);
        if (mods_.size() > static_cast<std::size_t>(kMaxMods)) {
            buckets_.deck.push_back(Err("deck.count", "deck", path,
                                        "mod list exceeds 256 mods"));
        }
        for (const LoadedMod* mod : mods_) {
            if (mod->cards.size() > static_cast<std::size_t>(kMaxKindsPerMod)) {
                buckets_.deck.push_back(Err(
                    "deck.count", "deck", path,
                    "mod '" + mod->manifest.id + "' exceeds 4096 kinds"));
            }
        }
        std::set<std::string> active_ns;
        for (const LoadedMod* mod : mods_) active_ns.insert(mod->manifest.id);
        for (const auto& listed : deck_->mods) {
            if (!active_ns.count(listed)) {
                buckets_.deck.push_back(Err(
                    "deck.mod", "deck", path,
                    "deck references mod '" + listed
                        + "' which is not in the active set"));
            }
        }

        for (const auto& [kind, count] : deck_->cards) {
            if (!IsValidKindId(kind) || !kind_ids_.count(kind)) {
                buckets_.deck.push_back(Err("deck.kind", "deck", path,
                                            "unresolved kind '" + kind + "'"));
            }
            if (count < 0 || count > kMaxCopiesPerKind) {
                buckets_.deck.push_back(Err(
                    "deck.count", "deck", path,
                    "count for '" + kind + "' is outside 0.."
                        + std::to_string(kMaxCopiesPerKind)));
            }
        }

        CheckSettings();
    }

    void CheckSettings() {
        const std::string path = DeckPath(*deck_);
        if (!deck_->settings.is_object()) {
            buckets_.deck.push_back(Err("deck.setting", "deck", path,
                                        "settings must be an object"));
            return;
        }
        std::map<std::string, const SettingDecl*> decls;
        for (const LoadedMod* mod : mods_) {
            for (const auto& decl : mod->manifest.settings) {
                decls[decl.id] = &decl;
            }
        }
        for (auto it = deck_->settings.begin(); it != deck_->settings.end();
             ++it) {
            auto decl = decls.find(it.key());
            if (decl != decls.end()) {
                CheckSettingValue(it.key(), it.value(), *decl->second, path);
                continue;
            }
            if (it.value().is_object()) {
                for (auto inner = it.value().begin(); inner != it.value().end();
                     ++inner) {
                    auto inner_decl = decls.find(inner.key());
                    if (inner_decl == decls.end()) {
                        buckets_.deck.push_back(Err(
                            "deck.setting", "deck", path,
                            "unknown setting '" + inner.key() + "'"));
                        continue;
                    }
                    CheckSettingValue(inner.key(), inner.value(),
                                      *inner_decl->second, path);
                }
            } else {
                buckets_.deck.push_back(Err("deck.setting", "deck", path,
                                            "unknown setting '" + it.key()
                                                + "'"));
            }
        }
    }

    void CheckSettingValue(const std::string& id,
                           const nlohmann::json& value,
                           const SettingDecl& decl,
                           const std::string& path) {
        const std::string& type = decl.type;
        bool type_ok = true;
        if (type == "int") type_ok = value.is_number_integer();
        else if (type == "number") type_ok = value.is_number();
        else if (type == "bool") type_ok = value.is_boolean();
        else if (type == "string") type_ok = value.is_string();
        if (!type_ok) {
            buckets_.deck.push_back(Err("deck.setting", "deck", path,
                                        "setting '" + id + "' must be of type "
                                            + type));
            return;
        }
        if (!decl.range || decl.range->is_null()) return;
        double min_value = 0.0;
        double max_value = 0.0;
        bool has_min = false;
        bool has_max = false;
        if (decl.range->is_object()) {
            if (decl.range->contains("min")
                && (*decl.range)["min"].is_number()) {
                min_value = (*decl.range)["min"].get<double>();
                has_min = true;
            }
            if (decl.range->contains("max")
                && (*decl.range)["max"].is_number()) {
                max_value = (*decl.range)["max"].get<double>();
                has_max = true;
            }
        } else if (decl.range->is_array() && decl.range->size() == 2
                   && (*decl.range)[0].is_number()
                   && (*decl.range)[1].is_number()) {
            min_value = (*decl.range)[0].get<double>();
            max_value = (*decl.range)[1].get<double>();
            has_min = true;
            has_max = true;
        } else {
            return;
        }
        if (!value.is_number()) return;
        double n = value.get<double>();
        if ((has_min && n < min_value) || (has_max && n > max_value)) {
            buckets_.deck.push_back(Err(
                "deck.setting", "deck", path,
                "setting '" + id + "' is outside its declared range"));
        }
    }

    std::vector<const LoadedMod*> mods_;
    bool match_set_ = false;
    const DeckDef* deck_ = nullptr;

    Buckets buckets_;
    std::set<std::string> kind_ids_;
    std::set<std::string> rule_ids_;
    std::set<std::string> status_ids_;
    std::set<std::string> tags_;
    std::set<std::string> restriction_ids_;
    std::set<std::string> table_ids_;
    std::set<std::string> event_ids_;
    std::set<std::string> streak_ids_;
    std::vector<std::pair<std::string, std::string>> removed_restrictions_;
    std::map<std::string, std::vector<const MutationDef*>> mutations_by_target_;
};

std::string DeckPath(const DeckDef& deck) {
    if (deck.namespace_id.empty()) return "decks/" + deck.id + ".json";
    return deck.namespace_id + "/decks/" + deck.id + ".json";
}

}  // namespace

// --- SemanticValidator -----------------------------------------------------

SemanticValidator::SemanticValidator(std::string schema_dir)
    : schema_dir_(std::move(schema_dir)) {}

const SemanticValidator::CompiledSchema& SemanticValidator::Schema(
    const std::string& filename) const {
    auto it = schema_cache_.find(filename);
    if (it != schema_cache_.end()) return it->second;

    CompiledSchema compiled;
    std::ifstream in(schema_dir_ + "/" + filename);
    if (!in.is_open()) {
        compiled.missing = true;
        compiled.error =
            "cannot read schema '" + schema_dir_ + "/" + filename + "'";
    } else {
        nlohmann::json doc;
        try {
            in >> doc;
        } catch (const nlohmann::json::parse_error& e) {
            compiled.error = std::string("malformed schema: ") + e.what();
        }
        if (compiled.error.empty()) {
            std::string compile_error;
            compiled.ok = JsonSchemaSubsetValidator::Compile(
                doc, compiled.validator, compile_error);
            compiled.error = compile_error;
        }
    }
    auto inserted = schema_cache_.emplace(filename, std::move(compiled));
    return inserted.first->second;
}

namespace {

std::vector<LoadError> ValidateAgainstSchema(
    const SemanticValidator::CompiledSchema& compiled,
    const nlohmann::json& instance,
    const std::string& artifact,
    const std::string& path) {
    std::vector<LoadError> errors;
    if (compiled.missing) {
        errors.push_back(Err("schema.missing", artifact, path, compiled.error));
        return errors;
    }
    if (!compiled.ok) {
        errors.push_back(
            Err("schema.unsupported", artifact, path, compiled.error));
        return errors;
    }
    for (const auto& violation : compiled.validator.Validate(instance)) {
        errors.push_back(Err(
            "schema.invalid", artifact, path,
            "at " + (violation.path.empty() ? std::string("/")
                                             : violation.path)
                + ": " + violation.message));
    }
    return errors;
}

std::vector<LoadError> SchemaErrors(
    const std::vector<const LoadedMod*>& mods,
    const SemanticValidator& validator,
    const std::vector<const DeckDef*>& decks) {
    std::vector<LoadError> errors;
    for (const LoadedMod* mod : mods) {
        const std::string base = mod->path;
        {
            const auto& compiled = validator.Schema(schema_files::kMod);
            std::vector<LoadError> local = ValidateAgainstSchema(
                compiled, mod->manifest.raw, "mod", base + "/mod.json");
            errors.insert(errors.end(), local.begin(), local.end());
        }
        if (!mod->cards.empty()) {
            /* INFO: Cards are one definition per file; the card schema
             *       describes a single card, so validate each entry against
             *       it and point the error at that entry's file. */
            const auto& compiled = validator.Schema(schema_files::kCards);
            for (const auto& card : mod->cards) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, card.raw, "cards",
                    base + "/cards/" + card.id + ".json");
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
        if (!mod->rules.empty() || !mod->statuses.empty()) {
            nlohmann::json rules = nlohmann::json::object();
            rules["rules"] = nlohmann::json::array();
            for (const auto& rule : mod->rules) rules["rules"].push_back(rule.raw);
            rules["statuses"] = nlohmann::json::array();
            for (const auto& status : mod->statuses) {
                rules["statuses"].push_back(status.raw);
            }
            const auto& compiled = validator.Schema(schema_files::kRules);
            std::vector<LoadError> local = ValidateAgainstSchema(
                compiled, rules, "rules", base + "/rules.json");
            errors.insert(errors.end(), local.begin(), local.end());
        }
        if (!mod->mutations.empty()) {
            nlohmann::json mutations = nlohmann::json::array();
            for (const auto& mut : mod->mutations) {
                mutations.push_back(mut.raw);
            }
            const auto& compiled = validator.Schema(schema_files::kMutations);
            std::vector<LoadError> local = ValidateAgainstSchema(
                compiled, mutations, "mutations", base + "/mutations.json");
            errors.insert(errors.end(), local.begin(), local.end());
        }
        if (!mod->tables.empty()) {
            /* INFO: tables are one definition per file; validate
             *       each entry against the single-table schema and point the
             *       error at that entry's file. */
            const auto& compiled = validator.Schema(schema_files::kTables);
            for (const auto& table : mod->tables) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, table.raw, "tables",
                    base + "/tables/" + table.id + ".json");
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
        if (!mod->entities.empty()) {
            /* INFO: entities are one definition per file; validate
             *       each entry against the single-entity schema and point the
             *       error at that entry's file. */
            const auto& compiled = validator.Schema(schema_files::kEntities);
            for (const auto& entity : mod->entities) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, entity.raw, "entities",
                    base + "/entities/" + entity.id + ".json");
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
        if (!mod->events.empty()) {
            const auto& compiled = validator.Schema(schema_files::kEvents);
            for (const auto& event : mod->events) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, event.raw, "events",
                    base + "/events/" + event.id + ".json");
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
        if (!mod->streaks.empty()) {
            const auto& compiled = validator.Schema(schema_files::kStreaks);
            for (const auto& streak : mod->streaks) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, streak.raw, "streaks",
                    base + "/streaks/" + streak.id + ".json");
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
        if (!mod->derived_events.empty()) {
            const auto& compiled = validator.Schema(schema_files::kDerived);
            for (const auto& derived : mod->derived_events) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, derived.raw, "derived",
                    base + "/derived/" + derived.id + ".json");
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
        if (!mod->glossary.empty()) {
            const auto& compiled = validator.Schema(schema_files::kGlossary);
            for (const auto& entry : mod->glossary) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, entry.raw, "glossary", entry.path);
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
        if (!mod->bindings.empty()) {
            const auto& compiled = validator.Schema(schema_files::kBindings);
            for (const auto& binding : mod->bindings) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, binding.raw, "presentations",
                    base + "/presentations/" + binding.id + ".json");
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
        if (!mod->presentations.empty()) {
            /* INFO: presentations are one definition per file;
             *       semantic checks (kind/target/trigger) report the rest. */
            const auto& compiled =
                validator.Schema(schema_files::kPresentations);
            for (const auto& presentation : mod->presentations) {
                std::vector<LoadError> local = ValidateAgainstSchema(
                    compiled, presentation.raw, "presentations",
                    base + "/presentations/" + presentation.id + ".json");
                errors.insert(errors.end(), local.begin(), local.end());
            }
        }
    }
    for (const DeckDef* deck : decks) {
        const auto& compiled = validator.Schema(schema_files::kDeck);
        std::vector<LoadError> local = ValidateAgainstSchema(
            compiled, deck->raw, "deck", DeckPath(*deck));
        errors.insert(errors.end(), local.begin(), local.end());
    }
    return errors;
}

}  // namespace

namespace {

/* INFO: runs the stages of `kValidationStageOrder` in order. Inside a stage
 *       the errors of the base mod come first. */
std::vector<LoadError> FirstFailingStage(
    const Buckets& buckets, const std::vector<const LoadedMod*>& mods) {
    std::string base_prefix;
    for (const LoadedMod* mod : mods) {
        if (IsBaseModId(mod->manifest.id)) base_prefix = mod->path + "/";
    }
    const auto base_first = [&](std::vector<LoadError> errors) {
        std::stable_partition(
            errors.begin(), errors.end(), [&](const LoadError& error) {
                return !base_prefix.empty()
                       && error.path.rfind(base_prefix, 0) == 0;
            });
        return errors;
    };
    const std::vector<const std::vector<LoadError>*> old_style_kinds = {
        &buckets.ids,       &buckets.refs,      &buckets.tables,
        &buckets.ops,       &buckets.graph,     &buckets.conflicts,
        &buckets.deck,      &buckets.selector,  &buckets.entities,
        &buckets.outcome,   &buckets.asset};
    for (const std::string_view stage : kValidationStageOrder) {
        std::vector<LoadError> found;
        if (stage == "uni") {
            for (const auto* list : old_style_kinds) {
                if (!list->empty()) {
                    found = *list;
                    break;
                }
            }
        } else if (stage == "events") {
            found = buckets.events;
        } else if (stage == "streaks") {
            found = buckets.streaks;
        } else if (stage == "derived") {
            found = buckets.derived;
        } else if (stage == "presentations") {
            found = buckets.presentations;
        } else if (stage == "texts") {
            found = buckets.texts;
        }
        if (!found.empty()) return base_first(std::move(found));
    }
    return {};
}

}  // namespace

std::vector<LoadError> SemanticValidator::ValidateMod(
    const LoadedMod& mod) const {
    std::vector<const LoadedMod*> mods = {&mod};
    std::vector<LoadError> schema_errors =
        SchemaErrors(mods, *this, std::vector<const DeckDef*>{});
    if (!schema_errors.empty()) return schema_errors;

    Checker checker(mods, /*match_set=*/false, nullptr);
    checker.Run();
    return FirstFailingStage(checker.buckets(), mods);
}

std::vector<LoadWarning> SemanticValidator::WarningsOf(
    const LoadedMod& mod) const {
    std::vector<const LoadedMod*> mods = {&mod};
    Checker checker(mods, /*match_set=*/false, nullptr);
    checker.Run();
    return checker.buckets().warnings;
}

std::vector<LoadError> SemanticValidator::ValidateMatchSet(
    const std::vector<LoadedMod>& mods,
    const DeckDef& deck) const {
    std::vector<const LoadedMod*> mod_ptrs;
    mod_ptrs.reserve(mods.size());
    for (const auto& mod : mods) mod_ptrs.push_back(&mod);

    std::vector<const DeckDef*> decks = {&deck};
    std::vector<LoadError> schema_errors = SchemaErrors(mod_ptrs, *this, decks);
    if (!schema_errors.empty()) return schema_errors;

    Checker checker(mod_ptrs, /*match_set=*/true, &deck);
    checker.Run();
    return FirstFailingStage(checker.buckets(), mod_ptrs);
}

void ValidateLoadedMods(LoadResult& result,
                        const SemanticValidator& validator) {
    const SemanticValidator::CompiledSchema& mod_schema =
        validator.Schema(schema_files::kMod);
    if (mod_schema.missing) {
        Logger::Error("[Mods] schema directory unreadable; the validation ",
                      "stages are skipped: ", mod_schema.error);
        return;
    }
    std::vector<LoadedMod> kept;
    for (LoadedMod& mod : result.mods) {
        std::vector<LoadError> errors = validator.ValidateMod(mod);
        std::vector<LoadWarning> warnings = validator.WarningsOf(mod);
        for (ModReport& report : result.reports) {
            if (report.mod_id != mod.manifest.id) continue;
            report.warnings.insert(report.warnings.end(), warnings.begin(),
                                   warnings.end());
        }
        result.warnings.insert(result.warnings.end(), warnings.begin(),
                               warnings.end());
        if (errors.empty()) {
            kept.push_back(std::move(mod));
            continue;
        }
        for (ModReport& report : result.reports) {
            if (report.mod_id == mod.manifest.id) {
                report.ok = false;
                report.errors.insert(report.errors.end(), errors.begin(),
                                     errors.end());
            }
        }
        for (const LoadError& error : errors) {
            Logger::Error("[Mods] rejected '", mod.manifest.id, "': ",
                          error.check, " ", error.message);
            result.errors.push_back(error);
        }
    }
    result.mods = std::move(kept);
}

}  // namespace match::modload
