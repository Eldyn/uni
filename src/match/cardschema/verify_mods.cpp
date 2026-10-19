#include <match/cardschema/verify_mods.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <match/cardschema/catalog.hpp>
#include <match/cardschema/loader.hpp>
#include <match/cardschema/model.hpp>
#include <match/cardschema/validate.hpp>
#include <match/expr/diagnostic.hpp>

namespace match::cardschema {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr std::string_view kUsage =
    "usage: uni_mods verify --mods-root <dir> [--mod <id>] [--deck <identifier>]\n";

struct Options {
    std::string mods_root;
    std::string mod;
    std::string deck;
    bool has_mods_root = false;
    bool has_mod = false;
    bool has_deck = false;
};

// INFO: consumes the value that follows `flag`; `index` stops on it.
bool TakeValue(const std::vector<std::string>& args, std::size_t& index,
               std::string_view flag, std::string& value, std::string& error) {
    if (index + 1 >= args.size()) {
        error = std::string(flag) + " needs a value";
        return false;
    }
    value = args[++index];
    return true;
}

// INFO: returns the message for a bad command line, or an empty optional when
// the arguments are complete. A missing required flag has no message of its own.
std::optional<std::string> ParseArgs(const std::vector<std::string>& args, Options& options) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string& arg = args[index];
        std::string error;
        if (arg == "--mods-root") {
            if (options.has_mods_root) {
                return std::string("flag \"--mods-root\" given twice");
            }
            if (!TakeValue(args, index, "--mods-root", options.mods_root, error)) {
                return error;
            }
            options.has_mods_root = true;
        } else if (arg == "--mod") {
            if (options.has_mod) {
                return std::string("flag \"--mod\" given twice");
            }
            if (!TakeValue(args, index, "--mod", options.mod, error)) {
                return error;
            }
            options.has_mod = true;
        } else if (arg == "--deck") {
            if (options.has_deck) {
                return std::string("flag \"--deck\" given twice");
            }
            if (!TakeValue(args, index, "--deck", options.deck, error)) {
                return error;
            }
            options.has_deck = true;
        } else {
            return "unknown flag \"" + arg + "\"";
        }
    }
    if (!options.has_mods_root) {
        return std::string();
    }
    return std::nullopt;
}

// INFO: the manifest is not a card file, so the shared card helpers do not
// apply; the non-throwing parse overload never raises on malformed JSON.
json ReadManifest(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return json();
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return json::parse(buffer.str(), nullptr, false);
}

// INFO: the manifest `id` when it is a non-empty string; otherwise the folder
// name. The verifier follows the same fallback the brief states.
std::string ManifestId(const json& manifest, const std::string& folder_name) {
    if (manifest.is_object()) {
        const auto id = manifest.find("id");
        if (id != manifest.end() && id->is_string() && !id->get<std::string>().empty()) {
            return id->get<std::string>();
        }
    }
    return folder_name;
}

// INFO: each `settings` entry whose `type` is `int` contributes its `id` and
// `default`; a later mod overrides an earlier one of the same name.
void CollectSettings(const json& manifest, std::map<std::string, int64_t>& settings) {
    if (!manifest.is_object()) {
        return;
    }
    const auto entries = manifest.find("settings");
    if (entries == manifest.end() || !entries->is_array()) {
        return;
    }
    for (const json& entry : *entries) {
        if (!entry.is_object()) {
            continue;
        }
        const auto type = entry.find("type");
        if (type == entry.end() || !type->is_string() || type->get<std::string>() != "int") {
            continue;
        }
        const auto id = entry.find("id");
        const auto value = entry.find("default");
        if (id == entry.end() || !id->is_string()) {
            continue;
        }
        if (value == entry.end() || !value->is_number_integer()) {
            continue;
        }
        settings[id->get<std::string>()] = value->get<int64_t>();
    }
}

// INFO: with no selection every diagnostic is reported; with `--mod` only the
// diagnostics that carry the selected mod's file label are.
bool IsReported(const std::string& file, const std::vector<std::string>& selected_ids) {
    if (selected_ids.empty()) {
        return true;
    }
    for (const std::string& id : selected_ids) {
        const std::string prefix = "mods/" + id + "/";
        if (file.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<fs::path> Subfolders(const fs::path& root) {
    std::vector<fs::path> folders;
    std::error_code error;
    fs::directory_iterator iterator(root, error);
    if (error) {
        return folders;
    }
    const fs::directory_iterator end;
    while (iterator != end) {
        std::error_code entry_error;
        if (iterator->is_directory(entry_error) && !entry_error) {
            folders.push_back(iterator->path());
        }
        iterator.increment(error);
        if (error) {
            break;
        }
    }
    std::sort(folders.begin(), folders.end(), [](const fs::path& left, const fs::path& right) {
        return left.filename().generic_string() < right.filename().generic_string();
    });
    return folders;
}

int RunVerifyModsImpl(const std::vector<std::string>& args, std::ostream& out,
                      std::ostream& err) {
    if (args.empty() || args[0] != "verify") {
        err << kUsage;
        return 2;
    }
    Options options;
    if (const std::optional<std::string> error = ParseArgs(args, options)) {
        if (!error->empty()) {
            err << *error << "\n";
        }
        err << kUsage;
        return 2;
    }

    std::error_code status_error;
    const fs::file_status status = fs::status(options.mods_root, status_error);
    if (status_error || !fs::is_directory(status)) {
        err << "cannot read mods root \"" << options.mods_root << "\"\n" << kUsage;
        return 2;
    }

    std::vector<ModSchema> mods;
    std::vector<Diagnostic> diagnostics;
    std::map<std::string, int64_t> settings;
    for (const fs::path& folder : Subfolders(options.mods_root)) {
        const fs::path manifest_path = folder / "mod.json";
        std::error_code exists_error;
        if (!fs::is_regular_file(manifest_path, exists_error) || exists_error) {
            continue;  // a folder without mod.json is skipped
        }
        const json manifest = ReadManifest(manifest_path);
        const std::string mod_id = ManifestId(manifest, folder.filename().generic_string());
        CollectSettings(manifest, settings);

        LoadOutcome outcome = LoadModSchema(folder, mod_id);
        for (Diagnostic& diagnostic : outcome.diagnostics) {
            diagnostics.push_back(std::move(diagnostic));
        }
        mods.push_back(std::move(outcome.schema));
    }

    const std::vector<std::string> selected_ids =
        options.has_mod ? std::vector<std::string>{options.mod} : std::vector<std::string>{};

    const CatalogResult catalog = ExpandCards(mods);
    for (const Diagnostic& diagnostic : catalog.diagnostics) {
        diagnostics.push_back(diagnostic);
    }
    for (const Diagnostic& diagnostic : ValidateCatalog(mods)) {
        diagnostics.push_back(diagnostic);
    }

    std::vector<std::string> deck_lines;
    for (const ModSchema& mod : mods) {
        if (options.has_mod && mod.mod_id != options.mod) {
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

    std::vector<Diagnostic> reported;
    for (Diagnostic& diagnostic : diagnostics) {
        if (IsReported(diagnostic.file, selected_ids)) {
            reported.push_back(std::move(diagnostic));
        }
    }
    if (!reported.empty()) {
        for (const Diagnostic& diagnostic : reported) {
            err << match::expr::FormatDiagnostic(diagnostic) << "\n";
        }
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

}  // namespace match::cardschema
