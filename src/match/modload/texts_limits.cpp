#include "match/modload/texts_limits.hpp"

#include <cctype>
#include <cstddef>

namespace match::modload {

namespace {

constexpr std::size_t kMinLanguageLength = 2;
constexpr std::size_t kMaxLanguageLength = 3;
constexpr std::size_t kMinSubtagLength = 2;
constexpr std::size_t kMaxSubtagLength = 8;

bool IsLowerLetter(char c) { return c >= 'a' && c <= 'z'; }

bool IsAlphanumeric(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

}  // namespace

bool IsValidLocaleTag(std::string_view tag) {
    const std::size_t language_end = tag.find('-');
    const std::string_view language = tag.substr(0, language_end);
    if (language.size() < kMinLanguageLength
        || language.size() > kMaxLanguageLength) {
        return false;
    }
    for (const char c : language) {
        if (!IsLowerLetter(c)) return false;
    }
    std::string_view rest = language_end == std::string_view::npos
                                ? std::string_view()
                                : tag.substr(language_end + 1);
    bool has_rest = language_end != std::string_view::npos;
    while (has_rest) {
        const std::size_t split = rest.find('-');
        const std::string_view subtag = rest.substr(0, split);
        if (subtag.size() < kMinSubtagLength
            || subtag.size() > kMaxSubtagLength) {
            return false;
        }
        for (const char c : subtag) {
            if (!IsAlphanumeric(c)) return false;
        }
        has_rest = split != std::string_view::npos;
        if (has_rest) rest = rest.substr(split + 1);
    }
    return true;
}

}  // namespace match::modload
