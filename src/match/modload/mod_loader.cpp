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

/* INFO: loader errors are collected, never thrown, so a bad folder yields a
 *       structured report instead of aborting the whole scan. */
void AddError(std::vector<LoadError>& errors,
              const std::string& check,
              const std::string& artifact,
              const std::string& path,
              const std::string& message) {
    errors.push_back(LoadError{check, artifact, path, message});
}

/** INFO: non-blocking counterpart to AddError: warnings never block. */
void AddWarning(std::vector<LoadWarning>& warnings,
                const std::string& check,
                const std::string& artifact,
                const std::string& path,
                const std::string& message) {
    warnings.push_back(LoadWarning{check, artifact, path, message});
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
    (void)ParseOptionalStringField(value, "art", out.art);
    (void)ParseStringField(value, "art_mode", out.art_mode);
    (void)ParseStringField(value, "art_fit", out.art_fit);
    auto keep = value.find("keep");
    if (keep != value.end() && keep->is_array()) {
        for (const auto& layer : *keep) {
            if (layer.is_string()) out.keep.push_back(layer.get<std::string>());
        }
    }
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
    ParseSettingsDecls(json, out);
    auto prompts = json.find("prompts");
    if (prompts != json.end()) out.prompts = *prompts;
    auto signals = json.find("signals");
    if (signals != json.end()) out.signals = *signals;
    return true;
}

/* INFO: List a directory's `*.json` files in sorted filename order. A
 *       missing directory is an empty entry set; an unreadable one is
 *       reported once against `artifact`. */
std::vector<fs::path> ListJsonFiles(const fs::path& dir,
                                    const std::string& artifact,
                                    std::vector<LoadError>& errors) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    if (ec) {
        AddError(errors, "entryset.read", artifact, dir.string(),
                 "cannot read entry-set directory");
        return {};
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool ParseCardEntry(const fs::path& path,
                    const std::string& ns,
                    CardDef& out,
                    std::vector<LoadError>& errors) {
    nlohmann::json entry;
    if (!ReadJsonFile(path, entry, errors, "cards")) return false;
    if (!entry.is_object()) {
        AddError(errors, "card.shape", "cards", path.string(),
                 "card file must be a JSON object");
        return false;
    }
    if (!ParseStringField(entry, "id", out.id) || !IsValidLocalId(out.id)) {
        AddError(errors, "card.id", "cards", path.string(),
                 "card needs an id matching [a-z0-9_]+ (1-32)");
        return false;
    }
    out.namespace_id = ns;
    out.kind_id = ns + ":" + out.id;
    out.raw = entry;
    (void)ParseStringField(entry, "title", out.title);

    bool ok = true;
    auto face = entry.find("face");
    if (face != entry.end()) {
        if (!ParseFace(*face, out.face, errors, path.string())) ok = false;
    }
    auto tags = entry.find("tags");
    if (tags != entry.end() && tags->is_array()) {
        for (const auto& tag : *tags) {
            if (tag.is_string()) out.tags.push_back(tag.get<std::string>());
        }
    }
    auto behavior = entry.find("behavior");
    if (behavior != entry.end() && behavior->is_object()) {
        for (auto it = behavior->begin(); it != behavior->end(); ++it) {
            BehaviorEntry be;
            if (ParseBehaviorEntry(it.value(), be, errors, "cards",
                                   path.string(), it.key())) {
                out.behaviors.push_back(std::move(be));
            } else {
                ok = false;
            }
        }
    }
    auto window = entry.find("window");
    if (window != entry.end()) {
        WindowSpec spec;
        if (ParseWindow(*window, spec, errors, path.string())) {
            out.window = std::move(spec);
        } else {
            ok = false;
        }
    }
    auto auto_trigger = entry.find("auto_trigger");
    if (auto_trigger != entry.end()) {
        AutoTriggerDef spec;
        if (ParseAutoTrigger(*auto_trigger, spec, errors, path.string())) {
            out.auto_trigger = std::move(spec);
        } else {
            ok = false;
        }
    }
    return ok;
}

bool ScanCardsDirectory(const fs::path& dir,
                        const std::string& ns,
                        std::vector<CardDef>& out,
                        std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "cards", errors)) {
        CardDef card;
        if (!ParseCardEntry(file, ns, card, errors)) {
            ok = false;
            continue;
        }
        if (!seen.insert(card.id).second) {
            AddError(errors, "card.id.duplicate", "cards", file.string(),
                     "duplicate card id '" + card.id + "'");
            ok = false;
            continue;
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

bool ScanRulesDirectory(const fs::path& dir,
                        const std::string& ns,
                        std::vector<RuleDef>& out,
                        std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "rules", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "rules")) {
            ok = false;
            continue;
        }
        RuleDef rule;
        std::size_t before = errors.size();
        if (!ParseRuleEntry(entry, ns, rule, errors, file.string())) {
            if (errors.size() == before) {
                AddError(errors, "rules.id", "rules", file.string(),
                         "rule entry needs an id matching [a-z0-9_]+ (1-32)");
            }
            ok = false;
            continue;
        }
        if (!seen.insert(rule.id).second) {
            AddError(errors, "rule.id.duplicate", "rules", file.string(),
                     "duplicate rule id '" + rule.id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(rule));
    }
    return ok;
}

bool ScanStatusesDirectory(const fs::path& dir,
                           const std::string& ns,
                           std::vector<StatusDef>& out,
                           std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "statuses", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "statuses")) {
            ok = false;
            continue;
        }
        StatusDef status;
        if (!ParseStatusEntry(entry, ns, status)) {
            AddError(errors, "status.id", "statuses", file.string(),
                     "status entry needs an id matching [a-z0-9_]+ (1-32)");
            ok = false;
            continue;
        }
        if (!seen.insert(status.id).second) {
            AddError(errors, "status.id.duplicate", "statuses", file.string(),
                     "duplicate status id '" + status.id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(status));
    }
    return ok;
}

bool ParseMutationEntry(const nlohmann::json& entry,
                        const std::string& ns,
                        MutationDef& out,
                        std::vector<LoadError>& errors,
                        const std::string& path) {
    if (!entry.is_object()) {
        AddError(errors, "mutation.shape", "mutations", path,
                 "mutation entry must be an object");
        return false;
    }
    if (!ParseStringField(entry, "id", out.id) || !IsValidLocalId(out.id)) {
        AddError(errors, "mutation.id", "mutations", path,
                 "every mutation needs an id matching [a-z0-9_]+ (1-32)");
        return false;
    }
    out.namespace_id = ns;
    out.mutation_id = ns + ":" + out.id;
    out.raw = entry;
    (void)ParseStringField(entry, "target", out.target);
    if (!ParseStringField(entry, "mode", out.mode)) {
        AddError(errors, "mutation.mode", "mutations", path,
                 "mutation '" + out.id + "' requires a string 'mode'");
        return false;
    }
    (void)ParseOptionalStringField(entry, "position", out.position);
    auto where = entry.find("where");
    if (where != entry.end()) out.where = *where;
    auto replacement = entry.find("replacement");
    if (replacement != entry.end()) {
        out.replacement = ParseGraph(*replacement);
    }
    return true;
}

bool ScanMutationsDirectory(const fs::path& dir,
                            const std::string& ns,
                            std::vector<MutationDef>& out,
                            std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "mutations", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "mutations")) {
            ok = false;
            continue;
        }
        MutationDef mut;
        if (!ParseMutationEntry(entry, ns, mut, errors, file.string())) {
            ok = false;
            continue;
        }
        if (!seen.insert(mut.id).second) {
            AddError(errors, "mutation.id.duplicate", "mutations",
                     file.string(), "duplicate mutation id '" + mut.id + "'");
            ok = false;
            continue;
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
    std::vector<fs::path> files = ListJsonFiles(dir, "deck", errors);
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

bool ParseAssetVariant(const nlohmann::json& value,
                       AssetVariant& out,
                       std::vector<LoadError>& errors,
                       const std::string& path,
                       const std::string& slot) {
    if (!value.is_object()) {
        AddError(errors, "asset.variant", "assets", path,
                 "slot '" + slot + "' variants must be objects");
        return false;
    }
    std::string tier_token;
    if (!ParseStringField(value, "tier", tier_token)) {
        AddError(errors, "asset.variant", "assets", path,
                 "slot '" + slot + "' variant requires a string 'tier'");
        return false;
    }
    auto tier = AssetTierFromString(tier_token);
    if (!tier) {
        AddError(errors, "asset.variant", "assets", path,
                 "slot '" + slot + "' has unknown tier '" + tier_token + "'");
        return false;
    }
    out.tier = *tier;
    (void)ParseOptionalStringField(value, "file", out.file);
    (void)ParseOptionalStringField(value, "value", out.value);
    out.raw = value;
    return true;
}

/* INFO: a slot is either an array of variants or a bare string (sugar for a
 *       single `high` file variant. */
bool ParseAssetSlot(const std::string& name,
                    const nlohmann::json& value,
                    AssetSlot& out,
                    std::vector<LoadError>& errors,
                    const std::string& path) {
    out.name = name;
    if (value.is_string()) {
        AssetVariant variant;
        variant.tier = AssetTier::kHigh;
        variant.file = value.get<std::string>();
        variant.raw = value;
        out.variants.push_back(std::move(variant));
        return true;
    }
    if (!value.is_array()) {
        AddError(errors, "asset.slot", "assets", path,
                 "slot '" + name + "' must be an array of variants or a string");
        return false;
    }
    bool ok = true;
    for (const auto& item : value) {
        AssetVariant variant;
        if (!ParseAssetVariant(item, variant, errors, path, name)) {
            ok = false;
            continue;
        }
        out.variants.push_back(std::move(variant));
    }
    return ok;
}

bool ParseAssetBundleFile(const fs::path& index_path,
                          const std::string& folder,
                          const std::string& ns,
                          AssetBundleDef& out,
                          std::vector<LoadError>& errors) {
    nlohmann::json json;
    if (!ReadJsonFile(index_path, json, errors, "assets")) return false;
    if (!json.is_object()) {
        AddError(errors, "asset.shape", "assets", index_path.string(),
                 "bundle index.json must be a JSON object");
        return false;
    }
    out.raw = json;
    if (!ParseStringField(json, "id", out.id) || !IsValidLocalId(out.id)) {
        AddError(errors, "asset.id", "assets", index_path.string(),
                 "bundle index requires an id matching [a-z0-9_]+ (1-32)");
        return false;
    }
    out.namespace_id = ns;
    out.bundle_id = ns + ":" + out.id;
    out.folder = folder;
    (void)ParseOptionalStringField(json, "card", out.card);
    (void)ParseStringField(json, "license", out.license);
    (void)ParseStringField(json, "author", out.author);

    auto slots = json.find("slots");
    if (slots == json.end() || !slots->is_object()) {
        AddError(errors, "asset.slots", "assets", index_path.string(),
                 "bundle index requires a 'slots' object");
        return false;
    }
    bool ok = true;
    for (auto it = slots->begin(); it != slots->end(); ++it) {
        AssetSlot slot;
        if (!ParseAssetSlot(it.key(), it.value(), slot, errors,
                            index_path.string())) {
            ok = false;
            continue;
        }
        out.slots.push_back(std::move(slot));
    }
    return ok;
}

bool ScanAssetsDirectory(const fs::path& dir,
                         const std::string& ns,
                         std::vector<AssetBundleDef>& out,
                         std::vector<LoadError>& errors) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return true;
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory()) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());
    bool ok = true;
    std::set<std::string> seen;
    for (const auto& folder : folders) {
        fs::path index = folder / "index.json";
        if (!fs::is_regular_file(index, ec)) continue;
        AssetBundleDef bundle;
        if (!ParseAssetBundleFile(index, folder.string(), ns, bundle, errors)) {
            ok = false;
            continue;
        }
        if (!seen.insert(bundle.id).second) {
            AddError(errors, "asset.id.duplicate", "assets", index.string(),
                     "duplicate bundle id '" + bundle.id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(bundle));
    }
    return ok;
}

/* INFO: asset budgets and the content-type allowlist. */
constexpr std::uintmax_t kMaxAssetFileBytes = 1ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t kMaxModAssetBytes = 2ULL * 1024ULL * 1024ULL;

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

bool IsAllowedAssetExtension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".png" || ext == ".webp" || ext == ".jpg" || ext == ".gif"
        || ext == ".json";
}

/* INFO: resolve a slot variant's relative path under its bundle folder,
 *       rejecting absolute paths, parent segments and symlink escapes. Reuses
 *       the ResolveProvidesPath canonicalisation approach. */
bool ResolveBundlePath(const fs::path& bundle_dir,
                       const std::string& rel,
                       fs::path& out) {
    if (rel.empty() || rel.front() == '/') return false;
    if (rel.find('\\') != std::string::npos) return false;
    const fs::path rel_path(rel);
    for (const auto& part : rel_path) {
        if (part == "..") return false;
    }
    std::error_code ec;
    const fs::path canonical_bundle = fs::weakly_canonical(bundle_dir, ec);
    if (ec) return false;
    const fs::path resolved = fs::weakly_canonical(bundle_dir / rel_path, ec);
    if (ec) return false;
    if (!IsPathWithin(canonical_bundle, resolved)) return false;
    out = resolved;
    return true;
}

/* INFO: whole-mod content validation. Errors block the mod; warnings
 *       are recorded and the mod still loads. */
bool ValidateModContent(const LoadedMod& mod,
                        std::vector<LoadWarning>& warnings,
                        std::vector<LoadError>& errors,
                        std::size_t& asset_count,
                        std::uintmax_t& asset_bytes) {
    bool ok = true;
    const fs::path root(mod.path);

    std::set<std::string> card_ids;
    for (const CardDef& card : mod.cards) card_ids.insert(card.id);
    std::set<std::string> bundle_ids;
    for (const AssetBundleDef& bundle : mod.assets) bundle_ids.insert(bundle.id);

    std::set<std::string> referenced_bundles;

    // Card -> bundle references.
    for (const CardDef& card : mod.cards) {
        if (card.face.kind != FaceKind::kImage) continue;
        const std::string card_path =
            (root / "cards" / (card.id + ".json")).string();
        if (!card.face.art.has_value()) {
            AddError(errors, "asset.card_ref", "cards", card_path,
                     "image face requires an 'art' bundle id");
            ok = false;
            continue;
        }
        if (bundle_ids.count(*card.face.art) == 0) {
            AddError(errors, "asset.card_ref", "cards", card_path,
                     "card references unknown asset bundle '"
                         + *card.face.art + "'");
            ok = false;
            continue;
        }
        referenced_bundles.insert(*card.face.art);
    }

    // Bundle -> card bindings.
    for (const AssetBundleDef& bundle : mod.assets) {
        if (!bundle.card.has_value()) continue;
        referenced_bundles.insert(bundle.id);
        if (card_ids.count(*bundle.card) == 0) {
            AddError(errors, "asset.card_binding", "assets",
                     bundle.folder + "/index.json",
                     "bundle '" + bundle.id + "' binds unknown card '"
                         + *bundle.card + "'");
            ok = false;
        }
    }

    // Per-bundle slots and files.
    for (const AssetBundleDef& bundle : mod.assets) {
        const fs::path bundle_dir(bundle.folder);
        const std::string index_path = bundle.folder + "/index.json";
        std::set<std::string> slot_tiers;
        std::set<std::string> referenced_files;
        for (const AssetSlot& slot : bundle.slots) {
            for (const AssetVariant& variant : slot.variants) {
                const std::string tier = ToString(variant.tier);
                if (!slot_tiers.insert(slot.name + "/" + tier).second) {
                    AddError(errors, "asset.duplicate_tier", "assets",
                             index_path,
                             "bundle '" + bundle.id + "' slot '" + slot.name
                                 + "' declares tier '" + tier + "' twice");
                    ok = false;
                }
                const bool has_file = variant.file.has_value();
                const bool has_value = variant.value.has_value();
                if (has_file == has_value) {
                    AddError(errors, "asset.variant", "assets", index_path,
                             "slot '" + slot.name
                                 + "' variant must declare exactly one of "
                                   "'file' or 'value'");
                    ok = false;
                    continue;
                }
                if (!has_file) continue;
                ++asset_count;
                fs::path resolved;
                if (!ResolveBundlePath(bundle_dir, *variant.file, resolved)) {
                    AddError(errors, "asset.path", "assets", index_path,
                             "slot '" + slot.name + "' file '" + *variant.file
                                 + "' escapes the bundle folder or is invalid");
                    ok = false;
                    continue;
                }
                if (!IsAllowedAssetExtension(resolved)) {
                    AddError(errors, "asset.extension", "assets",
                             resolved.string(),
                             "asset file '" + *variant.file
                                 + "' has a disallowed extension");
                    ok = false;
                    continue;
                }
                std::error_code ec;
                if (!fs::is_regular_file(resolved, ec)) {
                    AddError(errors, "asset.missing", "assets",
                             resolved.string(),
                             "asset file '" + *variant.file + "' is missing");
                    ok = false;
                    continue;
                }
                const std::uintmax_t size = fs::file_size(resolved, ec);
                if (ec) {
                    AddError(errors, "asset.missing", "assets",
                             resolved.string(),
                             "asset file '" + *variant.file
                                 + "' could not be sized");
                    ok = false;
                    continue;
                }
                if (size > kMaxAssetFileBytes) {
                    AddError(errors, "asset.too_large", "assets",
                             resolved.string(),
                             "asset file '" + *variant.file + "' is "
                                 + std::to_string(size)
                                 + " bytes, exceeding the 1 MiB limit");
                    ok = false;
                }
                asset_bytes += size;
                referenced_files.insert(resolved.string());
            }
        }

        if (referenced_bundles.count(bundle.id) == 0) {
            AddWarning(warnings, "asset.unreferenced", "assets", index_path,
                       "bundle '" + bundle.id
                           + "' is not referenced by any card or binding");
        }

        std::error_code ec;
        if (fs::is_directory(bundle_dir, ec)) {
            for (const auto& entry :
                 fs::recursive_directory_iterator(bundle_dir, ec)) {
                if (!entry.is_regular_file()) continue;
                const fs::path path = entry.path();
                if (path.filename() == "index.json"
                    && path.parent_path() == bundle_dir) {
                    continue;
                }
                if (referenced_files.count(path.string()) == 0) {
                    AddWarning(warnings, "asset.orphan_file", "assets",
                               path.string(),
                               "file is not referenced by any slot variant");
                }
            }
        }
    }

    if (asset_bytes > kMaxModAssetBytes) {
        AddError(errors, "asset.budget", "assets", root.string(),
                 "mod asset total " + std::to_string(asset_bytes)
                     + " bytes exceeds the 2 MiB budget");
        ok = false;
    }

    // Image cards whose bundle has no reachable variant at any tier.
    for (const CardDef& card : mod.cards) {
        if (card.face.kind != FaceKind::kImage || !card.face.art.has_value()) {
            continue;
        }
        const AssetBundleDef* bundle = nullptr;
        for (const AssetBundleDef& candidate : mod.assets) {
            if (candidate.id == *card.face.art) {
                bundle = &candidate;
                break;
            }
        }
        if (bundle == nullptr) continue;  // already an error above
        bool has_variant = false;
        for (const AssetSlot& slot : bundle->slots) {
            if ((slot.name == "art" || slot.name == "emoji")
                && !slot.variants.empty()) {
                has_variant = true;
            }
        }
        if (!has_variant) {
            AddWarning(warnings, "asset.unreachable", "cards",
                       (root / "cards" / (card.id + ".json")).string(),
                       "image card '" + card.id
                           + "' has no art/emoji variant at any tier; it "
                             "falls back to a generated face");
        }
    }

    // Unknown top-level entries are ignored with a warning.
    static const std::set<std::string> kKnownEntries = {
        "mod.json", "cards", "rules", "statuses", "mutations", "decks",
        "assets"};
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const std::string name = entry.path().filename().string();
        if (kKnownEntries.count(name) == 0) {
            AddWarning(warnings, "mod.unknown_entry", "mod",
                       entry.path().string(),
                       "unknown top-level entry '" + name + "' is ignored");
        }
    }

    return ok;
}

}  // namespace

std::string ToString(FaceKind kind) {
    switch (kind) {
        case FaceKind::kText: return "text";
        case FaceKind::kImage: return "image";
        case FaceKind::kEmoji: return "emoji";
        case FaceKind::kBlank: return "blank";
    }
    return "blank";
}

std::optional<FaceKind> FaceKindFromString(const std::string& token) {
    if (token == "text") return FaceKind::kText;
    if (token == "image") return FaceKind::kImage;
    if (token == "emoji") return FaceKind::kEmoji;
    if (token == "blank") return FaceKind::kBlank;
    return std::nullopt;
}

std::string ToString(AssetTier tier) {
    switch (tier) {
        case AssetTier::kHigh: return "high";
        case AssetTier::kMedium: return "medium";
        case AssetTier::kLow: return "low";
    }
    return "low";
}

std::optional<AssetTier> AssetTierFromString(const std::string& token) {
    if (token == "high") return AssetTier::kHigh;
    if (token == "medium") return AssetTier::kMedium;
    if (token == "low") return AssetTier::kLow;
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

    /* INFO: Entry sets are directories; a missing directory is an empty
     *       set, so a mod declares only the content it ships. */
    bool ok = true;
    if (!ScanCardsDirectory(root / "cards", loaded.manifest.id, loaded.cards,
                            errors)) {
        ok = false;
    }
    if (!ScanRulesDirectory(root / "rules", loaded.manifest.id, loaded.rules,
                            errors)) {
        ok = false;
    }
    if (!ScanStatusesDirectory(root / "statuses", loaded.manifest.id,
                               loaded.statuses, errors)) {
        ok = false;
    }
    if (!ScanMutationsDirectory(root / "mutations", loaded.manifest.id,
                                loaded.mutations, errors)) {
        ok = false;
    }
    if (!ScanDeckDirectory(root / "decks", loaded.manifest.id, loaded.decks,
                           errors)) {
        ok = false;
    }
    if (!ScanAssetsDirectory(root / "assets", loaded.manifest.id, loaded.assets,
                             errors)) {
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
        result.fatal_scan_error = true;
        return result;
    }

    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory()) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());

    /* INFO: per-mod isolation: every folder gets a ModReport; the
     *       mods that validate land in `mods`. A bad mod no longer empties the
     *       whole scan, so the server keeps running and authors can read why it
     *       failed. Fail-closed moves to the match boundary. */
    std::unordered_set<std::string> seen_ids;
    for (const auto& folder : folders) {
        ModReport report;
        report.folder = folder.string();
        report.mod_id = folder.filename().string();

        LoadedMod mod;
        std::vector<LoadError> errors;
        std::vector<LoadWarning> warnings;
        bool ok = ParseModFolder(folder.string(), folder.filename().string(),
                                 mod, errors);
        if (ok) {
            report.mod_id = mod.manifest.id;
            if (!seen_ids.insert(mod.manifest.id).second) {
                AddError(errors, "mod.id.duplicate", "mod",
                         (folder / "mod.json").string(),
                         "duplicate mod id '" + mod.manifest.id + "'");
                ok = false;
            } else {
                ok = ValidateModContent(mod, warnings, errors,
                                        report.asset_count,
                                        report.asset_bytes);
            }
        }

        report.ok = ok;
        report.errors = std::move(errors);
        report.warnings = std::move(warnings);
        for (const LoadError& error : report.errors) {
            result.errors.push_back(error);
        }
        for (const LoadWarning& warning : report.warnings) {
            result.warnings.push_back(warning);
        }
        if (ok) result.mods.push_back(std::move(mod));
        result.reports.push_back(std::move(report));
    }

    /* INFO: deterministic ordering: sort by manifest id so match assembly
     *       sees a stable mod list regardless of filesystem enumeration. */
    std::sort(result.mods.begin(), result.mods.end(),
              [](const LoadedMod& a, const LoadedMod& b) {
                  return a.manifest.id < b.manifest.id;
              });
    return result;
}

LoadResult LoadModsFromEnv() {
    std::string root = Env::Get("UNI_MODS_DIR", kModsDefaultDir);
    Logger::Info("[ModLoad] Scanning mods root '" + root + "'");
    LoadResult result = ScanModsDirectory(root);
    for (const ModReport& report : result.reports) {
        if (report.ok) {
            Logger::Info("[ModLoad] " + report.mod_id + " ok ("
                         + std::to_string(report.asset_count) + " asset(s), "
                         + std::to_string(report.asset_bytes) + " bytes)");
        } else {
            Logger::Error("[ModLoad] " + report.mod_id + " failed ("
                          + std::to_string(report.errors.size())
                          + " error(s))");
        }
        for (const LoadError& error : report.errors) {
            Logger::Error("[ModLoad]   " + report.mod_id + " " + error.check
                          + " (" + error.artifact + ") " + error.path + ": "
                          + error.message);
        }
        for (const LoadWarning& warning : report.warnings) {
            Logger::Warn("[ModLoad]   " + report.mod_id + " " + warning.check
                         + " (" + warning.artifact + ") " + warning.path + ": "
                         + warning.message);
        }
    }
    if (result.fatal()) {
        Logger::Error("[ModLoad] mods root is not readable");
    } else {
        Logger::Info("[ModLoad] Loaded " + std::to_string(result.mods.size())
                     + " mod(s), " + std::to_string(result.reports.size())
                     + " report(s)");
    }
    return result;
}

}  // namespace match::modload
