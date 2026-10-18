#include "avatar/loadout_repository.hpp"

#include "database.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

/**
 * @file loadout_repository.cpp
 * @brief `LoadoutRepository`: one JSON loadout per account.
 *
 * Both calls go to the shared `Database` singleton (`QueryOne` / `Exec`), the
 * same way `AuthService` reads accounts. `Get` never throws on bad stored text:
 * it parses with exceptions disabled and maps a discarded document to
 * `nullopt`.
 */

namespace uni::avatar {

namespace {

using nlohmann::json;

}  // namespace

std::optional<json> LoadoutRepository::Get(const std::string& username) const {
    auto result = Database::Get().QueryOne(
        "SELECT avatar_loadout FROM users WHERE username = ?;", {username});
    if (!result || !result->has_value()) return std::nullopt;

    const DbRow& row = result->value();
    const std::string stored = row.GetOr<std::string>("avatar_loadout", "");
    if (stored.empty()) return std::nullopt;

    json loadout = json::parse(stored, nullptr, false);
    if (loadout.is_discarded() || loadout.is_null()) return std::nullopt;
    return loadout;
}

bool LoadoutRepository::Save(const std::string& username,
                             const json& loadout) {
    auto user = Database::Get().QueryOne(
        "SELECT id FROM users WHERE username = ?;", {username});
    if (!user || !user->has_value()) return false;

    auto result = Database::Get().Exec(
        "UPDATE users SET avatar_loadout = ? WHERE username = ?;",
        {loadout.dump(), username});
    return result.has_value();
}

}  // namespace uni::avatar
