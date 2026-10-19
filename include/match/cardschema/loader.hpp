#pragma once

#include <filesystem>
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

}  // namespace match::cardschema
