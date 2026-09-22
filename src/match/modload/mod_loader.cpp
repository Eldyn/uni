#include "match/modload/mod_loader.hpp"

#include "common/env.hpp"
#include "logger.hpp"

#include <common/contract.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace match::modload {

namespace {

constexpr int kLocalIdMaxLength = 32;
constexpr const char* kModsDefaultDir = "mods/";

/* INFO: resource ceilings for untrusted mod content. Both are enforced before
 *       the data can reach the semantic validator (which runs later, on lobby
 *       requests, and only after the whole file has already been parsed). */
constexpr std::uintmax_t kMaxJsonFileBytes = 16ULL * 1024ULL * 1024ULL;
constexpr int kMaxJsonDepth = 64;

bool HasValidIdChars(const std::string& id) {
    if (id.empty()) return false;
    for (unsigned char c : id) {
        if (!(std::islower(c) || std::isdigit(c) || c == '_')) return false;
    }
    return true;
}

/** Thrown by the parse-depth callback to abort an over-nested document. */
struct JsonDepthExceeded : std::exception {
    const char* what() const noexcept override {
        return "JSON nesting depth exceeded";
    }
};

/** INFO: nlohmann's parser invokes this per nesting level; throwing here stops
 *       the recursive descent before it can exhaust the stack. */
bool DepthGuardCallback(int depth, nlohmann::json::parse_event_t,
                        nlohmann::json&) {
    if (depth > kMaxJsonDepth) throw JsonDepthExceeded{};
    return true;
}

/** INFO: `provides_*` is an untrusted bare filename. Mirrors IsValidLocalId's
 *       strictness and additionally forbids path syntax: separators, parent
 *       segments, absolute markers and a leading dot. */
bool IsSafeProvidesValue(const std::string& value) {
    if (value.empty()) return false;
    if (value.front() == '.') return false;
    if (value.find('/') != std::string::npos) return false;
    if (value.find('\\') != std::string::npos) return false;
    if (value.find("..") != std::string::npos) return false;
    if (value.find(':') != std::string::npos) return false;
    return true;
}

/** INFO: component-wise prefix test; true when `candidate` is `root` or below
 *       it. Both paths must already be canonical. */
bool IsPathWithin(const fs::path& root, const fs::path& candidate) {
    auto root_it = root.begin();
    auto cand_it = candidate.begin();
    for (; root_it != root.end() && cand_it != candidate.end();
         ++root_it, ++cand_it) {
        if (*root_it != *cand_it) return false;
    }
    return root_it == root.end();
}

/* INFO: loader errors are collected, never thrown, so a bad folder yields a
 *       structured report instead of aborting the whole scan. */
void AddError(std::vector<LoadError>& errors,
              const std::string& check,
              const std::string& artifact,
              const std::string& path,
              const std::string& message) {
    errors.push_back(LoadError{check, artifact, path, message});
}

/** INFO: resolves a `provides_*` value under `canonical_root` and rejects both
 *       lexical escapes and symlinks that point outside the mod folder. */
bool ResolveProvidesPath(const fs::path& canonical_root,
                         const std::string& value,
                         const std::string& artifact,
                         std::vector<LoadError>& errors,
                         fs::path& out) {
    if (!IsSafeProvidesValue(value)) {
        AddError(errors, "provides.invalid", artifact, value,
                 "provides_* must be a bare filename without '/', '\\', '..', "
                 "a leading '.' or ':'");
        return false;
    }
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(canonical_root / value, ec);
    if (ec) {
        AddError(errors, "provides.invalid", artifact, value,
                 "provides_* path could not be resolved");
        return false;
    }
    if (!IsPathWithin(canonical_root, resolved)) {
        AddError(errors, "provides.invalid", artifact, value,
                 "provides_* resolves outside the mod folder");
        return false;
    }
    out = std::move(resolved);
    return true;
}

bool ReadJsonFile(const fs::path& path,
                  nlohmann::json& out,
                  std::vector<LoadError>& errors,
                  const std::string& artifact) {
    /* INFO: reject non-regular targets (directories, FIFOs, devices) before
     *       opening, so an attacker cannot hang the scanning thread on a
     *       blocking read. */
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        AddError(errors, "file.read", artifact, path.string(),
                 "cannot open or read file");
        return false;
    }
    std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        AddError(errors, "file.read", artifact, path.string(),
                 "cannot open or read file");
        return false;
    }
    if (size > kMaxJsonFileBytes) {
        AddError(errors, "file.too_large", artifact, path.string(),
                 "file is " + std::to_string(size) + " bytes, exceeding the "
                     + std::to_string(kMaxJsonFileBytes) + "-byte limit");
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        AddError(errors, "file.read", artifact, path.string(),
                 "cannot open or read file");
        return false;
    }
    try {
        out = nlohmann::json::parse(file, DepthGuardCallback);
    } catch (const JsonDepthExceeded&) {
        AddError(errors, "json.depth", artifact, path.string(),
                 "JSON nesting exceeds " + std::to_string(kMaxJsonDepth)
                     + " levels");
        return false;
    } catch (const nlohmann::json::parse_error& e) {
        AddError(errors, "json.parse", artifact, path.string(),
                 std::string("malformed JSON: ") + e.what());
        return false;
    }
    return true;
}

bool ParseStringField(const nlohmann::json& obj,
                      const std::string& key,
                      std::string& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

bool ParseOptionalStringField(const nlohmann::json& obj,
                              const std::string& key,
                              std::optional<std::string>& out) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return true;
    if (!it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

BehaviorGraph ParseGraph(const nlohmann::json& value) {
    BehaviorGraph graph;
    graph.raw = value;
    if (value.is_object() && value.contains("nodes") && value["nodes"].is_array()) {
        graph.nodes = value["nodes"];
    }
    return graph;
}

bool ParseBehaviorEntry(const nlohmann::json& value,
                        BehaviorEntry& out,
                        std::vector<LoadError>& errors,
                        const std::string& artifact,
                        const std::string& path,
                        const std::string& hook_name) {
    if (!value.is_object()) {
        AddError(errors, "behavior.shape", artifact, path,
                 "behavior entry '" + hook_name + "' must be an object");
        return false;
    }
    out.hook = hook_name;
    (void)ParseOptionalStringField(value, "phase", out.phase);
    if (value.contains("where")) out.where = value["where"];
    if (value.contains("nodes")) {
        out.graph = ParseGraph(value);
    } else if (value.contains("graph")) {
        out.graph = ParseGraph(value["graph"]);
    } else {
        out.graph = ParseGraph(nlohmann::json::object());
    }
    return true;
}

bool ParseFace(const nlohmann::json& value,
               FaceSpec& out,
               std::vector<LoadError>& errors,
               const std::string& path) {
    if (!value.is_object()) {
        AddError(errors, "card.face", "cards", path,
                 "card face must be an object");
        return false;
    }
    std::string kind_token;
    if (!ParseStringField(value, "kind", kind_token)) {
        AddError(errors, "card.face", "cards", path,
                 "card face requires a string 'kind'");
        return false;
    }
    auto kind = FaceKindFromString(kind_token);
    if (!kind) {
        AddError(errors, "card.face", "cards", path,
                 "unknown face kind '" + kind_token + "'");
        return false;
    }
    out.kind = *kind;
    (void)ParseOptionalStringField(value, "color", out.color);
    (void)ParseOptionalStringField(value, "label", out.label);
    (void)ParseOptionalStringField(value, "url", out.url);
    auto av = value.find("art_version");
    if (av != value.end() && av->is_number_integer()) {
        out.art_version = av->get<int>();
    }
    return true;
}

bool ParseWindow(const nlohmann::json& value,
                 WindowSpec& out,
                 std::vector<LoadError>& errors,
                 const std::string& path) {
    if (!value.is_object()) {
        AddError(errors, "card.window", "cards", path,
                 "card window must be an object");
        return false;
    }
    out.raw = value;
    auto when = value.find("when_played");
    if (when == value.end() || !when->is_object()) {
        AddError(errors, "card.window", "cards", path,
                 "card window requires a 'when_played' object");
        return false;
    }
    (void)ParseStringField(*when, "responders", out.responders);
    (void)ParseStringField(*when, "duration", out.duration);
    (void)ParseStringField(*when, "on_response", out.on_response);
    (void)ParseStringField(*when, "default", out.default_route);
    auto rw = when->find("respond_with");
    if (rw != when->end()) out.respond_with = *rw;
    return true;
}

bool ParseAutoTrigger(const nlohmann::json& value,
                      AutoTriggerDef& out,
                      std::vector<LoadError>& errors,
                      const std::string& path) {
    if (!value.is_object()) {
        AddError(errors, "card.auto_trigger", "cards", path,
                 "card auto_trigger must be an object");
        return false;
    }
    auto condition = value.find("condition");
    if (condition == value.end() || !condition->is_object()) {
        AddError(errors, "card.auto_trigger", "cards", path,
                 "card auto_trigger requires a 'condition' object");
        return false;
    }
    out.condition = *condition;
    auto graph = value.find("graph");
    if (graph == value.end() || !graph->is_object()) {
        AddError(errors, "card.auto_trigger", "cards", path,
                 "card auto_trigger requires a 'graph' object");
        return false;
    }
    auto nodes = graph->find("nodes");
    if (nodes == graph->end() || !nodes->is_array()) {
        AddError(errors, "card.auto_trigger", "cards", path,
                 "card auto_trigger graph requires a 'nodes' array");
        return false;
    }
    out.graph = *graph;
    auto must = value.find("must_apply");
    if (must != value.end()) {
        if (!must->is_boolean()) {
            AddError(errors, "card.auto_trigger", "cards", path,
                     "card auto_trigger 'must_apply' must be a boolean");
            return false;
        }
        out.must_apply = must->get<bool>();
    }
    return true;
}

void ParseSettingsDecls(const nlohmann::json& manifest,
                        ModManifest& out) {
    auto it = manifest.find("settings");
    if (it == manifest.end() || !it->is_array()) return;
    for (const auto& entry : *it) {
        if (!entry.is_object()) continue;
        SettingDecl decl;
        (void)ParseStringField(entry, "id", decl.id);
        (void)ParseStringField(entry, "type", decl.type);
        (void)ParseStringField(entry, "description", decl.description);
        auto def = entry.find("default");
        if (def != entry.end()) decl.default_value = *def;
        auto range = entry.find("range");
        if (range != entry.end()) decl.range = *range;
        decl.raw = entry;
        out.settings.push_back(std::move(decl));
    }
}

bool ParseManifest(const fs::path& path,
                   ModManifest& out,
                   std::vector<LoadError>& errors) {
    nlohmann::json json;
    if (!ReadJsonFile(path, json, errors, "mod")) return false;
    if (!json.is_object()) {
        AddError(errors, "manifest.shape", "mod", path.string(),
                 "mod.json must be a JSON object");
        return false;
    }
    out.raw = json;

    static const char* kRequired[] = {"id", "name", "version", "api"};
    bool had_required_error = false;
    for (const char* key : kRequired) {
        std::string value;
        if (!ParseStringField(json, key, value)) {
            AddError(errors, "manifest.required", "mod", path.string(),
                     std::string("mod.json requires a string '") + key + "'");
            had_required_error = true;
        }
    }
    if (had_required_error) return false;

    (void)ParseStringField(json, "id", out.id);
    (void)ParseStringField(json, "name", out.name);
    (void)ParseStringField(json, "version", out.version);
    (void)ParseStringField(json, "api", out.api);
    (void)ParseStringField(json, "description", out.description);
    (void)ParseStringField(json, "author", out.author);
    (void)ParseOptionalStringField(json, "provides_cards", out.provides_cards);
    (void)ParseOptionalStringField(json, "provides_rules", out.provides_rules);
    (void)ParseOptionalStringField(json, "provides_mutations",
                                   out.provides_mutations);
    ParseSettingsDecls(json, out);
    auto prompts = json.find("prompts");
    if (prompts != json.end()) out.prompts = *prompts;
    auto signals = json.find("signals");
    if (signals != json.end()) out.signals = *signals;
    return true;
}

bool ParseCardsFile(const fs::path& path,
                    const std::string& ns,
                    std::vector<CardDef>& out,
                    std::vector<LoadError>& errors) {
    nlohmann::json json;
    if (!ReadJsonFile(path, json, errors, "cards")) return false;
    if (!json.is_array()) {
        AddError(errors, "cards.shape", "cards", path.string(),
                 "cards.json must be a JSON array");
        return false;
    }
    std::set<std::string> seen;
    bool ok = true;
    for (const auto& entry : json) {
        CardDef card;
        if (!entry.is_object()
            || !ParseStringField(entry, "id", card.id)
            || !IsValidLocalId(card.id)) {
            AddError(errors, "card.id", "cards", path.string(),
                     "every card needs an id matching [a-z0-9_]+ (1-32)");
            ok = false;
            continue;
        }
        if (!seen.insert(card.id).second) {
            AddError(errors, "card.id.duplicate", "cards", path.string(),
                     "duplicate card id '" + card.id + "'");
            ok = false;
            continue;
        }
        card.namespace_id = ns;
        card.kind_id = ns + ":" + card.id;
        card.raw = entry;
        (void)ParseStringField(entry, "title", card.title);

        auto face = entry.find("face");
        if (face != entry.end()) {
            if (!ParseFace(*face, card.face, errors, path.string())) ok = false;
        }
        auto tags = entry.find("tags");
        if (tags != entry.end() && tags->is_array()) {
            for (const auto& tag : *tags) {
                if (tag.is_string()) card.tags.push_back(tag.get<std::string>());
            }
        }
        auto behavior = entry.find("behavior");
        if (behavior != entry.end() && behavior->is_object()) {
            for (auto it = behavior->begin(); it != behavior->end(); ++it) {
                BehaviorEntry be;
                if (ParseBehaviorEntry(it.value(), be, errors, "cards",
                                       path.string(), it.key())) {
                    card.behaviors.push_back(std::move(be));
                } else {
                    ok = false;
                }
            }
        }
        auto window = entry.find("window");
        if (window != entry.end()) {
            WindowSpec spec;
            if (ParseWindow(*window, spec, errors, path.string())) {
                card.window = std::move(spec);
            } else {
                ok = false;
            }
        }
        auto auto_trigger = entry.find("auto_trigger");
        if (auto_trigger != entry.end()) {
            AutoTriggerDef spec;
            if (ParseAutoTrigger(*auto_trigger, spec, errors, path.string())) {
                card.auto_trigger = std::move(spec);
            } else {
                ok = false;
            }
        }
        out.push_back(std::move(card));
    }
    return ok;
}

bool ParseStatusEntry(const nlohmann::json& entry,
                      const std::string& ns,
                      StatusDef& out) {
    if (!entry.is_object()) return false;
    if (!ParseStringField(entry, "id", out.id) || !IsValidLocalId(out.id)) {
        return false;
    }
    out.namespace_id = ns;
    out.status_id = ns + ":" + out.id;
    out.raw = entry;
    (void)ParseStringField(entry, "title", out.title);
    (void)ParseStringField(entry, "stack_policy", out.stack_policy);
    auto hidden = entry.find("hidden");
    if (hidden != entry.end() && hidden->is_boolean()) {
        out.hidden = hidden->get<bool>();
    }
    return true;
}

bool ParseRuleEntry(const nlohmann::json& entry,
                    const std::string& ns,
                    RuleDef& out,
                    std::vector<LoadError>& errors,
                    const std::string& path) {
    if (!entry.is_object()) return false;
    if (!ParseStringField(entry, "id", out.id) || !IsValidLocalId(out.id)) {
        return false;
    }
    out.namespace_id = ns;
    out.rule_id = ns + ":" + out.id;
    out.raw = entry;
    (void)ParseStringField(entry, "title", out.title);
    (void)ParseStringField(entry, "description", out.description);

    bool ok = true;
    auto hooks = entry.find("hooks");
    if (hooks == entry.end()) return ok;
    if (!hooks->is_array()) {
        AddError(errors, "rules.hook", "rules", path,
                 "rule '" + out.id + "' hooks must be an array");
        return false;
    }
    for (const auto& hook : *hooks) {
        if (!hook.is_object()) {
            AddError(errors, "rules.hook", "rules", path,
                     "rule '" + out.id + "' has a non-object hook entry");
            ok = false;
            continue;
        }
        std::string on;
        if (!ParseStringField(hook, "on", on)) {
            AddError(errors, "rules.hook", "rules", path,
                     "rule '" + out.id + "' hook requires a string 'on'");
            ok = false;
            continue;
        }
        BehaviorEntry be;
        be.hook = on;
        (void)ParseOptionalStringField(hook, "phase", be.phase);
        auto where = hook.find("where");
        if (where != hook.end()) be.where = *where;
        /* INFO: mirror card behavior: `nodes` inline or nested under `graph`,
         *       both accepted by rules.schema.json. */
        if (hook.contains("nodes")) {
            be.graph = ParseGraph(hook);
        } else if (hook.contains("graph")) {
            be.graph = ParseGraph(hook["graph"]);
        } else {
            be.graph = ParseGraph(nlohmann::json::object());
        }
        out.hooks.push_back(std::move(be));
    }
    return ok;
}

bool ParseRulesFile(const fs::path& path,
                    const std::string& ns,
                    std::vector<RuleDef>& rules,
                    std::vector<StatusDef>& statuses,
                    std::vector<LoadError>& errors) {
    nlohmann::json json;
    if (!ReadJsonFile(path, json, errors, "rules")) return false;
    bool ok = true;
    std::set<std::string> seen;

    auto parse_list = [&](const char* key, auto parse_one) {
        auto it = json.find(key);
        if (it == json.end()) return;
        if (!it->is_array()) {
            AddError(errors, std::string("rules.shape"), "rules", path.string(),
                     std::string("'") + key + "' must be an array");
            ok = false;
            return;
        }
        for (const auto& entry : *it) {
            std::size_t before = errors.size();
            if (parse_one(entry)) continue;
            if (errors.size() == before) {
                AddError(errors, "rules.id", "rules", path.string(),
                         std::string("every ") + key
                             + " entry needs an id matching [a-z0-9_]+ (1-32)");
            }
            ok = false;
        }
    };

    if (json.is_array()) {
        /* INFO: bare array form: rule entries only. */
        for (const auto& entry : json) {
            RuleDef rule;
            std::size_t before = errors.size();
            if (!ParseRuleEntry(entry, ns, rule, errors, path.string())) {
                if (errors.size() == before) {
                    AddError(errors, "rules.id", "rules", path.string(),
                             "rule entry needs an id matching [a-z0-9_]+ (1-32)");
                }
                ok = false;
                continue;
            }
            if (!seen.insert(rule.id).second) {
                AddError(errors, "rule.id.duplicate", "rules", path.string(),
                         "duplicate rule id '" + rule.id + "'");
                ok = false;
                continue;
            }
            rules.push_back(std::move(rule));
        }
        return ok;
    }
    if (!json.is_object()) {
        AddError(errors, "rules.shape", "rules", path.string(),
                 "rules.json must be a JSON object or array");
        return false;
    }

    auto parse_rule = [&](const nlohmann::json& entry) {
        RuleDef rule;
        std::size_t before = errors.size();
        if (!ParseRuleEntry(entry, ns, rule, errors, path.string())) {
            if (errors.size() == before) {
                AddError(errors, "rules.id", "rules", path.string(),
                         "rule entry needs an id matching [a-z0-9_]+ (1-32)");
            }
            return false;
        }
        if (!seen.insert(rule.id).second) {
            AddError(errors, "rule.id.duplicate", "rules", path.string(),
                     "duplicate rule id '" + rule.id + "'");
            return false;
        }
        rules.push_back(std::move(rule));
        return true;
    };
    auto parse_status = [&](const nlohmann::json& entry) {
        StatusDef status;
        if (!ParseStatusEntry(entry, ns, status)) return false;
        if (!seen.insert(status.id).second) {
            AddError(errors, "status.id.duplicate", "rules", path.string(),
                     "duplicate status id '" + status.id + "'");
            return false;
        }
        statuses.push_back(std::move(status));
        return true;
    };

    if (json.contains("rules")) parse_list("rules", parse_rule);
    if (json.contains("statuses")) parse_list("statuses", parse_status);
    /* INFO: flat form: `{"id": ..., "hooks": [...]}` rule entries directly,
     *       which is how the rule example reads. */
    if (!json.contains("rules") && !json.contains("statuses")
        && json.contains("hooks")) {
        if (!parse_rule(json)) ok = false;
    }
    return ok;
}

bool ParseMutationsFile(const fs::path& path,
                        const std::string& ns,
                        std::vector<MutationDef>& out,
                        std::vector<LoadError>& errors) {
    nlohmann::json json;
    if (!ReadJsonFile(path, json, errors, "mutations")) return false;
    if (!json.is_array()) {
        AddError(errors, "mutations.shape", "mutations", path.string(),
                 "mutations.json must be a JSON array");
        return false;
    }
    std::set<std::string> seen;
    bool ok = true;
    for (const auto& entry : json) {
        MutationDef mut;
        if (!entry.is_object()
            || !ParseStringField(entry, "id", mut.id)
            || !IsValidLocalId(mut.id)) {
            AddError(errors, "mutation.id", "mutations", path.string(),
                     "every mutation needs an id matching [a-z0-9_]+ (1-32)");
            ok = false;
            continue;
        }
        if (!seen.insert(mut.id).second) {
            AddError(errors, "mutation.id.duplicate", "mutations", path.string(),
                     "duplicate mutation id '" + mut.id + "'");
            ok = false;
            continue;
        }
        mut.namespace_id = ns;
        mut.mutation_id = ns + ":" + mut.id;
        mut.raw = entry;
        (void)ParseStringField(entry, "target", mut.target);
        if (!ParseStringField(entry, "mode", mut.mode)) {
            AddError(errors, "mutation.mode", "mutations", path.string(),
                     "mutation '" + mut.id + "' requires a string 'mode'");
            ok = false;
        }
        (void)ParseOptionalStringField(entry, "position", mut.position);
        auto where = entry.find("where");
        if (where != entry.end()) mut.where = *where;
        auto replacement = entry.find("replacement");
        if (replacement != entry.end()) {
            mut.replacement = ParseGraph(*replacement);
        }
        out.push_back(std::move(mut));
    }
    return ok;
}

bool ParseDeckFile(const fs::path& path,
                   const std::string& ns,
                   DeckDef& out,
                   std::vector<LoadError>& errors) {
    nlohmann::json json;
    if (!ReadJsonFile(path, json, errors, "deck")) return false;
    if (!json.is_object()) {
        AddError(errors, "deck.shape", "deck", path.string(),
                 "deck file must be a JSON object");
        return false;
    }
    out.raw = json;
    if (!ParseStringField(json, "id", out.id) || !IsValidLocalId(out.id)) {
        AddError(errors, "deck.id", "deck", path.string(),
                 "deck requires an id matching [a-z0-9_]+ (1-32)");
        return false;
    }
    out.namespace_id = ns;
    out.deck_id = ns + ":" + out.id;
    (void)ParseStringField(json, "name", out.name);
    auto mods = json.find("mods");
    if (mods != json.end() && mods->is_array()) {
        for (const auto& mod : *mods) {
            if (mod.is_string()) out.mods.push_back(mod.get<std::string>());
        }
    }
    auto cards = json.find("cards");
    if (cards != json.end()) {
        if (!cards->is_object()) {
            AddError(errors, "deck.cards", "deck", path.string(),
                     "'cards' must be an object of kind id -> count");
            return false;
        }
        for (auto it = cards->begin(); it != cards->end(); ++it) {
            if (!it.value().is_number_integer()) {
                AddError(errors, "deck.cards", "deck", path.string(),
                         "count for '" + it.key() + "' must be an integer");
                return false;
            }
            out.cards.emplace_back(it.key(), it.value().get<int>());
        }
    }
    auto settings = json.find("settings");
    if (settings != json.end()) out.settings = *settings;
    return true;
}

bool ScanDeckDirectory(const fs::path& dir,
                       const std::string& ns,
                       std::vector<DeckDef>& out,
                       std::vector<LoadError>& errors) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return true;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    bool ok = true;
    std::set<std::string> seen;
    for (const auto& file : files) {
        DeckDef deck;
        if (!ParseDeckFile(file, ns, deck, errors)) {
            ok = false;
            continue;
        }
        if (!seen.insert(deck.id).second) {
            AddError(errors, "deck.id.duplicate", "deck", file.string(),
                     "duplicate deck id '" + deck.id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(deck));
    }
    return ok;
}

}  // namespace

std::string ToString(FaceKind kind) {
    switch (kind) {
        case FaceKind::kText: return "text";
        case FaceKind::kArtRef: return "art_ref";
        case FaceKind::kEmoji: return "emoji";
        case FaceKind::kBlank: return "blank";
    }
    return "blank";
}

std::optional<FaceKind> FaceKindFromString(const std::string& token) {
    if (token == "text") return FaceKind::kText;
    if (token == "art_ref") return FaceKind::kArtRef;
    if (token == "emoji") return FaceKind::kEmoji;
    if (token == "blank") return FaceKind::kBlank;
    return std::nullopt;
}

int EngineModApiVersion() {
    return contract::kModApiVersion;
}

bool IsValidLocalId(const std::string& id) {
    return !id.empty() && id.size() <= kLocalIdMaxLength && HasValidIdChars(id);
}

bool IsValidKindId(const std::string& id) {
    auto colon = id.find(':');
    if (colon == std::string::npos) return false;
    if (id.find(':', colon + 1) != std::string::npos) return false;
    return IsValidLocalId(id.substr(0, colon))
        && IsValidLocalId(id.substr(colon + 1));
}

bool ParseApiVersion(const nlohmann::json& value, int& out) {
    if (value.is_number_integer()) {
        out = value.get<int>();
        return true;
    }
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        if (text.empty()) return false;
        try {
            size_t consumed = 0;
            int parsed = std::stoi(text, &consumed);
            if (consumed != text.size()) return false;
            out = parsed;
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool ParseModFolder(const std::string& folder_path,
                    const std::string& folder_name,
                    LoadedMod& out,
                    std::vector<LoadError>& errors) {
    fs::path root(folder_path);
    fs::path manifest_path = root / "mod.json";

    ModManifest manifest;
    if (!ParseManifest(manifest_path, manifest, errors)) return false;

    if (!IsValidLocalId(manifest.id)) {
        AddError(errors, "manifest.id", "mod", manifest_path.string(),
                 "manifest id '" + manifest.id
                     + "' must match [a-z0-9_]+ (1-32 chars)");
        return false;
    }

    int api = 0;
    if (!ParseApiVersion(manifest.raw.at("api"), api)) {
        AddError(errors, "manifest.api", "mod", manifest_path.string(),
                 "manifest 'api' must be an integer or an integer string");
        return false;
    }
    if (api > EngineModApiVersion()) {
        AddError(errors, "manifest.api", "mod", manifest_path.string(),
                 "mod targets api " + std::to_string(api)
                     + " but this engine supports at most "
                     + std::to_string(EngineModApiVersion()));
        return false;
    }

    LoadedMod loaded;
    loaded.folder = folder_name;
    loaded.path = folder_path;
    loaded.manifest = std::move(manifest);

    /* INFO: canonical mod root; every `provides_*` target must resolve inside
     *       it, so a symlink cannot smuggle a path out of the folder. */
    std::error_code root_ec;
    fs::path canonical_root = fs::weakly_canonical(root, root_ec);
    if (root_ec) canonical_root = root;

    bool ok = true;
    if (loaded.manifest.provides_cards) {
        fs::path cards_path;
        if (!ResolveProvidesPath(canonical_root, *loaded.manifest.provides_cards,
                                 "cards", errors, cards_path)) {
            ok = false;
        } else if (!fs::exists(cards_path)) {
            AddError(errors, "provides.missing", "cards", cards_path.string(),
                     "manifest declares provides_cards but the file is absent");
            ok = false;
        } else if (!ParseCardsFile(cards_path, loaded.manifest.id,
                                   loaded.cards, errors)) {
            ok = false;
        }
    }
    if (loaded.manifest.provides_rules) {
        fs::path rules_path;
        if (!ResolveProvidesPath(canonical_root, *loaded.manifest.provides_rules,
                                 "rules", errors, rules_path)) {
            ok = false;
        } else if (!fs::exists(rules_path)) {
            AddError(errors, "provides.missing", "rules", rules_path.string(),
                     "manifest declares provides_rules but the file is absent");
            ok = false;
        } else if (!ParseRulesFile(rules_path, loaded.manifest.id,
                                   loaded.rules, loaded.statuses, errors)) {
            ok = false;
        }
    }
    if (loaded.manifest.provides_mutations) {
        fs::path mut_path;
        if (!ResolveProvidesPath(canonical_root,
                                 *loaded.manifest.provides_mutations,
                                 "mutations", errors, mut_path)) {
            ok = false;
        } else if (!fs::exists(mut_path)) {
            AddError(errors, "provides.missing", "mutations", mut_path.string(),
                     "manifest declares provides_mutations but the file is absent");
            ok = false;
        } else if (!ParseMutationsFile(mut_path, loaded.manifest.id,
                                       loaded.mutations, errors)) {
            ok = false;
        }
    }
    if (!ScanDeckDirectory(root / "decks", loaded.manifest.id,
                           loaded.decks, errors)) {
        ok = false;
    }

    if (!ok) return false;
    out = std::move(loaded);
    return true;
}

LoadResult ScanModsDirectory(const std::string& root) {
    LoadResult result;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        AddError(result.errors, "root.scan", "mods", root,
                 "mods root is not a readable directory");
        return result;
    }

    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory()) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());

    std::unordered_set<std::string> seen_ids;
    std::vector<LoadedMod> mods;
    bool ok = true;
    for (const auto& folder : folders) {
        LoadedMod mod;
        if (!ParseModFolder(folder.string(), folder.filename().string(),
                            mod, result.errors)) {
            ok = false;
            continue;
        }
        if (!seen_ids.insert(mod.manifest.id).second) {
            AddError(result.errors, "mod.id.duplicate", "mod",
                     (folder / "mod.json").string(),
                     "duplicate mod id '" + mod.manifest.id + "'");
            ok = false;
            continue;
        }
        mods.push_back(std::move(mod));
    }

    if (!ok) {
        result.mods.clear();
        return result;
    }

    /* INFO: deterministic ordering: sort by manifest id so match assembly
     *       sees a stable mod list regardless of filesystem enumeration. */
    std::sort(mods.begin(), mods.end(),
              [](const LoadedMod& a, const LoadedMod& b) {
                  return a.manifest.id < b.manifest.id;
              });
    result.mods = std::move(mods);
    return result;
}

LoadResult LoadModsFromEnv() {
    std::string root = Env::Get("UNI_MODS_DIR", kModsDefaultDir);
    Logger::Info("[ModLoad] Scanning mods root '" + root + "'");
    LoadResult result = ScanModsDirectory(root);
    if (!result.ok()) {
        for (const auto& err : result.errors) {
            Logger::Error("[ModLoad] " + err.check + " (" + err.artifact + ") "
                          + err.path + ": " + err.message);
        }
    } else {
        Logger::Info("[ModLoad] Loaded " + std::to_string(result.mods.size())
                     + " mod(s)");
    }
    return result;
}

}  // namespace match::modload
