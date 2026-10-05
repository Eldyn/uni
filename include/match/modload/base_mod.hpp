#pragma once

#include <string>
#include <string_view>

/**
 * @file base_mod.hpp
 * @brief The base mod id pair: `uni` beside the legacy `vanilla`.
 *
 * INFO: Expand step of the vanilla to uni rename. Every check for the base
 *       mod goes through this header, so the contract step changes one place.
 */

namespace match::modload {

inline constexpr std::string_view kBaseModId = "uni";
inline constexpr std::string_view kLegacyBaseModId = "vanilla";

/** @brief True when `mod_id` names the base mod under either id. */
inline bool IsBaseModId(std::string_view mod_id) {
    return mod_id == kBaseModId || mod_id == kLegacyBaseModId;
}

/** @brief True when both ids name the same mod, the base mod under any id. */
inline bool IsSameModId(std::string_view left, std::string_view right) {
    return left == right || (IsBaseModId(left) && IsBaseModId(right));
}

/**
 * @brief Rewrites a `uni:` kind id to its `vanilla:` form.
 *
 * Other kind ids pass through unchanged.
 */
inline std::string ToLegacyBaseKindId(const std::string& kind_id) {
    const std::string uni_prefix = std::string(kBaseModId) + ":";
    if (kind_id.compare(0, uni_prefix.size(), uni_prefix) != 0) {
        return kind_id;
    }
    return std::string(kLegacyBaseModId) + ":" +
           kind_id.substr(uni_prefix.size());
}

}  // namespace match::modload
