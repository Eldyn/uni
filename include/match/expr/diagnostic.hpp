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

// INFO: appends one JSON pointer token to `pointer` in place: the first token
// starts the pointer at "/" and later ones are separated by "/". The token is
// escaped for RFC 6901 (`~` becomes `~0`, `/` becomes `~1`), so a key that
// holds either character still yields a readable pointer.
void AppendPointerTokenTo(std::string& pointer, std::string_view token);

// INFO: the value-returning form of AppendPointerTokenTo, for a caller that
// keeps the parent pointer as a string_view.
std::string AppendPointerToken(std::string_view pointer, std::string_view token);

}  // namespace match::expr
