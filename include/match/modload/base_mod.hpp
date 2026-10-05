#pragma once

#include <string_view>

/**
 * @file base_mod.hpp
 * @brief The id of the base mod.
 */

namespace match::modload {

inline constexpr std::string_view kBaseModId = "uni";

/** @brief True when `mod_id` names the base mod. */
inline bool IsBaseModId(std::string_view mod_id) {
    return mod_id == kBaseModId;
}

}  // namespace match::modload
