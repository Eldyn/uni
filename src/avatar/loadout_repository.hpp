#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>

/**
 * @file loadout_repository.hpp
 * @brief Read and write the one active loadout per account.
 *
 * The loadout is plain JSON text in `users.avatar_loadout` (migration v13).
 * `NULL` means the account never saved one, and a stored value that no longer
 * parses reads as `nullopt` so a corrupt row degrades to the default look
 * instead of failing a join or a save.
 */

namespace uni::avatar {

/** @brief The account loadout row: `users.avatar_loadout`. */
class LoadoutRepository {
  public:
    /**
     * @brief The saved loadout for `username`.
     * @return The stored JSON, or `std::nullopt` when the row is `NULL`, the
     *         user does not exist, or the stored text is not valid JSON.
     */
    std::optional<nlohmann::json> Get(const std::string& username) const;

    /**
     * @brief Write `loadout` as the account's active loadout.
     * @return false when `username` has no row; true on a successful write.
     */
    bool Save(const std::string& username, const nlohmann::json& loadout);
};

}  // namespace uni::avatar
