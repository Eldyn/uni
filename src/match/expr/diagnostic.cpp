#include <match/expr/diagnostic.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace match::expr {

namespace {

int Levenshtein(std::string_view a, std::string_view b) {
    if (a == b) {
        return 0;
    }
    const std::size_t la = a.size();
    const std::size_t lb = b.size();
    if (la == 0) {
        return static_cast<int>(lb);
    }
    if (lb == 0) {
        return static_cast<int>(la);
    }

    std::vector<int> prev(lb + 1);
    std::vector<int> cur(lb + 1);
    for (std::size_t j = 0; j <= lb; ++j) {
        prev[j] = static_cast<int>(j);
    }
    for (std::size_t i = 1; i <= la; ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= lb; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[lb];
}

}  // namespace

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