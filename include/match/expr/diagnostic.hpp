#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace match::expr {

struct Diagnostic {
    std::string file;
    std::string pointer;
    std::string message;
    std::string suggestion;
};

std::string FormatDiagnostic(const Diagnostic& diagnostic);

std::string SuggestClosest(std::string_view word, const std::vector<std::string>& candidates);

}  // namespace match::expr
