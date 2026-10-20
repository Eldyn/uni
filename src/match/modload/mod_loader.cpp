#include "match/modload/mod_loader.hpp"

#include "common/env.hpp"
#include "common/environment.hpp"
#include "logger.hpp"
#include "match/modload/base_mod.hpp"
#include "match/modload/binding_definition_keys.hpp"
#include "match/modload/derived_definition_keys.hpp"
#include "match/modload/event_definition_keys.hpp"
#include "match/modload/glossary_entries.hpp"
#include "match/modload/streak_definition_keys.hpp"
#include "match/modload/mod_hash.hpp"
#include "match/modload/texts_limits.hpp"
#include "common/crypto_hash.hpp"

#include "match/cardschema/catalog.hpp"
#include "match/cardschema/loader.hpp"
#include "match/cardschema/model.hpp"
#include "match/cardschema/validate.hpp"
#include "match/expr/diagnostic.hpp"

#include <common/contract.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string_view>
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

/* INFO: dev-only mods (manifest "dev_only": true) carry test content that must
 *       never ship. A production build defines UNI_PROD_BUILD, and
 *       UNI_ENV=production also excludes them, so a stray test mod cannot
 *       reach a release even when only one of the two is set. */
bool DevContentAllowed() { return !IsProductionEnvironment(); }

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

/* INFO: one mod folder carried between the two scan passes: the parsed model,
 *       its report, and the diagnostics collected so far. */
struct PendingMod {
    LoadedMod mod;
    ModReport report;
    std::vector<LoadError> errors;
    std::vector<LoadWarning> warnings;
    bool ok = true;
};

/** INFO: converts one cardschema diagnostic into a loader error, keeping the
 *        owner file label and the exact formatted text. */
void AddSchemaError(std::vector<LoadError>& errors,
                    const match::cardschema::Diagnostic& diagnostic) {
    AddError(errors, "card_schema", "cards", diagnostic.file,
             match::expr::FormatDiagnostic(diagnostic));
}

/** INFO: one final marker when a schema report hit the cap, so a caller can
 *        tell a truncated report from a complete one. */
void AddSchemaTruncationMarker(std::vector<LoadError>& errors) {
    AddError(errors, "card_schema", "cards", "",
             "too many schema errors; more not shown");
}

/** INFO: the number of schema errors already in `errors`. The cap counts these
 *        only, so ordinary loader errors do not hide a schema report. */
std::size_t CountSchemaErrors(const std::vector<LoadError>& errors) {
    std::size_t count = 0;
    for (const LoadError& error : errors) {
        if (error.check == "card_schema") ++count;
    }
    return count;
}

/** INFO: appends one cardschema diagnostic, capped at
 *        kMaximumSchemaDiagnostics plus one truncation marker. A report the
 *        catalog already truncated ends with `kSchemaTruncationMarker`; that
 *        entry is replaced by the loader's own marker so exactly one line says
 *        the report was cut. */
void AppendSchemaDiagnostic(std::vector<LoadError>& errors,
                            const match::cardschema::Diagnostic& diagnostic) {
    const bool truncated = diagnostic.message
                           == match::cardschema::kSchemaTruncationMarker;
    if (!truncated
        && CountSchemaErrors(errors)
               < match::cardschema::kMaximumSchemaDiagnostics) {
        AddSchemaError(errors, diagnostic);
        return;
    }
    for (const LoadError& error : errors) {
        if (error.message == "too many schema errors; more not shown") return;
    }
    AddSchemaTruncationMarker(errors);
}

/** INFO: appends a whole cardschema report through AppendSchemaDiagnostic. */
void AppendSchemaErrors(std::vector<LoadError>& errors,
                        const std::vector<match::cardschema::Diagnostic>& diagnostics) {
    for (const match::cardschema::Diagnostic& diagnostic : diagnostics) {
        AppendSchemaDiagnostic(errors, diagnostic);
    }
}

/** INFO: the mod id a cardschema diagnostic names. Every label starts with
 *        `mods/<id>/`, so the folder after `mods/` owns the diagnostic. An
 *        empty result means the label does not name a mod folder. */
std::string DiagnosticModId(const std::string& file) {
    constexpr std::string_view kModsPrefix = "mods/";
    if (file.compare(0, kModsPrefix.size(), kModsPrefix) != 0) return {};
    const std::size_t slash = file.find('/', kModsPrefix.size());
    if (slash == std::string::npos) return {};
    return file.substr(kModsPrefix.size(), slash - kModsPrefix.size());
}

/** INFO: the default integer settings of a manifest, the same rule the
 *        `uni-mods verify` tool uses: every `int` setting contributes `id` to
 *        `default`. Reads the raw manifest so it stays independent of the
 *        loader's own manifest model. */
std::map<std::string, std::int64_t> ManifestDefaultSettings(
    const nlohmann::json& manifest) {
    std::map<std::string, std::int64_t> settings;
    if (!manifest.is_object()) return settings;
    const auto entries = manifest.find("settings");
    if (entries == manifest.end() || !entries->is_array()) return settings;
    for (const nlohmann::json& entry : *entries) {
        if (!entry.is_object()) continue;
        const auto id = entry.find("id");
        const auto type = entry.find("type");
        const auto value = entry.find("default");
        if (id == entry.end() || !id->is_string()) continue;
        if (type == entry.end() || !type->is_string()
            || type->get<std::string>() != "int") {
            continue;
        }
        if (value == entry.end() || !value->is_number_integer()) continue;
        settings[id->get<std::string>()] = value->get<std::int64_t>();
    }
    return settings;
}

/** INFO: the kind file label of a generated card's template, so a parse error
 *        on the converted definition names the kind the card came from. */
std::string KindFileLabel(const match::cardschema::ModSchema& schema,
                          const std::string& template_id) {
    for (const match::cardschema::KindTemplate& kind : schema.kinds) {
        if (kind.identifier == template_id) return kind.file;
    }
    return template_id;
}

/** INFO: the legacy-shaped `DeckDef` the lobby and the assembler already read
 *        (kind id -> count snapshot plus the five snapshot-file keys).
 *
 *        `settings` is the same merged manifest-default map the deck expanded
 *        with, so the lobby snapshot, the `match_start` hook payload, the wire
 *        `match_start.settings` and the validator's `CheckSettings` all keep
 *        the values they had with the old deck files. */
DeckDef BuildDeckDef(const match::cardschema::ExpandedDeck& expanded,
                     const std::vector<std::string>& active_mod_ids,
                     const std::map<std::string, std::int64_t>& settings) {
    DeckDef deck;
    deck.id = match::cardschema::LocalNameOf(expanded.deck_id);
    deck.namespace_id = match::cardschema::NamespaceOf(expanded.deck_id);
    deck.deck_id = expanded.deck_id;
    deck.name = expanded.name;
    deck.mods = active_mod_ids;
    deck.cards = expanded.cards;
    deck.settings = nlohmann::json::object();
    for (const auto& [name, value] : settings) deck.settings[name] = value;
    nlohmann::json cards = nlohmann::json::object();
    for (const auto& [kind_id, count] : deck.cards) cards[kind_id] = count;
    deck.raw = nlohmann::json::object();
    deck.raw["id"] = deck.id;
    deck.raw["name"] = deck.name;
    deck.raw["namespace"] = deck.namespace_id;
    deck.raw["mods"] = deck.mods;
    deck.raw["cards"] = std::move(cards);
    return deck;
}

/** INFO: the expansion shared by the public entry point and the load-time pass.
 *        The load-time pass already holds the expanded catalog and keeps the
 *        diagnostics for `AppendSchemaErrors`, so it passes `catalog`; the
 *        public entry point expands the catalog from the schemas. */
match::cardschema::ExpandedDeck ExpandDeckFromSchemas(
    const std::vector<match::cardschema::ModSchema>& schemas,
    const match::cardschema::CatalogResult* catalog,
    const std::string& deck_id,
    const std::vector<std::string>& active_mod_ids,
    const std::map<std::string, std::int64_t>& settings) {
    const std::set<std::string> active_set(active_mod_ids.begin(),
                                           active_mod_ids.end());
    if (catalog != nullptr) {
        return match::cardschema::ExpandDeck(*catalog, schemas, deck_id,
                                             active_set, settings);
    }
    return match::cardschema::ExpandDeck(schemas, deck_id, active_set, settings);
}

/** INFO: the settings a deck expands with: the manifest defaults of every mod
 *        in `active_mods`, merged in the order the deck lists them so a later
 *        mod wins. Two mods that declare one name with different defaults are
 *        a diagnostic naming both, because silently picking one would make the
 *        dealt deck depend on mod order.
 *
 *        Returns false when a conflict was reported; the caller then drops the
 *        deck's mod. */
bool MergeDeckSettings(const std::vector<PendingMod>& pending,
                       const std::vector<std::string>& active_mods,
                       const std::string& owner_mod,
                       const std::string& deck_file,
                       std::map<std::string, std::int64_t>& settings,
                       std::vector<LoadError>& errors) {
    std::map<std::string, std::string> declared_by;
    bool ok = true;
    for (const std::string& mod_id : active_mods) {
        const PendingMod* entry = nullptr;
        for (const PendingMod& candidate : pending) {
            if (candidate.mod.manifest.id == mod_id) {
                entry = &candidate;
                break;
            }
        }
        if (entry == nullptr) continue;
        for (const auto& [name, value] : ManifestDefaultSettings(entry->mod.manifest.raw)) {
            const auto found = settings.find(name);
            if (found == settings.end()) {
                settings[name] = value;
                declared_by[name] = mod_id;
                continue;
            }
            if (found->second == value) continue;
            const std::string first = declared_by.count(name) != 0
                                          ? declared_by[name]
                                          : owner_mod;
            AddError(errors, "card_schema", "cards", deck_file,
                     deck_file + "#/deck: setting \"" + name + "\" is declared by "
                         + "mods \"" + first + "\" and \"" + mod_id
                         + "\" with different defaults");
            ok = false;
        }
    }
    return ok;
}

/** INFO: the pending entry whose manifest id is `mod_id`, or nullptr. */
PendingMod* PendingById(std::vector<PendingMod>& pending,
                        const std::string& mod_id) {
    if (mod_id.empty()) return nullptr;
    for (PendingMod& entry : pending) {
        if (entry.mod.manifest.id == mod_id) return &entry;
    }
    return nullptr;
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
    out.dev_only = json.value("dev_only", false);
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

bool ParseCardObject(const nlohmann::json& entry,
                     const std::string& source_label,
                     const std::string& ns,
                     CardDef& out,
                     std::vector<LoadError>& errors) {
    if (!entry.is_object()) {
        AddError(errors, "card.shape", "cards", source_label,
                 "card file must be a JSON object");
        return false;
    }
    if (!ParseStringField(entry, "id", out.id) || !IsValidLocalId(out.id)) {
        AddError(errors, "card.id", "cards", source_label,
                 "card needs an id matching [a-z0-9_]+ (1-32)");
        return false;
    }
    out.namespace_id = ns;
    out.kind_id = ns + ":" + out.id;
    out.raw = entry;
    (void)ParseStringField(entry, "title", out.title);

    bool ok = true;
    for (const auto& [field, target] :
         {std::pair<std::string_view, std::string*>{
              glossary_keys::kCardLabelKey, &out.label_key},
          std::pair<std::string_view, std::string*>{
              glossary_keys::kCardDescriptionKey, &out.description_key}}) {
        const auto found = entry.find(std::string(field));
        if (found == entry.end()) continue;
        if (!found->is_string()) {
            AddError(errors, "card.keys", "cards", source_label,
                     "card '" + out.id + "' field '" + std::string(field)
                         + "' must be a text key string");
            ok = false;
            continue;
        }
        *target = found->get<std::string>();
    }
    auto face = entry.find("face");
    if (face != entry.end()) {
        if (!ParseFace(*face, out.face, errors, source_label)) ok = false;
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
                                   source_label, it.key())) {
                out.behaviors.push_back(std::move(be));
            } else {
                ok = false;
            }
        }
    }
    auto window = entry.find("window");
    if (window != entry.end()) {
        WindowSpec spec;
        if (ParseWindow(*window, spec, errors, source_label)) {
            out.window = std::move(spec);
        } else {
            ok = false;
        }
    }
    auto auto_trigger = entry.find("auto_trigger");
    if (auto_trigger != entry.end()) {
        AutoTriggerDef spec;
        if (ParseAutoTrigger(*auto_trigger, spec, errors, source_label)) {
            out.auto_trigger = std::move(spec);
        } else {
            ok = false;
        }
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

/* INFO: one outcome table per file.
 *       Structural checks only; schema + semantic checks are the semantic validator's job. */
bool ParseTableEntry(const nlohmann::json& entry,
                     const std::string& ns,
                     TableDef& out,
                     std::vector<LoadError>& errors,
                     const std::string& path) {
    if (!entry.is_object()) {
        AddError(errors, "table.shape", "tables", path,
                 "table file must be a JSON object");
        return false;
    }
    if (!ParseStringField(entry, "id", out.id) || !IsValidLocalId(out.id)) {
        return false;
    }
    const auto entries = entry.find("entries");
    if (entries == entry.end() || !entries->is_array()) {
        AddError(errors, "table.entries", "tables", path,
                 "table '" + out.id + "' requires an 'entries' array");
        return false;
    }
    out.namespace_id = ns;
    out.table_id = ns + ":" + out.id;
    out.raw = entry;

    bool ok = true;
    for (const auto& raw_entry : *entries) {
        if (!raw_entry.is_object()) {
            AddError(errors, "table.entry", "tables", path,
                     "table '" + out.id + "' has a non-object entry");
            ok = false;
            continue;
        }
        TableEntry parsed;
        if (!ParseStringField(raw_entry, "key", parsed.key)
            || !IsValidLocalId(parsed.key)) {
            AddError(errors, "table.entry", "tables", path,
                     "table '" + out.id + "' entry needs a key matching "
                         "[a-z0-9_]+ (1-32)");
            ok = false;
            continue;
        }
        const auto weight = raw_entry.find("weight");
        if (weight == raw_entry.end() || !weight->is_number_integer()) {
            AddError(errors, "table.entry", "tables", path,
                     "table '" + out.id + "' entry '" + parsed.key
                         + "' needs an integer 'weight'");
            ok = false;
            continue;
        }
        parsed.weight = weight->get<int64_t>();
        const auto value = raw_entry.find("value");
        if (value != raw_entry.end() && value->is_number_integer()) {
            parsed.value = value->get<int64_t>();
        }
        const auto roll = raw_entry.find("roll");
        if (roll != raw_entry.end()) parsed.roll = *roll;
        const auto where = raw_entry.find("where");
        if (where != raw_entry.end()) parsed.where = *where;
        parsed.raw = raw_entry;
        out.entries.push_back(std::move(parsed));
    }

    std::sort(out.entries.begin(), out.entries.end(),
              [](const TableEntry& a, const TableEntry& b) {
                  return a.key < b.key;
              });
    return ok;
}

bool ScanTablesDirectory(const fs::path& dir,
                         const std::string& ns,
                         std::vector<TableDef>& out,
                         std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "tables", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "tables")) {
            ok = false;
            continue;
        }
        TableDef table;
        std::size_t before = errors.size();
        if (!ParseTableEntry(entry, ns, table, errors, file.string())) {
            if (errors.size() == before) {
                AddError(errors, "table.id", "tables", file.string(),
                         "table entry needs an id matching [a-z0-9_]+ (1-32)");
            }
            ok = false;
            continue;
        }
        if (!seen.insert(table.id).second) {
            AddError(errors, "table.id.duplicate", "tables", file.string(),
                     "duplicate table id '" + table.id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(table));
    }
    return ok;
}

/* INFO: one entity per file. Structural checks only; the
 *       component vocabulary and exclusive-family checks are the semantic validator's job. */
bool ParseEntityEntry(const nlohmann::json& entry,
                      const std::string& ns,
                      EntityDef& out,
                      std::vector<LoadError>& errors,
                      const std::string& path) {
    if (!entry.is_object()) {
        AddError(errors, "entity.shape", "entities", path,
                 "entity file must be a JSON object");
        return false;
    }
    if (!ParseStringField(entry, "id", out.id) || !IsValidLocalId(out.id)) {
        return false;
    }
    const auto components = entry.find("components");
    if (components == entry.end() || !components->is_object()) {
        AddError(errors, "entity.components", "entities", path,
                 "entity '" + out.id + "' requires a 'components' object");
        return false;
    }
    out.namespace_id = ns;
    out.entity_id = ns + ":" + out.id;
    out.raw = entry;
    /* INFO: nlohmann objects iterate in key order, so components are sorted by
     *       name (deterministic) without an explicit sort. */
    for (auto it = components->begin(); it != components->end(); ++it) {
        ComponentDef component;
        component.name = it.key();
        component.params = it.value();
        out.components.push_back(std::move(component));
    }
    return true;
}

bool ScanEntitiesDirectory(const fs::path& dir,
                           const std::string& ns,
                           std::vector<EntityDef>& out,
                           std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "entities", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "entities")) {
            ok = false;
            continue;
        }
        EntityDef entity;
        std::size_t before = errors.size();
        if (!ParseEntityEntry(entry, ns, entity, errors, file.string())) {
            if (errors.size() == before) {
                AddError(errors, "entity.id", "entities", file.string(),
                         "entity entry needs an id matching [a-z0-9_]+ (1-32)");
            }
            ok = false;
            continue;
        }
        if (!seen.insert(entity.id).second) {
            AddError(errors, "entity.id.duplicate", "entities", file.string(),
                     "duplicate entity id '" + entity.id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(entity));
    }
    return ok;
}

/* INFO: one presentation per file. Structural checks only;
 *       modifier kinds, merge modes, target grammar and trigger sources are
 *       the semantic validator's job. */
bool ParsePresentationEntry(const nlohmann::json& entry,
                            const std::string& ns,
                            PresentationDef& out,
                            std::vector<LoadError>& errors,
                            const std::string& path) {
    if (!entry.is_object()) {
        AddError(errors, "presentation.shape", "presentations", path,
                 "presentation file must be a JSON object");
        return false;
    }
    if (!ParseStringField(entry, "id", out.id) || !IsValidLocalId(out.id)) {
        return false;
    }
    if (!ParseStringField(entry, "target", out.target)) {
        AddError(errors, "presentation.target", "presentations", path,
                 "presentation '" + out.id + "' requires a string 'target'");
        return false;
    }
    const auto modifiers = entry.find("modifiers");
    if (modifiers == entry.end() || !modifiers->is_array()) {
        AddError(errors, "presentation.modifiers", "presentations", path,
                 "presentation '" + out.id + "' requires a 'modifiers' array");
        return false;
    }
    out.namespace_id = ns;
    out.presentation_id = ns + ":" + out.id;
    out.raw = entry;

    const auto priority = entry.find("priority");
    if (priority != entry.end() && priority->is_number_integer()) {
        out.priority = priority->get<int64_t>();
    }

    bool ok = true;
    for (const auto& raw_modifier : *modifiers) {
        ModifierDef modifier;
        if (!raw_modifier.is_object()
            || !ParseStringField(raw_modifier, "kind", modifier.kind)) {
            AddError(errors, "presentation.modifier", "presentations", path,
                     "presentation '" + out.id
                         + "' modifiers must be objects with a string 'kind'");
            ok = false;
            continue;
        }
        modifier.params = raw_modifier;
        out.modifiers.push_back(std::move(modifier));
    }

    const auto triggers = entry.find("triggers");
    if (triggers != entry.end()) {
        if (!triggers->is_array()) {
            AddError(errors, "presentation.trigger", "presentations", path,
                     "presentation '" + out.id + "' triggers must be an array");
            ok = false;
        } else {
            for (const auto& raw_trigger : *triggers) {
                if (!raw_trigger.is_object()) {
                    AddError(errors, "presentation.trigger", "presentations",
                             path,
                             "presentation '" + out.id
                                 + "' has a non-object trigger");
                    ok = false;
                    continue;
                }
                TriggerDef trigger;
                (void)ParseOptionalStringField(raw_trigger, "on", trigger.on);
                const auto where = raw_trigger.find("where");
                if (where != raw_trigger.end()) trigger.where = *where;
                const auto duration = raw_trigger.find("duration");
                if (duration != raw_trigger.end() && duration->is_number()) {
                    trigger.duration = duration->get<double>();
                }
                out.triggers.push_back(std::move(trigger));
            }
        }
    }
    return ok;
}

bool IsBindingFile(const nlohmann::json& entry);

bool ScanPresentationsDirectory(const fs::path& dir,
                                const std::string& ns,
                                std::vector<PresentationDef>& out,
                                std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "presentations", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "presentations")) {
            ok = false;
            continue;
        }
        if (IsBindingFile(entry)) continue;
        PresentationDef presentation;
        std::size_t before = errors.size();
        if (!ParsePresentationEntry(entry, ns, presentation, errors,
                                    file.string())) {
            if (errors.size() == before) {
                AddError(errors, "presentation.id", "presentations",
                         file.string(),
                         "presentation entry needs an id matching [a-z0-9_]+ "
                         "(1-32)");
            }
            ok = false;
            continue;
        }
        if (!seen.insert(presentation.id).second) {
            AddError(errors, "presentation.id.duplicate", "presentations",
                     file.string(),
                     "duplicate presentation id '" + presentation.id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(presentation));
    }
    return ok;
}

/* INFO: `major[.minor[.patch]]`, each part digits only, major supported. */
bool IsSupportedFormatVersion(const std::string& text) {
    std::size_t start = 0;
    int parts = 0;
    int major = -1;
    while (start <= text.size()) {
        const std::size_t dot = text.find('.', start);
        const std::size_t end = dot == std::string::npos ? text.size() : dot;
        if (end == start) return false;
        int value = 0;
        const auto parsed =
            std::from_chars(text.data() + start, text.data() + end, value);
        if (parsed.ec != std::errc() || parsed.ptr != text.data() + end) {
            return false;
        }
        if (parts == 0) major = value;
        ++parts;
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return parts <= 3 && major == event_keys::kSupportedFormatMajor;
}

/* INFO: a mod version enters the client cache key `id@version#hash|...`, so
 *       it may not hold `|` or `#`. */
bool IsValidModVersion(const std::string& version) {
    if (version.empty()) return false;
    return std::all_of(version.begin(), version.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '+' || c == '-';
    });
}

/* INFO: one event definition per file, version 1 style. Structural checks
 *       and the namespace rule only; the closed word lists (type, audience,
 *       retention) are the schema's and the semantic validator's job. */
bool ParseEventEntry(const nlohmann::json& entry,
                     const std::string& mod_id,
                     EventDef& out,
                     std::vector<LoadError>& errors,
                     const std::string& path) {
    namespace keys = event_keys;
    if (!entry.is_object()) {
        AddError(errors, "event.shape", "events", path,
                 "event file must be a JSON object");
        return false;
    }
    const auto version = entry.find(std::string(keys::kFormatVersion));
    if (version == entry.end() || !version->is_string()
        || !IsSupportedFormatVersion(version->get<std::string>())) {
        AddError(errors, "event.format_version", "events", path,
                 "event file needs '" + std::string(keys::kFormatVersion)
                     + "' with major version "
                     + std::to_string(keys::kSupportedFormatMajor));
        return false;
    }
    const auto event = entry.find(std::string(keys::kEvent));
    if (event == entry.end() || !event->is_object()) {
        AddError(errors, "event.shape", "events", path,
                 "event file needs a '" + std::string(keys::kEvent)
                     + "' object");
        return false;
    }
    const auto description = event->find(std::string(keys::kDescription));
    std::string identifier;
    if (description == event->end() || !description->is_object()
        || !ParseStringField(*description, std::string(keys::kIdentifier),
                             identifier)) {
        AddError(errors, "event.identifier", "events", path,
                 "event needs '" + std::string(keys::kDescription) + "."
                     + std::string(keys::kIdentifier) + "'");
        return false;
    }
    const std::string violation = NamespaceRuleViolation(mod_id, identifier);
    if (!violation.empty()) {
        AddError(errors, "event.id.namespace", "events", path, violation);
        return false;
    }
    out.event_id = identifier;
    out.namespace_id = mod_id;
    out.id = identifier.substr(mod_id.size() + 1);
    out.retention = std::string(keys::kDefaultRetention);
    const auto retention = event->find(std::string(keys::kRetention));
    if (retention != event->end()) {
        if (!retention->is_string()) {
            AddError(errors, "event.retention", "events", path,
                     "event '" + identifier + "' retention must be a string");
            return false;
        }
        out.retention = retention->get<std::string>();
    }
    out.raw = entry;

    const auto payload = event->find(std::string(keys::kPayload));
    if (payload != event->end() && payload->is_object()) {
        const auto fields = payload->find(std::string(keys::kFields));
        if (fields != payload->end() && fields->is_object()) {
            /* INFO: nlohmann objects iterate in key order, so the fields
             *       are name-sorted without an explicit sort. */
            if (fields->size() > keys::kMaxEventFields) {
                AddError(errors, "event.fields.count", "events", path,
                         "event '" + identifier + "' declares "
                             + std::to_string(fields->size())
                             + " fields; the maximum is "
                             + std::to_string(keys::kMaxEventFields));
                return false;
            }
            for (auto it = fields->begin(); it != fields->end(); ++it) {
                EventFieldDef field;
                field.name = it.key();
                field.audience = std::string(keys::kDefaultAudience);
                if (!it.value().is_object()) {
                    AddError(errors, "event.field.shape", "events", path,
                             "field '" + field.name + "' must be an object");
                    return false;
                }
                const auto type = it.value().find(std::string(keys::kType));
                if (type != it.value().end()) {
                    if (!type->is_string()) {
                        AddError(errors, "event.field.type", "events", path,
                                 "field '" + field.name
                                     + "' type must be a string");
                        return false;
                    }
                    field.type = type->get<std::string>();
                }
                const auto audience =
                    it.value().find(std::string(keys::kAudience));
                if (audience != it.value().end()) {
                    if (!audience->is_string()) {
                        AddError(errors, "event.field.audience", "events",
                                 path,
                                 "field '" + field.name
                                     + "' audience must be a string");
                        return false;
                    }
                    field.audience = audience->get<std::string>();
                }
                if ((field.name == keys::kTargetRole
                     || field.name == keys::kOwnerRole)
                    && field.type != keys::kSeatType) {
                    AddError(errors, "event.field.role_type", "events", path,
                             "field '" + field.name
                                 + "' names a seat role and must have type '"
                                 + std::string(keys::kSeatType) + "'");
                    return false;
                }
                out.fields.push_back(std::move(field));
            }
        }
    }
    const auto context = event->find(std::string(keys::kContext));
    if (context != event->end() && context->is_array()) {
        for (const nlohmann::json& name : *context) {
            if (name.is_string()) {
                out.context.push_back(name.get<std::string>());
            }
        }
    }
    return true;
}

bool ScanEventsDirectory(const fs::path& dir,
                         const std::string& mod_id,
                         std::vector<EventDef>& out,
                         std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "events", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "events")) {
            ok = false;
            continue;
        }
        EventDef event;
        if (!ParseEventEntry(entry, mod_id, event, errors, file.string())) {
            ok = false;
            continue;
        }
        if (!seen.insert(event.event_id).second) {
            AddError(errors, "event.id.duplicate", "events", file.string(),
                     "duplicate event id '" + event.event_id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(event));
    }
    return ok;
}

/* INFO: the tests of one matcher or of one binding case, version 1 style.
 *       Shape only; the closed word lists and the field checks are the
 *       schema's and the validator's job. */
bool ParseMatcherTests(const nlohmann::json& tests,
                       std::vector<StreakTestDef>& out,
                       std::vector<LoadError>& errors,
                       const std::string& path, const std::string& check,
                       const std::string& artifact) {
    namespace keys = streak_keys;
    if (!tests.is_array() || tests.size() > keys::kMaxMatcherTests) {
        AddError(errors, check, artifact, path,
                 "'" + std::string(keys::kTests) + "' must be an array of at "
                     "most " + std::to_string(keys::kMaxMatcherTests)
                     + " tests");
        return false;
    }
    for (const nlohmann::json& item : tests) {
        StreakTestDef test;
        if (!item.is_object()
            || !ParseStringField(item, std::string(keys::kTest), test.test)) {
            AddError(errors, check, artifact, path,
                     "a test needs an object with '"
                         + std::string(keys::kTest) + "'");
            return false;
        }
        (void)ParseStringField(item, std::string(keys::kField), test.field);
        (void)ParseStringField(item, std::string(keys::kState), test.state);
        (void)ParseStringField(item, std::string(keys::kTag), test.tag);
        (void)ParseStringField(item, std::string(keys::kRelation),
                               test.relation);
        (void)ParseStringField(item, std::string(binding_keys::kRole),
                               test.role);
        (void)ParseStringField(item, std::string(binding_keys::kParamStreak),
                               test.streak);
        (void)ParseStringField(item, std::string(binding_keys::kParamKey),
                               test.key);
        (void)ParseStringField(item, std::string(binding_keys::kParamKeyField),
                               test.key_field);
        const auto value = item.find(std::string(keys::kValue));
        if (value != item.end()) test.value = *value;
        const auto values = item.find(std::string(keys::kValues));
        if (values != item.end() && values->is_array()) test.values = *values;
        const auto negate = item.find(std::string(keys::kNegate));
        test.negate =
            negate != item.end() && negate->is_boolean() && negate->get<bool>();
        const auto actor = item.find(std::string(binding_keys::kParamActor));
        test.actor =
            actor != item.end() && actor->is_boolean() && actor->get<bool>();
        out.push_back(std::move(test));
    }
    return true;
}

/* INFO: one matcher, version 1 style. */
bool ParseStreakMatcher(const nlohmann::json& entry, StreakMatcherDef& out,
                        std::vector<LoadError>& errors,
                        const std::string& path,
                        const std::string& check = "streak.matcher",
                        const std::string& artifact = "streaks") {
    namespace keys = streak_keys;
    if (!entry.is_object()
        || !ParseStringField(entry, std::string(keys::kMatcherEvent),
                             out.event)) {
        AddError(errors, check, artifact, path,
                 "a matcher needs an object with '"
                     + std::string(keys::kMatcherEvent) + "'");
        return false;
    }
    const auto tests = entry.find(std::string(keys::kTests));
    if (tests == entry.end()) return true;
    return ParseMatcherTests(*tests, out.tests, errors, path, check, artifact);
}

/* INFO: a file of `presentations/` is a binding (new kind) when it has the
 *       top level key `uni:binding` or `uni:format_version`. Everything else
 *       is the old presentation kind. */
bool IsBindingFile(const nlohmann::json& entry) {
    return entry.is_object()
           && (entry.contains(std::string(binding_keys::kBinding))
               || entry.contains(std::string(binding_keys::kFormatVersion)));
}

bool ParseBindingSounds(const nlohmann::json& present, BindingCaseDef& out,
                        std::vector<LoadError>& errors,
                        const std::string& path) {
    namespace keys = binding_keys;
    const auto sounds = present.find(std::string(keys::kSounds));
    if (sounds == present.end()) return true;
    if (!sounds->is_array() || sounds->size() > keys::kMaxSounds) {
        AddError(errors, "binding.present", "presentations", path,
                 "'" + std::string(keys::kSounds) + "' must be an array of at "
                     "most " + std::to_string(keys::kMaxSounds) + " sounds");
        return false;
    }
    for (const nlohmann::json& item : *sounds) {
        BindingSoundDef sound;
        if (!item.is_object()
            || !ParseStringField(item, std::string(keys::kSound),
                                 sound.sound)) {
            AddError(errors, "binding.present", "presentations", path,
                     "a sound needs an object with '"
                         + std::string(keys::kSound) + "'");
            return false;
        }
        const auto volume = item.find(std::string(keys::kVolume));
        if (volume != item.end() && volume->is_number()) {
            sound.volume = volume->get<double>();
        }
        const auto pitch = item.find(std::string(keys::kPitch));
        if (pitch != item.end() && pitch->is_number()) {
            sound.pitch = pitch->get<double>();
        }
        out.sounds.push_back(std::move(sound));
    }
    return true;
}

bool ParseBindingParam(const nlohmann::json& item, BindingParamDef& out) {
    namespace keys = binding_keys;
    if (!item.is_object()) return false;
    (void)ParseStringField(item, std::string(keys::kParamField), out.field);
    (void)ParseStringField(item, std::string(keys::kParamStreak), out.streak);
    (void)ParseStringField(item, std::string(keys::kParamKey), out.key);
    (void)ParseStringField(item, std::string(keys::kParamKeyField),
                           out.key_field);
    const auto value = item.find(std::string(keys::kParamValue));
    if (value != item.end() && (value->is_string() || value->is_number())) {
        out.value = *value;
    }
    const auto fallback = item.find(std::string(keys::kParamDefault));
    if (fallback != item.end()
        && (fallback->is_string() || fallback->is_number())) {
        out.default_value = *fallback;
    }
    const auto part = item.find(std::string(keys::kParamCardPart));
    if (part != item.end() && part->is_string()) {
        out.card_part = part->get<std::string>();
    }
    const auto actor = item.find(std::string(keys::kParamActor));
    out.actor = actor != item.end() && actor->is_boolean()
                && actor->get<bool>();
    const auto cause = item.find(std::string(keys::kParamCause));
    out.cause = cause != item.end() && cause->is_boolean()
                && cause->get<bool>();
    return true;
}

bool ParseBindingLog(const nlohmann::json& present, BindingCaseDef& out,
                     std::vector<LoadError>& errors,
                     const std::string& path) {
    namespace keys = binding_keys;
    const auto log = present.find(std::string(keys::kLog));
    if (log == present.end()) return true;
    const auto variants =
        log->is_object() ? log->find(std::string(keys::kVariants))
                         : log->end();
    if (!log->is_object() || variants == log->end() || !variants->is_array()
        || variants->size() > keys::kMaxVariants) {
        AddError(errors, "binding.present", "presentations", path,
                 "'" + std::string(keys::kLog) + "' needs '"
                     + std::string(keys::kVariants) + "' with at most "
                     + std::to_string(keys::kMaxVariants) + " keys");
        return false;
    }
    BindingLogDef line;
    for (const nlohmann::json& key : *variants) {
        if (!key.is_string()) {
            AddError(errors, "binding.present", "presentations", path,
                     "a log variant must be a string key");
            return false;
        }
        line.variants.push_back(key.get<std::string>());
    }
    const auto params = log->find(std::string(keys::kParams));
    if (params != log->end()) {
        if (!params->is_object() || params->size() > keys::kMaxParams) {
            AddError(errors, "binding.present", "presentations", path,
                     "'" + std::string(keys::kParams)
                         + "' must be an object with at most "
                         + std::to_string(keys::kMaxParams) + " entries");
            return false;
        }
        for (auto it = params->begin(); it != params->end(); ++it) {
            BindingParamDef param;
            if (!ParseBindingParam(it.value(), param)) {
                AddError(errors, "binding.present", "presentations", path,
                         "log parameter '" + it.key()
                             + "' must be an object");
                return false;
            }
            line.params[it.key()] = std::move(param);
        }
    }
    out.log = std::move(line);
    return true;
}

bool ParseBindingBeats(const nlohmann::json& present, BindingCaseDef& out,
                       std::vector<LoadError>& errors,
                       const std::string& path) {
    namespace keys = binding_keys;
    const auto beats = present.find(std::string(keys::kBeats));
    if (beats == present.end()) return true;
    if (!beats->is_array() || beats->size() > keys::kMaxBeats) {
        AddError(errors, "binding.present", "presentations", path,
                 "'" + std::string(keys::kBeats) + "' must be an array of at "
                     "most " + std::to_string(keys::kMaxBeats) + " beats");
        return false;
    }
    for (const nlohmann::json& beat : *beats) {
        if (!beat.is_array() || beat.size() > keys::kMaxStepsPerBeat) {
            AddError(errors, "binding.present", "presentations", path,
                     "a beat must be an array of at most "
                         + std::to_string(keys::kMaxStepsPerBeat) + " steps");
            return false;
        }
        out.beats.push_back(beat);
    }
    return true;
}

bool ParseBindingCase(const nlohmann::json& entry, BindingCaseDef& out,
                      std::vector<LoadError>& errors,
                      const std::string& path) {
    namespace keys = binding_keys;
    const auto present =
        entry.is_object() ? entry.find(std::string(keys::kPresent))
                          : entry.end();
    if (!entry.is_object() || present == entry.end() || !present->is_object()) {
        AddError(errors, "binding.present", "presentations", path,
                 "a case needs a '" + std::string(keys::kPresent)
                     + "' object");
        return false;
    }
    const auto tests = entry.find(std::string(keys::kTests));
    if (tests != entry.end()
        && !ParseMatcherTests(*tests, out.tests, errors, path,
                              "binding.cases", "presentations")) {
        return false;
    }
    (void)ParseStringField(*present, std::string(keys::kGlossary),
                           out.glossary);
    return ParseBindingBeats(*present, out, errors, path)
           && ParseBindingSounds(*present, out, errors, path)
           && ParseBindingLog(*present, out, errors, path);
}

/* INFO: one binding per file, version 1 style. Structural checks and the
 *       namespace rule only. */
bool ParseBindingEntry(const nlohmann::json& entry, const std::string& mod_id,
                       BindingDef& out, std::vector<LoadError>& errors,
                       const std::string& path) {
    namespace keys = binding_keys;
    const auto version = entry.find(std::string(keys::kFormatVersion));
    if (version == entry.end() || !version->is_string()
        || !IsSupportedFormatVersion(version->get<std::string>())) {
        AddError(errors, "binding.format_version", "presentations", path,
                 "binding file needs '" + std::string(keys::kFormatVersion)
                     + "' with major version "
                     + std::to_string(event_keys::kSupportedFormatMajor));
        return false;
    }
    const auto binding = entry.find(std::string(keys::kBinding));
    if (binding == entry.end() || !binding->is_object()) {
        AddError(errors, "binding.shape", "presentations", path,
                 "binding file needs a '" + std::string(keys::kBinding)
                     + "' object");
        return false;
    }
    const auto description = binding->find(std::string(keys::kDescription));
    std::string identifier;
    if (description == binding->end() || !description->is_object()
        || !ParseStringField(*description, std::string(keys::kIdentifier),
                             identifier)) {
        AddError(errors, "binding.identifier", "presentations", path,
                 "binding needs '" + std::string(keys::kDescription) + "."
                     + std::string(keys::kIdentifier) + "'");
        return false;
    }
    const std::string violation = NamespaceRuleViolation(mod_id, identifier);
    if (!violation.empty()) {
        AddError(errors, "binding.id.namespace", "presentations", path,
                 violation);
        return false;
    }
    out.binding_id = identifier;
    out.namespace_id = mod_id;
    out.id = identifier.substr(mod_id.size() + 1);
    out.raw = entry;
    std::string select;
    if (ParseStringField(*binding, std::string(keys::kSelect), select)) {
        out.select = select;
    }
    if (!ParseStringField(*binding, std::string(keys::kEvent), out.event)) {
        AddError(errors, "binding.event", "presentations", path,
                 "binding '" + identifier + "' needs a string '"
                     + std::string(keys::kEvent) + "'");
        return false;
    }
    const auto cases = binding->find(std::string(keys::kCases));
    if (cases == binding->end() || !cases->is_array() || cases->empty()
        || cases->size() > keys::kMaxCases) {
        AddError(errors, "binding.cases", "presentations", path,
                 "binding '" + identifier + "' needs '"
                     + std::string(keys::kCases) + "' with 1 to "
                     + std::to_string(keys::kMaxCases) + " cases");
        return false;
    }
    for (const nlohmann::json& item : *cases) {
        BindingCaseDef binding_case;
        if (!ParseBindingCase(item, binding_case, errors, path)) return false;
        out.cases.push_back(std::move(binding_case));
    }
    return true;
}

bool ScanBindingsDirectory(const fs::path& dir, const std::string& mod_id,
                           std::vector<BindingDef>& out,
                           std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "presentations", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "presentations")) continue;
        if (!IsBindingFile(entry)) continue;
        BindingDef binding;
        if (!ParseBindingEntry(entry, mod_id, binding, errors, file.string())) {
            ok = false;
            continue;
        }
        if (!seen.insert(binding.binding_id).second) {
            AddError(errors, "binding.id.duplicate", "presentations",
                     file.string(),
                     "duplicate binding id '" + binding.binding_id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(binding));
    }
    return ok;
}

bool PathIsInside(const fs::path& path, const fs::path& root) {
    const auto [root_end, path_it] =
        std::mismatch(root.begin(), root.end(), path.begin(), path.end());
    return root_end == root.end();
}

/* INFO: reads the `texts/<locale>.json` files. A file that is too large,
 *       unreadable, or not JSON keeps a `read_error`, and the "texts"
 *       validation stage reports it. A file over the size limit is not read
 *       at all. A symlink, and a file whose real path leaves the mod folder,
 *       is a load error: the server must not read a file outside the mod. */
bool ScanTextsDirectory(const fs::path& mod_root, std::vector<TextsFileDef>& out,
                        std::vector<LoadError>& errors) {
    const fs::path dir = mod_root / "texts";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return true;
    const fs::path real_root = fs::weakly_canonical(mod_root, ec);
    const fs::path real_dir = fs::weakly_canonical(dir, ec);
    if (ec || fs::is_symlink(dir) || !PathIsInside(real_dir, real_root)) {
        AddError(errors, "texts.symlink", "texts", dir.string(),
                 "the texts folder must be a real folder inside the mod");
        return false;
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".json") continue;
        if (entry.is_symlink()) {
            AddError(errors, "texts.symlink", "texts", entry.path().string(),
                     "a texts file must not be a symlink");
            return false;
        }
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        TextsFileDef texts;
        texts.locale = file.stem().string();
        texts.path = file.string();
        texts.size = fs::file_size(file, ec);
        if (ec) {
            texts.read_error = "cannot read the file";
        } else if (texts.size > texts_limits::kMaxTextsFileBytes) {
            texts.read_error = "the file is too large to read";
        } else {
            std::ifstream input(file, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
            texts.hash = Sha256Hex(bytes);
            texts.content = nlohmann::json::parse(bytes, nullptr, false);
            if (texts.content.is_discarded()) {
                texts.content = nullptr;
                texts.read_error = "the file is not valid JSON";
            }
        }
        out.push_back(std::move(texts));
    }
    return true;
}

/* INFO: one streak definition per file, version 1 style. Structural checks
 *       and the namespace rule only. */
bool ParseStreakEntry(const nlohmann::json& entry, const std::string& mod_id,
                      StreakDef& out, std::vector<LoadError>& errors,
                      const std::string& path) {
    namespace keys = streak_keys;
    if (!entry.is_object()) {
        AddError(errors, "streak.shape", "streaks", path,
                 "streak file must be a JSON object");
        return false;
    }
    const auto version = entry.find(std::string(keys::kFormatVersion));
    if (version == entry.end() || !version->is_string()
        || !IsSupportedFormatVersion(version->get<std::string>())) {
        AddError(errors, "streak.format_version", "streaks", path,
                 "streak file needs '" + std::string(keys::kFormatVersion)
                     + "' with major version "
                     + std::to_string(event_keys::kSupportedFormatMajor));
        return false;
    }
    const auto streak = entry.find(std::string(keys::kStreak));
    if (streak == entry.end() || !streak->is_object()) {
        AddError(errors, "streak.shape", "streaks", path,
                 "streak file needs a '" + std::string(keys::kStreak)
                     + "' object");
        return false;
    }
    const auto description = streak->find(std::string(keys::kDescription));
    std::string identifier;
    if (description == streak->end() || !description->is_object()
        || !ParseStringField(*description, std::string(keys::kIdentifier),
                             identifier)) {
        AddError(errors, "streak.identifier", "streaks", path,
                 "streak needs '" + std::string(keys::kDescription) + "."
                     + std::string(keys::kIdentifier) + "'");
        return false;
    }
    const std::string violation = NamespaceRuleViolation(mod_id, identifier);
    if (!violation.empty()) {
        AddError(errors, "streak.id.namespace", "streaks", path, violation);
        return false;
    }
    out.streak_id = identifier;
    out.namespace_id = mod_id;
    out.id = identifier.substr(mod_id.size() + 1);
    out.raw = entry;
    if (!ParseStringField(*streak, std::string(keys::kType), out.type)) {
        AddError(errors, "streak.type", "streaks", path,
                 "streak '" + identifier + "' needs a string '"
                     + std::string(keys::kType) + "'");
        return false;
    }
    const auto matcher = streak->find(std::string(keys::kMatcher));
    if (matcher == streak->end()
        || !ParseStreakMatcher(*matcher, out.matcher, errors, path)) {
        if (matcher == streak->end()) {
            AddError(errors, "streak.matcher", "streaks", path,
                     "streak '" + identifier + "' needs a '"
                         + std::string(keys::kMatcher) + "'");
        }
        return false;
    }
    (void)ParseStringField(*streak, std::string(keys::kGroupBy), out.group_by);
    (void)ParseStringField(*streak, std::string(keys::kValueField),
                           out.value_field);
    const auto reset_on = streak->find(std::string(keys::kResetOn));
    if (reset_on != streak->end()) {
        if (!reset_on->is_array()
            || reset_on->size() > keys::kMaxResetMatchers) {
            AddError(errors, "streak.matcher", "streaks", path,
                     "'" + std::string(keys::kResetOn)
                         + "' must be an array of at most "
                         + std::to_string(keys::kMaxResetMatchers)
                         + " matchers");
            return false;
        }
        for (const nlohmann::json& item : *reset_on) {
            StreakMatcherDef reset;
            if (!ParseStreakMatcher(item, reset, errors, path)) return false;
            out.reset_on.push_back(std::move(reset));
        }
    }
    return true;
}

bool ScanStreaksDirectory(const fs::path& dir, const std::string& mod_id,
                          std::vector<StreakDef>& out,
                          std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "streaks", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "streaks")) {
            ok = false;
            continue;
        }
        StreakDef streak;
        if (!ParseStreakEntry(entry, mod_id, streak, errors, file.string())) {
            ok = false;
            continue;
        }
        if (!seen.insert(streak.streak_id).second) {
            AddError(errors, "streak.id.duplicate", "streaks", file.string(),
                     "duplicate streak id '" + streak.streak_id + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(streak));
    }
    return ok;
}

/* INFO: one list of seat matchers of a derived event: a matcher plus the
 *       payload field that names the seat. */
bool ParseSeatMatchers(const nlohmann::json& parent, std::string_view key,
                       std::vector<DerivedSeatMatcherDef>& out,
                       std::vector<LoadError>& errors,
                       const std::string& path) {
    const auto list = parent.find(std::string(key));
    if (list == parent.end()) return true;
    if (!list->is_array() || list->empty()
        || list->size() > derived_keys::kMaxSeatMatchers) {
        AddError(errors, "derived.matcher", "derived", path,
                 "'" + std::string(key) + "' must be an array of 1 to "
                     + std::to_string(derived_keys::kMaxSeatMatchers)
                     + " matchers");
        return false;
    }
    for (const nlohmann::json& item : *list) {
        DerivedSeatMatcherDef seat_matcher;
        if (!ParseStreakMatcher(item, seat_matcher.matcher, errors, path,
                                "derived.matcher", "derived")) {
            return false;
        }
        if (!ParseStringField(item, std::string(derived_keys::kSeatField),
                              seat_matcher.seat_field)
            || seat_matcher.seat_field.empty()) {
            AddError(errors, "derived.matcher", "derived", path,
                     "a matcher of '" + std::string(key) + "' needs '"
                         + std::string(derived_keys::kSeatField) + "'");
            return false;
        }
        out.push_back(std::move(seat_matcher));
    }
    return true;
}

/* INFO: one derived event definition per file, version 1 style. Structural
 *       checks and the namespace rule only. Only the `uni` mod may define
 *       derived events: they are core events. */
bool ParseDerivedEntry(const nlohmann::json& entry, const std::string& mod_id,
                       DerivedEventDef& out, std::vector<LoadError>& errors,
                       const std::string& path) {
    namespace keys = derived_keys;
    if (!entry.is_object()) {
        AddError(errors, "derived.shape", "derived", path,
                 "derived event file must be a JSON object");
        return false;
    }
    const auto version =
        entry.find(std::string(streak_keys::kFormatVersion));
    if (version == entry.end() || !version->is_string()
        || !IsSupportedFormatVersion(version->get<std::string>())) {
        AddError(errors, "derived.format_version", "derived", path,
                 "derived event file needs '"
                     + std::string(streak_keys::kFormatVersion)
                     + "' with major version "
                     + std::to_string(event_keys::kSupportedFormatMajor));
        return false;
    }
    const auto body = entry.find(std::string(keys::kDerivedEvent));
    if (body == entry.end() || !body->is_object()) {
        AddError(errors, "derived.shape", "derived", path,
                 "derived event file needs a '"
                     + std::string(keys::kDerivedEvent) + "' object");
        return false;
    }
    const auto description =
        body->find(std::string(streak_keys::kDescription));
    std::string identifier;
    if (description == body->end() || !description->is_object()
        || !ParseStringField(*description,
                             std::string(streak_keys::kIdentifier),
                             identifier)) {
        AddError(errors, "derived.identifier", "derived", path,
                 "derived event needs '"
                     + std::string(streak_keys::kDescription) + "."
                     + std::string(streak_keys::kIdentifier) + "'");
        return false;
    }
    std::string violation = NamespaceRuleViolation(mod_id, identifier);
    if (violation.empty() && mod_id != "uni") {
        violation = "mod '" + mod_id
                    + "' cannot define a derived event: derived events are "
                      "core events and only the uni mod defines them";
    }
    if (!violation.empty()) {
        AddError(errors, "derived.id.namespace", "derived", path, violation);
        return false;
    }
    out.derived_id = identifier;
    out.namespace_id = mod_id;
    out.id = identifier.substr(mod_id.size() + 1);
    out.raw = entry;
    if (!ParseStringField(*body, std::string(keys::kEmits), out.emits)) {
        AddError(errors, "derived.emits", "derived", path,
                 "derived event '" + identifier + "' needs a string '"
                     + std::string(keys::kEmits) + "'");
        return false;
    }
    if (!ParseSeatMatchers(*body, keys::kOpenOn, out.open_on, errors, path)
        || !ParseSeatMatchers(*body, keys::kCloseOn, out.close_on, errors,
                              path)
        || !ParseSeatMatchers(*body, keys::kBlockOn, out.block_on, errors,
                              path)) {
        return false;
    }
    (void)ParseStringField(*body, std::string(keys::kWindowOf),
                           out.window_of);
    const bool is_window = !out.open_on.empty();
    const bool is_block = !out.block_on.empty() && !out.window_of.empty();
    const bool stray_window_keys = !out.close_on.empty() && !is_window;
    const bool stray_block_keys =
        (!out.block_on.empty() || !out.window_of.empty()) && !is_block;
    if (is_window == is_block || stray_window_keys || stray_block_keys) {
        AddError(errors, "derived.kind", "derived", path,
                 "derived event '" + identifier + "' must set either '"
                     + std::string(keys::kOpenOn) + "' (a window) or '"
                     + std::string(keys::kBlockOn) + "' with '"
                     + std::string(keys::kWindowOf) + "' (a block)");
        return false;
    }
    out.kind = std::string(is_window ? keys::kWindowKind : keys::kBlockKind);
    return true;
}

bool ScanDerivedDirectory(const fs::path& dir, const std::string& mod_id,
                          std::vector<DerivedEventDef>& out,
                          std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "derived", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "derived")) {
            ok = false;
            continue;
        }
        DerivedEventDef derived;
        if (!ParseDerivedEntry(entry, mod_id, derived, errors,
                               file.string())) {
            ok = false;
            continue;
        }
        if (!seen.insert(derived.derived_id).second) {
            AddError(errors, "derived.id.duplicate", "derived", file.string(),
                     "duplicate derived event id '" + derived.derived_id
                         + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(derived));
    }
    return ok;
}

/* INFO: one explicit glossary entry per file, version 1 style. The entry
 *       holds translation keys only. Structural checks and the namespace
 *       rule; the key syntax is a semantic check. */
bool ParseGlossaryEntry(const nlohmann::json& entry, const std::string& mod_id,
                        GlossaryEntryDef& out, std::vector<LoadError>& errors,
                        const std::string& path) {
    namespace keys = glossary_keys;
    if (!entry.is_object()) {
        AddError(errors, "glossary.shape", "glossary", path,
                 "glossary file must be a JSON object");
        return false;
    }
    const auto version = entry.find(std::string(keys::kFormatVersion));
    if (version == entry.end() || !version->is_string()
        || !IsSupportedFormatVersion(version->get<std::string>())) {
        AddError(errors, "glossary.format_version", "glossary", path,
                 "glossary file needs '" + std::string(keys::kFormatVersion)
                     + "' with major version "
                     + std::to_string(event_keys::kSupportedFormatMajor));
        return false;
    }
    const auto body = entry.find(std::string(keys::kGlossaryEntry));
    if (body == entry.end() || !body->is_object()) {
        AddError(errors, "glossary.shape", "glossary", path,
                 "glossary file needs a '"
                     + std::string(keys::kGlossaryEntry) + "' object");
        return false;
    }
    const auto description = body->find(std::string(keys::kDescription));
    std::string identifier;
    if (description == body->end() || !description->is_object()
        || !ParseStringField(*description, std::string(keys::kIdentifier),
                             identifier)) {
        AddError(errors, "glossary.identifier", "glossary", path,
                 "glossary entry needs '" + std::string(keys::kDescription)
                     + "." + std::string(keys::kIdentifier) + "'");
        return false;
    }
    const std::string violation = NamespaceRuleViolation(mod_id, identifier);
    if (!violation.empty()) {
        AddError(errors, "glossary.id.namespace", "glossary", path,
                 violation);
        return false;
    }
    out.path = path;
    out.entry_id = identifier;
    out.namespace_id = mod_id;
    out.raw = entry;
    if (!ParseStringField(*body, std::string(keys::kLabelKey), out.label_key)
        || !ParseStringField(*body, std::string(keys::kDescriptionKey),
                             out.description_key)) {
        AddError(errors, "glossary.keys", "glossary", path,
                 "glossary entry '" + identifier + "' needs the string keys '"
                     + std::string(keys::kLabelKey) + "' and '"
                     + std::string(keys::kDescriptionKey) + "'");
        return false;
    }
    return true;
}

bool ScanGlossaryDirectory(const fs::path& dir, const std::string& mod_id,
                           std::vector<GlossaryEntryDef>& out,
                           std::vector<LoadError>& errors) {
    bool ok = true;
    std::set<std::string> seen;
    for (const fs::path& file : ListJsonFiles(dir, "glossary", errors)) {
        nlohmann::json entry;
        if (!ReadJsonFile(file, entry, errors, "glossary")) {
            ok = false;
            continue;
        }
        GlossaryEntryDef definition;
        if (!ParseGlossaryEntry(entry, mod_id, definition, errors,
                                file.string())) {
            ok = false;
            continue;
        }
        if (!seen.insert(definition.entry_id).second) {
            AddError(errors, "glossary.id.duplicate", "glossary", file.string(),
                     "duplicate glossary entry id '" + definition.entry_id
                         + "'");
            ok = false;
            continue;
        }
        out.push_back(std::move(definition));
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
        "mod.json", "kinds", "attributes", "properties", "rules", "statuses",
        "mutations", "decks", "assets", "tables", "entities", "presentations",
        "events", "texts", "glossary"};
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const std::string name = entry.path().filename().string();
        /* INFO: a `cards/` folder is refused by ParseModFolder before this
         *       check runs, so the mod never reaches here; skip the name
         *       anyway so a second wording cannot appear if the order changes. */
        if (name == "cards") continue;
        if (kKnownEntries.count(name) == 0) {
            AddWarning(warnings, "mod.unknown_entry", "mod",
                       entry.path().string(),
                       "unknown top-level entry '" + name + "' is ignored");
        }
    }

    return ok;
}

}  // namespace

bool IsDevContentAllowed() { return DevContentAllowed(); }

std::string CatalogDiagnosticModId(const std::string& file) {
    return DiagnosticModId(file);
}

std::map<std::string, std::int64_t> DefaultSettings(const LoadedMod& mod) {
    std::map<std::string, std::int64_t> settings;
    for (const SettingDecl& setting : mod.manifest.settings) {
        if (setting.id.empty()) continue;
        if (setting.type != "int") continue;
        if (!setting.default_value.is_number_integer()) continue;
        settings[setting.id] = setting.default_value.get<std::int64_t>();
    }
    return settings;
}

DeckDef ExpandDeckDef(const std::vector<LoadedMod>& mods,
                      const std::string& deck_id,
                      const std::vector<std::string>& active_mod_ids,
                      const std::map<std::string, std::int64_t>& settings,
                      std::string& error) {
    std::vector<match::cardschema::ModSchema> schemas;
    schemas.reserve(mods.size());
    for (const LoadedMod& mod : mods) schemas.push_back(mod.card_schema);

    const match::cardschema::ExpandedDeck expanded = ExpandDeckFromSchemas(
        schemas, nullptr, deck_id, active_mod_ids, settings);
    error.clear();
    if (!expanded.diagnostics.empty()) {
        error = match::expr::FormatDiagnostic(expanded.diagnostics.front());
        return {};
    }
    return BuildDeckDef(expanded, active_mod_ids, settings);
}

std::vector<std::string> CatalogDiagnosticOwners(
    const std::string& file, const std::vector<std::string>& loaded_mod_ids) {
    const std::string candidate = DiagnosticModId(file);
    // INFO: only a label that names one of the loaded mods is attributable.
    //       Anything else (no `mods/` prefix, a stray folder, a mod that is not
    //       in the list) fails closed instead of guessing an owner.
    if (std::find(loaded_mod_ids.begin(), loaded_mod_ids.end(), candidate)
        != loaded_mod_ids.end()) {
        return {candidate};
    }
    return loaded_mod_ids;
}

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

bool CardActive(const CardDef& card,
                const std::vector<std::string>& active_mod_ids) {
    for (const std::string& required : card.requires_mods) {
        if (std::find(active_mod_ids.begin(), active_mod_ids.end(), required)
            == active_mod_ids.end()) {
            return false;
        }
    }
    return true;
}

std::string NamespaceRuleViolation(const std::string& mod_id,
                                   const std::string& full_id) {
    if (!IsValidKindId(full_id)) {
        return "id '" + full_id
               + "' needs the form namespace:name; an id with no namespace "
                 "could collide with a core event";
    }
    const std::string id_namespace = full_id.substr(0, full_id.find(':'));
    if (id_namespace == mod_id) return std::string();
    if (IsBaseModId(id_namespace)) {
        return "mod '" + mod_id + "' defines the uni id '" + full_id
               + "'; only the uni mod defines uni: ids";
    }
    return "mod '" + mod_id + "' defines id '" + full_id
           + "' outside its own namespace";
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

    if (IsBaseModId(manifest.id) && folder_name != kBaseModId) {
        AddError(errors, "manifest.id.reserved", "mod", manifest_path.string(),
                 "manifest id 'uni' is reserved for the folder 'uni'");
        return false;
    }

    if (!IsValidModVersion(manifest.version)) {
        AddError(errors, "manifest.version", "mod", manifest_path.string(),
                 "manifest version '" + manifest.version
                     + "' may hold only letters, digits, '.', '+' and '-'");
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
    loaded.content_hash = ComputeModContentHash(folder_path);
    if (loaded.content_hash.empty()) {
        AddError(errors, "mod.hash", "mod", manifest_path.string(),
                 "cannot read every file of the mod to compute its hash");
        return false;
    }

    /* INFO: Entry sets are directories; a missing directory is an empty
     *       set, so a mod declares only the content it ships. Schema v1 card
     *       sources (attributes, kinds, decks, properties) load through the
     *       cardschema loader; the catalog is expanded once for every mod in
     *       ScanModsDirectory. */
    bool ok = true;
    /* INFO: schema 0 shipped one file per card under `cards/`. The v1 loader
     *       reads `kinds/` instead and refuses the old folder here, before the
     *       schema files are read, so an old mod that also still ships an old
     *       deck file shows the migration hint instead of only a deck error. */
    std::error_code cards_error;
    const fs::path cards_path = root / "cards";
    if (fs::is_directory(cards_path, cards_error)) {
        AddError(errors, "card_schema", "cards", cards_path.string(),
                 "cards/ is the removed schema 0 form; move the files to "
                 "kinds/ (see docs)");
        ok = false;
    }
    match::cardschema::LoadOutcome outcome =
        match::cardschema::LoadModSchema(root, loaded.manifest.id);
    if (!outcome.diagnostics.empty()) {
        AppendSchemaErrors(errors, outcome.diagnostics);
        ok = false;
    }
    loaded.card_schema = std::move(outcome.schema);
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
    if (!ScanAssetsDirectory(root / "assets", loaded.manifest.id, loaded.assets,
                             errors)) {
        ok = false;
    }
    if (!ScanTablesDirectory(root / "tables", loaded.manifest.id, loaded.tables,
                             errors)) {
        ok = false;
    }
    if (!ScanEntitiesDirectory(root / "entities", loaded.manifest.id,
                               loaded.entities, errors)) {
        ok = false;
    }
    if (!ScanEventsDirectory(root / "events", loaded.manifest.id,
                             loaded.events, errors)) {
        return false;
    }
    if (!ScanStreaksDirectory(root / "streaks", loaded.manifest.id,
                              loaded.streaks, errors)) {
        return false;
    }
    if (!ScanDerivedDirectory(root / "derived", loaded.manifest.id,
                              loaded.derived_events, errors)) {
        return false;
    }
    if (!ScanPresentationsDirectory(root / "presentations", loaded.manifest.id,
                                    loaded.presentations, errors)) {
        ok = false;
    }
    if (!ScanBindingsDirectory(root / "presentations", loaded.manifest.id,
                               loaded.bindings, errors)) {
        ok = false;
    }

    if (!ScanGlossaryDirectory(root / "glossary", loaded.manifest.id,
                               loaded.glossary, errors)) {
        ok = false;
    }

    if (!ScanTextsDirectory(root, loaded.texts, errors)) ok = false;

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
     *       failed. Fail-closed moves to the match boundary.
     *
     *       The scan runs in two passes. Pass one parses every folder, so the
     *       card catalog can be expanded once over all of them; the generated
     *       cards and decks are then attached to their host mods. Pass two runs
     *       the whole-mod validation and publishes the reports. */
    std::vector<PendingMod> pending;
    std::unordered_set<std::string> seen_ids;
    for (const auto& folder : folders) {
        PendingMod entry;
        entry.report.folder = folder.string();
        entry.report.mod_id = folder.filename().string();

        bool ok = ParseModFolder(folder.string(), folder.filename().string(),
                                 entry.mod, entry.errors);
        if (ok && entry.mod.manifest.dev_only && !DevContentAllowed()) {
            continue;
        }
        if (ok) {
            entry.report.mod_id = entry.mod.manifest.id;
            if (!seen_ids.insert(entry.mod.manifest.id).second) {
                AddError(entry.errors, "mod.id.duplicate", "mod",
                         (folder / "mod.json").string(),
                         "duplicate mod id '" + entry.mod.manifest.id + "'");
                ok = false;
            }
        }
        entry.ok = ok;
        pending.push_back(std::move(entry));
    }

    /* INFO: the catalog expands once for every mod that parsed, so a card a
     *       mod's template generates over another mod's members is seen before
     *       the per-mod card lists are built. A diagnostic is charged to the
     *       mod its file label names, and that mod is dropped like any other
     *       bad mod. A diagnostic whose label names no mod folder fails closed:
     *       it goes to the flat report and drops every mod that contributed to
     *       the catalog, because no narrower owner is known. */
    const std::size_t decks_per_mod =
        match::cardschema::kMaximumDecksPerMod;
    const std::size_t decks_total = match::cardschema::kMaximumDecksTotal;
    std::size_t declared_decks = 0;
    for (const PendingMod& entry : pending) {
        if (entry.ok) declared_decks += entry.mod.card_schema.decks.size();
    }
    for (PendingMod& entry : pending) {
        if (!entry.ok) continue;
        const std::size_t count = entry.mod.card_schema.decks.size();
        if (count > decks_per_mod) {
            AddError(entry.errors, "card_schema", "decks",
                     (fs::path(entry.mod.path) / "mod.json").string(),
                     "mod \"" + entry.mod.manifest.id + "\" declares "
                         + std::to_string(count) + " decks; the limit is "
                         + std::to_string(decks_per_mod));
            entry.ok = false;
        }
    }
    if (declared_decks > decks_total) {
        /* INFO: no single mod may be over its own limit while the scan is over
         *       the total one, so every mod that still parses is dropped with
         *       the same reason: the deck budget belongs to the whole scan, and
         *       a mod that silently disappeared would be worse. */
        for (PendingMod& entry : pending) {
            if (!entry.ok) continue;
            AddError(entry.errors, "card_schema", "decks",
                     (fs::path(entry.mod.path) / "mod.json").string(),
                     "mod \"" + entry.mod.manifest.id + "\" declares "
                         + std::to_string(entry.mod.card_schema.decks.size())
                         + " decks; the mods together declare "
                         + std::to_string(declared_decks) + ", the limit is "
                         + std::to_string(decks_total));
            entry.ok = false;
        }
    }

    /* INFO: the catalog is expanded once over every mod that parsed, and the
     *       generated cards are converted from it. A diagnostic is charged to
     *       the mod its label names and drops only that mod; a card whose
     *       `requires_mods` names a mod that no longer loaded is dropped at
     *       conversion time, because its members would be absent at match time.
     *       Decks expand from a schema list rebuilt over the mods still `ok`, so
     *       a dropped mod lends no member or template to another mod's deck. */
    std::vector<match::cardschema::ModSchema> schemas;
    for (const PendingMod& entry : pending) {
        if (entry.ok) schemas.push_back(entry.mod.card_schema);
    }

    const match::cardschema::CatalogResult catalog =
        match::cardschema::ExpandCards(schemas);
    std::vector<match::cardschema::Diagnostic> catalog_diagnostics =
        catalog.diagnostics;
    for (match::cardschema::Diagnostic& diagnostic :
         match::cardschema::ValidateCatalog(schemas)) {
        catalog_diagnostics.push_back(std::move(diagnostic));
    }
    std::vector<std::string> loaded_mod_ids;
    for (const PendingMod& entry : pending) {
        if (entry.ok) loaded_mod_ids.push_back(entry.mod.manifest.id);
    }
    for (const match::cardschema::Diagnostic& diagnostic : catalog_diagnostics) {
        const std::vector<std::string> owners =
            CatalogDiagnosticOwners(diagnostic.file, loaded_mod_ids);
        if (owners.size() != 1) {
            /* INFO: fail closed. A label with no owner among the loaded mods
             *       names nobody, so the diagnostic goes to the flat report and
             *       every mod that contributed to the catalog is dropped;
             *       charging it to an arbitrary mod would let an unrelated one
             *       disappear. */
            AppendSchemaDiagnostic(result.errors, diagnostic);
            for (PendingMod& candidate : pending) {
                if (!candidate.ok) continue;
                candidate.ok = false;
                AppendSchemaDiagnostic(candidate.errors, diagnostic);
            }
            continue;
        }
        PendingMod* owner = PendingById(pending, owners.front());
        if (owner == nullptr) {
            AppendSchemaDiagnostic(result.errors, diagnostic);
            continue;
        }
        AppendSchemaDiagnostic(owner->errors, diagnostic);
        owner->ok = false;
    }

    for (PendingMod& entry : pending) {
        if (!entry.ok) continue;
        const std::string host_mod = entry.mod.manifest.id;
        for (const match::cardschema::GeneratedCard& generated : catalog.cards) {
            if (generated.host_mod != host_mod) continue;
            /* INFO: this card can be built at match time only when every mod it
             *       uses loaded; a card over a dropped mod's members is dropped
             *       here, while the host mod stays. */
            const bool all_loaded =
                std::all_of(generated.requires_mods.begin(),
                            generated.requires_mods.end(),
                            [&pending](const std::string& mod_id) {
                                const PendingMod* required =
                                    PendingById(pending, mod_id);
                                return required != nullptr && required->ok;
                            });
            if (!all_loaded) continue;
            const std::string label =
                KindFileLabel(entry.mod.card_schema, generated.template_id);
            CardDef card;
            if (!ParseCardObject(generated.legacy_entry, label, host_mod, card,
                                 entry.errors)) {
                entry.ok = false;
                continue;
            }
            /* INFO: `requires_mods` is recorded here and not filtered here: a
             *       match that does not enable one of those mods must not deal
             *       the card. Readers filter the generated card with
             *       `modload::CardActive`; the loader drops a card whose
             *       required mod did not load. */
            card.requires_mods = generated.requires_mods;
            card.slot_members = generated.slot_members;
            card.template_id = generated.template_id;
            entry.mod.cards.push_back(std::move(card));
        }
    }

    /* INFO: the whole-mod validation runs in pass one, before any deck expands:
     *       a mod that fails it must not lend cards or decks to the mods that
     *       stay. It needs the generated cards for the asset checks, so it runs
     *       after the conversion above. */
    for (PendingMod& entry : pending) {
        if (!entry.ok) continue;
        entry.ok = ValidateModContent(entry.mod, entry.warnings, entry.errors,
                                      entry.report.asset_count,
                                      entry.report.asset_bytes);
    }

    /* INFO: a mod can still drop here, so rebuild the deck schema list from the
     *       mods that remain and re-filter every card over the final loaded set.
     */
    schemas.clear();
    std::set<std::string> active_mod_ids;
    for (const PendingMod& entry : pending) {
        if (!entry.ok) continue;
        schemas.push_back(entry.mod.card_schema);
        active_mod_ids.insert(entry.mod.manifest.id);
    }
    for (PendingMod& entry : pending) {
        if (!entry.ok) continue;
        std::vector<CardDef> kept;
        kept.reserve(entry.mod.cards.size());
        for (CardDef& card : entry.mod.cards) {
            const bool all_loaded =
                std::all_of(card.requires_mods.begin(), card.requires_mods.end(),
                            [&active_mod_ids](const std::string& mod_id) {
                                return active_mod_ids.count(mod_id) != 0;
                            });
            if (all_loaded) kept.push_back(std::move(card));
        }
        entry.mod.cards = std::move(kept);
    }

    for (PendingMod& entry : pending) {
        if (!entry.ok) continue;
        const std::string host_mod = entry.mod.manifest.id;
        for (const match::cardschema::DeckSpec& deck : entry.mod.card_schema.decks) {
            std::vector<std::string> active_mods = deck.mods;
            if (active_mods.empty()) active_mods.push_back(host_mod);
            std::map<std::string, std::int64_t> settings;
            if (!MergeDeckSettings(pending, active_mods, host_mod, deck.file,
                                   settings, entry.errors)) {
                entry.ok = false;
                break;
            }
            const match::cardschema::ExpandedDeck expanded =
                ExpandDeckFromSchemas(schemas, &catalog, deck.identifier,
                                      active_mods, settings);
            if (!expanded.diagnostics.empty()) {
                AppendSchemaErrors(entry.errors, expanded.diagnostics);
                entry.ok = false;
                break;
            }
            entry.mod.decks.push_back(
                BuildDeckDef(expanded, active_mods, settings));
        }
    }

    for (PendingMod& entry : pending) {
        entry.report.ok = entry.ok;
        entry.report.errors = std::move(entry.errors);
        entry.report.warnings = std::move(entry.warnings);
        for (const LoadError& error : entry.report.errors) {
            result.errors.push_back(error);
        }
        for (const LoadWarning& warning : entry.report.warnings) {
            result.warnings.push_back(warning);
        }
        if (entry.ok) result.mods.push_back(std::move(entry.mod));
        result.reports.push_back(std::move(entry.report));
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
