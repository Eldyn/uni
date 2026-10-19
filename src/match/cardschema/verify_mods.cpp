#include <match/cardschema/verify_mods.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <match/cardschema/catalog.hpp>
#include <match/cardschema/internal.hpp>
#include <match/cardschema/loader.hpp>
#include <match/cardschema/model.hpp>
#include <match/cardschema/resolve.hpp>
#include <match/cardschema/validate.hpp>
#include <match/expr/diagnostic.hpp>
#include <match/expr/schema_json.hpp>
#include <match/expr/verify_command.hpp>

namespace match::cardschema {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr std::string_view kUsage =
    "usage: uni-mods verify --mods-root <dir> [--mod <id>] [--deck <identifier>]\n";

constexpr std::string_view kModsPrefix = "mods/";
constexpr std::string_view kManifestName = "mod.json";
constexpr std::string_view kMoreDiagnosticsMarker = "more diagnostics not shown";
constexpr std::string_view kSymbolicLinkMessage = "symbolic links are not allowed";

// INFO: the manifest cap matches the card-file cap of the loader; an oversized
// manifest is a diagnostic, not an IO error.
constexpr std::uintmax_t kMaximumManifestBytes = 1024 * 1024;
constexpr std::size_t kMaximumMods = 64;
constexpr std::size_t kMaximumDecks = 256;

struct Options {
    std::string mods_root;
    std::string mod;
    std::string deck;
    bool has_mods_root = false;
    bool has_mod = false;
    bool has_deck = false;
};

// INFO: one row per flag drives both the "given twice" check and the value read,
// so a new flag costs no branch.
struct FlagBinding {
    std::string_view name;
    std::string Options::* value;
    bool Options::* seen;
};

constexpr FlagBinding kFlagBindings[] = {
    {"--mods-root", &Options::mods_root, &Options::has_mods_root},
    {"--mod", &Options::mod, &Options::has_mod},
    {"--deck", &Options::deck, &Options::has_deck},
};

// INFO: `complete` is set when every required flag is present. `usage_only`
// marks the missing-flag case, whose only output is the usage line.
struct ParseResult {
    bool complete = false;
    bool usage_only = false;
    std::string error;
};

ParseResult ParseArgs(const std::vector<std::string>& args, Options& options) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string& arg = args[index];
        const FlagBinding* binding = nullptr;
        for (const FlagBinding& candidate : kFlagBindings) {
            if (arg == candidate.name) {
                binding = &candidate;
                break;
            }
        }
        if (binding == nullptr) {
            return {false, false, "unknown flag \"" + arg + "\""};
        }
        if (options.*(binding->seen)) {
            return {false, false, "flag \"" + std::string(binding->name) + "\" given twice"};
        }
        if (index + 1 >= args.size()) {
            return {false, false, std::string(binding->name) + " needs a value"};
        }
        options.*(binding->value) = args[++index];
        options.*(binding->seen) = true;
    }
    if (!options.has_mods_root) {
        return {false, true, {}};
    }
    return {true, false, {}};
}

void ReportManifestTooLarge(const std::string& label, std::vector<Diagnostic>& diagnostics) {
    internal::AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                       "file is too large (more than " +
                           std::to_string(kMaximumManifestBytes) + " bytes)");
}

// INFO: the read failed before any JSON could be seen (a stat or an open
// failure); the caller stops the run with the IO exit code instead of a
// diagnostic. Otherwise the JSON-level problems are diagnostics that mirror the
// card loader: duplicate keys, invalid JSON, too deep nesting, a non-object.
enum class ManifestRead { ok, io_error };

ManifestRead ReadManifest(const fs::path& path, const std::string& label, json& manifest,
                          std::vector<Diagnostic>& diagnostics, std::ostream& err) {
    std::error_code size_error;
    const std::uintmax_t size = fs::file_size(path, size_error);
    if (size_error) {
        err << "cannot read \"" << path.generic_string() << "\": " << size_error.message()
            << "\n";
        return ManifestRead::io_error;
    }
    if (size > kMaximumManifestBytes) {
        ReportManifestTooLarge(label, diagnostics);
        return ManifestRead::ok;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        err << "cannot read \"" << path.generic_string() << "\": " << std::strerror(errno)
            << "\n";
        return ManifestRead::io_error;
    }
    // INFO: read one byte past the cap so a file that grew after the stat is
    // still rejected, like the card loader's bounded read.
    std::string text(static_cast<std::size_t>(size) + 1, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(stream.gcount()));
    if (text.size() > kMaximumManifestBytes) {
        ReportManifestTooLarge(label, diagnostics);
        return ManifestRead::ok;
    }

    match::expr::SchemaScan scan;
    const std::optional<json> parsed = match::expr::ParseSchemaJson(text, scan);
    for (const match::expr::DuplicateKey& duplicate : scan.duplicates) {
        internal::AddError(diagnostics, label, duplicate.pointer,
                           "duplicate key \"" + duplicate.key + "\"");
    }
    if (scan.duplicates_truncated) {
        internal::AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                           "more duplicate keys not shown");
    }
    if (!parsed.has_value()) {
        internal::AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                           "invalid JSON");
        return ManifestRead::ok;
    }
    if (scan.nested_too_deep) {
        internal::AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                           "schema is nested too deeply (more than " +
                               std::to_string(match::expr::kMaximumSchemaDepth) + " levels)");
        return ManifestRead::ok;
    }
    if (!parsed->is_object()) {
        internal::AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                           "file must be a JSON object");
        return ManifestRead::ok;
    }
    manifest = *parsed;
    return ManifestRead::ok;
}

// INFO: the mod id of a manifest. A missing `id` falls back to the folder name.
// A present `id` that is not a string, is empty, or differs from the folder name
// is a diagnostic; the folder name is used anyway so every later label still
// names the real folder.
std::string ManifestId(const json& manifest, const std::string& folder_name,
                       const std::string& label, std::vector<Diagnostic>& diagnostics) {
    if (!manifest.is_object()) {
        return folder_name;
    }
    const auto id = manifest.find("id");
    if (id == manifest.end()) {
        return folder_name;
    }
    const std::string pointer =
        match::expr::AppendPointerToken(match::expr::kRootPointer, "id");
    if (!id->is_string()) {
        internal::AddError(diagnostics, label, pointer, "mod \"id\" must be a string");
        return folder_name;
    }
    const std::string text = id->get<std::string>();
    if (text.empty()) {
        internal::AddError(diagnostics, label, pointer, "mod \"id\" must not be empty");
        return folder_name;
    }
    if (text != folder_name) {
        internal::AddError(diagnostics, label, pointer,
                           "mod id \"" + text + "\" does not match the folder name \"" +
                               folder_name + "\"");
        return folder_name;
    }
    return text;
}

// INFO: the `settings` array of one manifest. Every entry must be an object with
// a non-empty string `id`, an `int` `type`, and an integer `default`; every other
// shape is a diagnostic at the entry's pointer. A setting id declared by two mods
// is an error naming both mods, and the first declaration wins.
void CollectSettings(const json& manifest, const std::string& mod_id, const std::string& label,
                     std::map<std::string, std::string>& declared_by,
                     std::map<std::string, int64_t>& settings,
                     std::vector<Diagnostic>& diagnostics) {
    if (!manifest.is_object()) {
        return;
    }
    const auto entries = manifest.find("settings");
    if (entries == manifest.end()) {
        return;
    }
    if (!entries->is_array()) {
        internal::AddError(diagnostics, label,
                           match::expr::AppendPointerToken(match::expr::kRootPointer, "settings"),
                           "setting list must be an array");
        return;
    }
    std::size_t position = 0;
    const std::string list =
        match::expr::AppendPointerToken(match::expr::kRootPointer, "settings");
    for (const json& entry : *entries) {
        const std::string pointer =
            match::expr::AppendPointerToken(list, std::to_string(position));
        ++position;
        if (!entry.is_object()) {
            internal::AddError(diagnostics, label, pointer, "setting must be an object");
            continue;
        }
        const auto id = entry.find("id");
        if (id == entry.end() || !id->is_string() || id->get<std::string>().empty()) {
            const std::string message = id != entry.end() && id->is_string()
                                            ? "setting \"id\" must not be empty"
                                            : "setting \"id\" must be a non-empty string";
            internal::AddError(diagnostics, label, pointer, message);
            continue;
        }
        const std::string name = id->get<std::string>();
        const auto type = entry.find("type");
        if (type == entry.end() || !type->is_string() || type->get<std::string>() != "int") {
            internal::AddError(diagnostics, label, pointer,
                               "setting \"" + name + "\" must have type \"int\"");
            continue;
        }
        const auto value = entry.find("default");
        if (value == entry.end() || !value->is_number_integer()) {
            internal::AddError(diagnostics, label, pointer,
                               "setting \"" + name + "\" must have an integer default");
            continue;
        }
        const auto previous = declared_by.find(name);
        if (previous != declared_by.end()) {
            internal::AddError(diagnostics, label, pointer,
                               "setting id \"" + name + "\" is declared by mods \"" +
                                   previous->second + "\" and \"" + mod_id + "\"");
            continue;
        }
        declared_by.emplace(name, mod_id);
        settings[name] = value->get<int64_t>();
    }
}

void ReportModsRootError(const std::string& root, const std::string& reason, std::ostream& err) {
    err << "cannot read mods root \"" << root << "\": " << reason << "\n";
}

// INFO: the direct subfolders of the root, sorted by name. A symlinked folder is
// reported instead of followed, so a run never depends on where a link points.
// Returns false after reporting an iteration failure.
bool CollectModFolders(const std::string& root, std::vector<fs::path>& mod_folders,
                       std::vector<Diagnostic>& diagnostics, std::ostream& err) {
    std::error_code error;
    fs::directory_iterator iterator(root, error);
    if (error) {
        ReportModsRootError(root, error.message(), err);
        return false;
    }
    const fs::directory_iterator end;
    while (iterator != end) {
        const fs::path path = iterator->path();
        std::error_code link_error;
        const fs::file_status link_status = fs::symlink_status(path, link_error);
        if (link_error) {
            ReportModsRootError(root, link_error.message(), err);
            return false;
        }
        if (fs::is_symlink(link_status)) {
            std::error_code target_error;
            const fs::file_status target_status = fs::status(path, target_error);
            if (!target_error && fs::is_directory(target_status)) {
                internal::AddError(diagnostics, path.filename().generic_string(),
                                   std::string(match::expr::kRootPointer),
                                   std::string(kSymbolicLinkMessage));
            }
        } else if (fs::is_directory(link_status)) {
            mod_folders.push_back(path);
        }
        iterator.increment(error);
        if (error) {
            ReportModsRootError(root, error.message(), err);
            return false;
        }
    }
    std::sort(mod_folders.begin(), mod_folders.end(),
              [](const fs::path& left, const fs::path& right) {
                  return left.filename().generic_string() < right.filename().generic_string();
              });
    return true;
}

// INFO: a diagnostic is reported when its label does not name a mod folder, when
// the label names the selected mod, or when the selected mod is involved in a
// cross-mod problem (its identifier namespace appears in the message).
bool IsReported(const Diagnostic& diagnostic, const std::optional<std::string>& selected_mod) {
    if (!selected_mod.has_value()) {
        return true;
    }
    if (diagnostic.file.compare(0, kModsPrefix.size(), kModsPrefix) != 0) {
        return true;
    }
    const std::string prefix = std::string(kModsPrefix) + *selected_mod + "/";
    if (diagnostic.file.compare(0, prefix.size(), prefix) == 0) {
        return true;
    }
    const std::string namespace_token = *selected_mod + ":";
    return diagnostic.message.find(namespace_token) != std::string::npos ||
           diagnostic.suggestion.find(namespace_token) != std::string::npos;
}

std::vector<Diagnostic> FilterDiagnostics(std::vector<Diagnostic> diagnostics,
                                          const std::optional<std::string>& selected_mod) {
    DedupeDiagnostics(diagnostics);
    std::vector<Diagnostic> reported;
    reported.reserve(diagnostics.size());
    for (Diagnostic& diagnostic : diagnostics) {
        if (IsReported(diagnostic, selected_mod)) {
            reported.push_back(std::move(diagnostic));
        }
    }
    return reported;
}

// INFO: prints at most the shared diagnostic cap, then one marker line when the
// report is longer, so a hopeless folder cannot flood the terminal.
void PrintReport(const std::vector<Diagnostic>& reported, std::ostream& err) {
    const std::size_t shown = std::min(reported.size(), internal::kMaximumDiagnostics);
    for (std::size_t index = 0; index < shown; ++index) {
        err << match::expr::FormatDiagnostic(reported[index]) << "\n";
    }
    if (reported.size() > shown) {
        err << kMoreDiagnosticsMarker << "\n";
    }
}

int ReportDiagnostics(std::vector<Diagnostic> diagnostics,
                      const std::optional<std::string>& selected_mod, std::ostream& err) {
    const std::vector<Diagnostic> reported =
        FilterDiagnostics(std::move(diagnostics), selected_mod);
    if (!reported.empty()) {
        PrintReport(reported, err);
    }
    return 1;
}

int RunVerifyModsImpl(const std::vector<std::string>& args, std::ostream& out,
                      std::ostream& err) {
    if (args.empty()) {
        err << kUsage;
        return 2;
    }
    if (args[0] != "verify") {
        err << "unknown command \"" << args[0] << "\"\n" << kUsage;
        return 2;
    }
    Options options;
    const ParseResult parsed = ParseArgs(args, options);
    if (!parsed.complete) {
        if (!parsed.usage_only) {
            err << parsed.error << "\n";
        }
        err << kUsage;
        return 2;
    }

    std::error_code root_error;
    const fs::file_status root_status = fs::status(options.mods_root, root_error);
    if (root_error && root_error != std::errc::no_such_file_or_directory) {
        ReportModsRootError(options.mods_root, root_error.message(), err);
        return 3;
    }
    if (!fs::exists(root_status)) {
        ReportModsRootError(options.mods_root, std::strerror(ENOENT), err);
        return 3;
    }
    if (!fs::is_directory(root_status)) {
        ReportModsRootError(options.mods_root, "not a directory", err);
        return 3;
    }

    std::vector<Diagnostic> diagnostics;
    std::vector<fs::path> mod_folders;
    if (!CollectModFolders(options.mods_root, mod_folders, diagnostics, err)) {
        return 3;
    }

    std::vector<ModSchema> mods;
    std::map<std::string, int64_t> settings;
    std::map<std::string, std::string> declared_by;
    bool too_many_mods = false;
    for (const fs::path& folder : mod_folders) {
        const fs::path manifest_path = folder / std::string(kManifestName);
        std::error_code manifest_error;
        const fs::file_status manifest_status = fs::status(manifest_path, manifest_error);
        if (manifest_error) {
            if (manifest_error == std::errc::no_such_file_or_directory) {
                continue;  // a folder without mod.json is skipped
            }
            err << "cannot read \"" << manifest_path.generic_string() << "\": "
                << manifest_error.message() << "\n";
            return 3;
        }
        if (!fs::is_regular_file(manifest_status)) {
            continue;  // a folder without mod.json is skipped
        }
        if (mods.size() >= kMaximumMods) {
            internal::AddError(diagnostics, options.mods_root,
                               std::string(match::expr::kRootPointer),
                               "too many mods (more than " + std::to_string(kMaximumMods) + ")");
            too_many_mods = true;
            break;
        }
        const std::string folder_name = folder.filename().generic_string();
        const std::string label =
            std::string(kModsPrefix) + folder_name + "/" + std::string(kManifestName);
        json manifest;
        if (ReadManifest(manifest_path, label, manifest, diagnostics, err) ==
            ManifestRead::io_error) {
            return 3;
        }
        const std::string mod_id = ManifestId(manifest, folder_name, label, diagnostics);
        CollectSettings(manifest, mod_id, label, declared_by, settings, diagnostics);

        LoadOutcome outcome = LoadModSchema(folder, mod_id);
        for (Diagnostic& diagnostic : outcome.diagnostics) {
            diagnostics.push_back(std::move(diagnostic));
        }
        mods.push_back(std::move(outcome.schema));
    }

    const std::optional<std::string> selected_mod =
        options.has_mod ? std::optional<std::string>(options.mod) : std::nullopt;

    if (too_many_mods) {
        return ReportDiagnostics(std::move(diagnostics), selected_mod, err);
    }

    std::size_t deck_total = 0;
    for (const ModSchema& mod : mods) {
        deck_total += mod.decks.size();
    }
    if (deck_total > kMaximumDecks) {
        internal::AddError(diagnostics, options.mods_root,
                           std::string(match::expr::kRootPointer),
                           "too many decks (more than " + std::to_string(kMaximumDecks) + ")");
        return ReportDiagnostics(std::move(diagnostics), selected_mod, err);
    }

    if (mods.empty()) {
        if (diagnostics.empty()) {
            err << "no mods found in \"" << options.mods_root << "\"\n";
            return 1;
        }
        return ReportDiagnostics(std::move(diagnostics), selected_mod, err);
    }

    if (selected_mod.has_value()) {
        std::vector<std::string> mod_ids;
        mod_ids.reserve(mods.size());
        bool found = false;
        for (const ModSchema& mod : mods) {
            mod_ids.push_back(mod.mod_id);
            found = found || mod.mod_id == *selected_mod;
        }
        if (!found) {
            diagnostics.push_back(
                Diagnostic{options.mods_root, std::string(match::expr::kRootPointer),
                           "unknown mod \"" + *selected_mod + "\"",
                           match::expr::SuggestClosest(*selected_mod, mod_ids)});
            return ReportDiagnostics(std::move(diagnostics), selected_mod, err);
        }
    }

    std::vector<std::string> deck_ids;
    for (const ModSchema& mod : mods) {
        if (selected_mod.has_value() && mod.mod_id != *selected_mod) {
            continue;
        }
        for (const DeckSpec& deck : mod.decks) {
            deck_ids.push_back(deck.identifier);
        }
    }
    if (options.has_deck) {
        const bool found =
            std::find(deck_ids.begin(), deck_ids.end(), options.deck) != deck_ids.end();
        if (!found) {
            diagnostics.push_back(
                Diagnostic{options.mods_root, std::string(match::expr::kRootPointer),
                           "unknown deck \"" + options.deck + "\"",
                           match::expr::SuggestClosest(options.deck, deck_ids)});
            return ReportDiagnostics(std::move(diagnostics), selected_mod, err);
        }
    }

    const CatalogResult catalog = ExpandCards(mods);
    for (const Diagnostic& diagnostic : catalog.diagnostics) {
        diagnostics.push_back(diagnostic);
    }
    for (const Diagnostic& diagnostic : ValidateCatalog(mods)) {
        diagnostics.push_back(diagnostic);
    }

    std::vector<std::string> deck_lines;
    for (const ModSchema& mod : mods) {
        if (selected_mod.has_value() && mod.mod_id != *selected_mod) {
            continue;
        }
        for (const DeckSpec& deck : mod.decks) {
            if (options.has_deck && deck.identifier != options.deck) {
                continue;
            }
            const ExpandedDeck expanded = ExpandDeck(mods, deck.identifier, {}, settings);
            for (const Diagnostic& diagnostic : expanded.diagnostics) {
                diagnostics.push_back(diagnostic);
            }
            if (expanded.diagnostics.empty()) {
                deck_lines.push_back("deck " + expanded.deck_id + ": " +
                                     std::to_string(expanded.total) + " cards\n");
            }
        }
    }

    const std::vector<Diagnostic> reported =
        FilterDiagnostics(std::move(diagnostics), selected_mod);
    if (!reported.empty()) {
        PrintReport(reported, err);
        return 1;
    }
    out << "ok\n";
    for (const std::string& line : deck_lines) {
        out << line;
    }
    return 0;
}

}  // namespace

int RunVerifyMods(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    try {
        return RunVerifyModsImpl(args, out, err);
    } catch (const std::exception& error) {
        err << "internal error: " << error.what() << "\n";
        return 3;
    }
}

int RunVerifyCommand(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    bool has_mods_root = false;
    bool has_schema = false;
    // INFO: only the flag positions matter; a value that looks like a flag (the
    // `--expression` text of the phase-1 command) must not select a verifier.
    for (std::size_t index = 1; index < args.size(); index += 2) {
        const std::string& arg = args[index];
        if (arg == "--mods-root") {
            has_mods_root = true;
        } else if (arg == "--schema") {
            has_schema = true;
        }
    }
    if (has_mods_root && has_schema) {
        err << "--mods-root cannot be combined with --schema\n" << kUsage;
        return 2;
    }
    if (has_mods_root) {
        return RunVerifyMods(args, out, err);
    }
    return match::expr::RunVerify(args, out, err);
}

}  // namespace match::cardschema
