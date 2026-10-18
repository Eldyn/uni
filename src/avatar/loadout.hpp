#pragma once

#include <avatar/catalogue.hpp>

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>

/**
 * @file loadout.hpp
 * @brief Server-side loadout validation and the guest default look.
 *
 * A save is atomic: `ValidateLoadout` returns the first failure, and the
 * controller rejects the whole save on any failure. Ownership sits behind
 * `IOwnership` so plan 1D can ship starters-only while a later plan adds an
 * ownership table without touching the rules.
 */

namespace uni::avatar {

/** @brief A rejected loadout: an error `code` and a human-readable `detail`. */
struct LoadoutError {
    std::string code;
    std::string detail;
};

/** @brief Who owns which items and swatches, per account. */
class IOwnership {
  public:
    virtual ~IOwnership() = default;

    /** @brief True when `username` may equip `item_id`. */
    virtual bool OwnsItem(const std::string& username,
                          const std::string& item_id) const = 0;

    /** @brief True when `username` may use `swatch_id`. */
    virtual bool OwnsSwatch(const std::string& username,
                            const std::string& swatch_id) const = 0;
};

/** @brief Plan 1D ownership: every account owns the starters and nothing else. */
class StarterOwnership : public IOwnership {
  public:
    explicit StarterOwnership(const Catalogue& catalogue)
        : catalogue_(catalogue) {}

    bool OwnsItem(const std::string& username,
                  const std::string& item_id) const override;
    bool OwnsSwatch(const std::string& username,
                    const std::string& swatch_id) const override;

  private:
    const Catalogue& catalogue_;
};

/**
 * @brief Check a whole loadout against the catalogue and ownership.
 * @return The first failure, or `std::nullopt` when the save is acceptable.
 */
std::optional<LoadoutError> ValidateLoadout(const nlohmann::json& loadout,
                                            const Catalogue& catalogue,
                                            const IOwnership& ownership,
                                            const std::string& username);

/**
 * @brief The stable default look for a guest session.
 *
 * The default shape and eyes/mouth items; the skin is a basic-palette swatch
 * picked from `seed`, so one session keeps one colour and different sessions
 * differ.
 */
nlohmann::json GuestLoadout(const Catalogue& catalogue, uint32_t seed);

}  // namespace uni::avatar
