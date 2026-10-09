#include "match/modload/semantic_validator.hpp"

#include "logger.hpp"
#include "match/modload/base_mod.hpp"
#include "match/modload/event_definition_keys.hpp"
#include "match/modload/mod_loader.hpp"
#include "match/modload/vocabulary.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
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
    std::vector<LoadError> presentations;
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
        CheckPresentations();
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
 *       the errors of the base mod come first. The "streaks" and "texts"
 *       stages have no checks yet; they keep their place in the order. */
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
        } else if (stage == "presentations") {
            found = buckets.presentations;
        }
        // TODO: the "streaks" stage lands with the streak ticket and the
        //       "texts" stage with the texts ticket.
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
