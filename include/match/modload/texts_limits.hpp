#pragma once

#include <cstddef>
#include <string_view>

/**
 * @file texts_limits.hpp
 * @brief Limits and syntax of the mod `texts/<locale>.json` files.
 *
 * One header holds every limit, so the loader, the validator, and the tests
 * read the same numbers.
 */

namespace match::modload {

namespace texts_limits {
inline constexpr std::size_t kMaxTextsFileBytes = 256 * 1024;
inline constexpr std::size_t kMaxTextValueBytes = 2 * 1024;
inline constexpr std::size_t kMaxTextKeys = 5000;
}  // namespace texts_limits

/** @brief The locale that the client makes at run time from `en`. */
inline constexpr std::string_view kPseudoLocale = "pseudo";

/**
 * @brief True for a locale tag: a language of 2 or 3 lowercase letters, then
 *        subtags of 2 to 8 letters or digits, split by `-`.
 *
 * The run-time locale `pseudo` is not a valid tag for a file.
 */
bool IsValidLocaleTag(std::string_view tag);

}  // namespace match::modload
