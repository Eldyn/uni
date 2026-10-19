#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <match/cardschema/model.hpp>

namespace match::cardschema {

struct LoadOutcome {
    ModSchema schema;
    std::vector<Diagnostic> diagnostics;   // errors only; warnings use severity prefix "warning: "
    bool ok() const { return diagnostics.empty(); }
};

LoadOutcome LoadModSchema(const std::filesystem::path& mod_directory, const std::string& mod_id);

namespace internal {

// INFO: reads one schema file with the size cap, reports every JSON-level problem
// through `diagnostics`, and returns the document only when it is a usable object.
// The kinds, decks and properties loaders share it unchanged.
std::optional<json> ReadCardDocument(const std::filesystem::path& path,
                                     const std::string& label,
                                     std::vector<Diagnostic>& diagnostics);

}  // namespace internal

}  // namespace match::cardschema
