#include <match/expr/diagnostic.hpp>

#include <algorithm>
#include <cstddef>
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
    // INFO: the root marker "/" is also the separator of the first token, so a
    // root and an empty key must be told apart by the marker, not the last
    // character; an empty key keeps its own separator.
    if (pointer.empty() || pointer == kRootPointer) {
        pointer = std::string(kRootPointer);
        if (token.empty()) {
            pointer.push_back('/');
            return;
        }
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
    // INFO: a word past the cap cannot be a plausible typo of a short name, and
    // a full distance against it would dominate the search, so answer at once.
    constexpr std::size_t kMaximumSuggestedWordLength = 256;
    if (word.size() > kMaximumSuggestedWordLength) {
        return "";
    }
    const int threshold = std::max(1, static_cast<int>(word.size()) / 3);
    const int word_length = static_cast<int>(word.size());

    std::string best;
    int best_distance = 0;
    bool have_best = false;
    for (const std::string& candidate : candidates) {
        // INFO: a distance is at least the difference of the two lengths, so a
        // candidate outside the threshold can never win and needs no distance.
        const int candidate_length = static_cast<int>(candidate.size());
        const int length_difference = word_length > candidate_length
                                          ? word_length - candidate_length
                                          : candidate_length - word_length;
        if (length_difference > threshold) {
            continue;
        }
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
