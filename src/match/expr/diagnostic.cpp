#include <match/expr/diagnostic.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace match::expr {

namespace {

// INFO: RFC 6901: `~` escapes to `~0` and `/` escapes to `~1`.
std::string EscapePointerToken(std::string_view token) {
    std::string escaped;
    escaped.reserve(token.size());
    for (const char character : token) {
        if (character == '~') {
            escaped += "~0";
        } else if (character == '/') {
            escaped += "~1";
        } else {
            escaped.push_back(character);
        }
    }
    return escaped;
}

int Levenshtein(std::string_view left, std::string_view right) {
    if (left == right) {
        return 0;
    }
    const std::size_t left_length = left.size();
    const std::size_t right_length = right.size();
    if (left_length == 0) {
        return static_cast<int>(right_length);
    }
    if (right_length == 0) {
        return static_cast<int>(left_length);
    }

    std::vector<int> previous(right_length + 1);
    std::vector<int> current(right_length + 1);
    for (std::size_t right_index = 0; right_index <= right_length; ++right_index) {
        previous[right_index] = static_cast<int>(right_index);
    }
    for (std::size_t left_index = 1; left_index <= left_length; ++left_index) {
        current[0] = static_cast<int>(left_index);
        for (std::size_t right_index = 1; right_index <= right_length; ++right_index) {
            const int cost = (left[left_index - 1] == right[right_index - 1]) ? 0 : 1;
            current[right_index] =
                std::min({previous[right_index] + 1, current[right_index - 1] + 1,
                          previous[right_index - 1] + cost});
        }
        std::swap(previous, current);
    }
    return previous[right_length];
}

}  // namespace

void AppendPointerTokenTo(std::string& pointer, std::string_view token) {
    if (pointer.empty()) {
        pointer = "/";
    } else if (pointer.back() != '/') {
        pointer.push_back('/');
    }
    pointer += EscapePointerToken(token);
}

std::string AppendPointerToken(std::string_view pointer, std::string_view token) {
    std::string result(pointer);
    AppendPointerTokenTo(result, token);
    return result;
}

std::string FormatDiagnostic(const Diagnostic& diagnostic) {
    std::string result = diagnostic.file + "#" + diagnostic.pointer + ": " + diagnostic.message;
    if (!diagnostic.suggestion.empty()) {
        result += " (did you mean \"" + diagnostic.suggestion + "\")";
    }
    return result;
}

std::string SuggestClosest(std::string_view word, const std::vector<std::string>& candidates) {
    if (candidates.empty()) {
        return "";
    }
    const int threshold = std::max(1, static_cast<int>(word.size()) / 3);

    std::string best;
    int best_distance = 0;
    bool have_best = false;
    for (const std::string& candidate : candidates) {
        const int distance = Levenshtein(word, candidate);
        if (!have_best || distance < best_distance) {
            best = candidate;
            best_distance = distance;
            have_best = true;
        }
    }
    if (best_distance <= threshold) {
        return best;
    }
    return "";
}

}  // namespace match::expr
