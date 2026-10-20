#pragma once

#include <avatar/catalogue.hpp>
#include <avatar/loadout.hpp>
#include <database.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file economy_service.hpp
 * @brief The cash ledger, item ownership and the buy transaction.
 *
 * One service owns the whole economy: the append-only `cash_ledger`, the
 * cached `users.cash_balance` and the `item_ownership` rows. It is also the
 * database-backed `IOwnership` the loadout rules use, replacing
 * `StarterOwnership`. Accounts are keyed by `users.id` and looked up by
 * `username`; a guest has no `users` row, so every write fails and every read
 * is empty.
 *
 * Amounts are integers. Balance only changes in the same transaction as the
 * ledger row that caused it, and never goes below zero. Every write is
 * idempotent: `UNIQUE(user_id, source, ref)` makes earns and purchases once
 * per ref, and the ownership primary key makes a grant once per item. Time
 * always comes from the caller's `now`, never the system clock.
 */
namespace uni::economy {

/** @brief One append-only ledger entry, as the read API returns it. */
struct LedgerRow {
    std::int64_t amount;
    std::string source;
    std::string ref;
    std::int64_t created_at;
};

/** @brief The outcome of `EconomyService::Buy`. */
enum class BuyStatus {
    kOk,
    kUnknownItem,
    kNotAvailable,
    kAlreadyOwned,
    kInsufficient,
    kNoAccount,
};

/** @brief A buy's status and the balance after it (unchanged on failure). */
struct BuyResult {
    BuyStatus status;
    std::int64_t balance;
};

/** @brief One row of the most-bought ranking, with its public wishlist count. */
struct PopularItem {
    std::string item_id;
    int purchases;
    int wishlists;
};

class EconomyService : public uni::avatar::IOwnership {
  public:
    EconomyService(Database& db, const uni::avatar::Catalogue& catalogue);

    /** @brief The cached balance, or `std::nullopt` when there is no account. */
    std::optional<std::int64_t> Balance(const std::string& username) const;

    /** @brief The ids in `item_ownership` for `username`, in acquisition order. */
    std::vector<std::string> Owned(const std::string& username) const;

    /** @brief The last `limit` ledger rows for `username`, newest first. */
    std::vector<LedgerRow> Recent(const std::string& username,
                                  int limit = 50) const;

    /**
     * @brief Add `amount` under `(source, ref)`.
     * @return false when the ref is already recorded or there is no account.
     */
    bool Credit(const std::string& username, std::int64_t amount,
                const std::string& source, const std::string& ref,
                std::int64_t now);

    /**
     * @brief Buy an item or a swatch once, idempotent on `request_id`.
     *
     * One transaction checks the catalogue, ownership and balance, then writes
     * the ledger row, the ownership row and the balance together. A replay of
     * `request_id` returns the stored balance with `kOk`.
     */
    BuyResult Buy(const std::string& username, const std::string& item_id,
                  const std::string& request_id, std::int64_t now);

    /**
     * @brief Ownership without payment, once per item.
     * @return false when already owned or there is no account.
     */
    bool GrantItem(const std::string& username, const std::string& item_id,
                   const std::string& reason_key, std::int64_t now);

    /** @brief The catalogue ids `username` has wishlisted, oldest first. */
    std::vector<std::string> Wishlist(const std::string& username) const;

    /**
     * @brief Wishlist a catalogue item, idempotent per user and item.
     * @return false for an unknown account or an id outside the catalogue.
     */
    bool AddWishlist(const std::string& username, const std::string& item_id);

    /**
     * @brief Drop a wishlisted catalogue item, idempotent per user and item.
     * @return false for an unknown account or an id outside the catalogue.
     */
    bool RemoveWishlist(const std::string& username, const std::string& item_id);

    /**
     * @brief The most-bought items in the last `days`, wishlist counts included.
     *
     * `days` is clamped to 1..90 and `limit` to 1..50. Only `item_ownership`
     * rows with `source = 'purchase'` count, ordered by purchases descending
     * then by item id.
     */
    std::vector<PopularItem> Popular(int days, int limit) const;

    /** @brief The equipped item in `slot`, or `std::nullopt` for the default look. */
    std::optional<std::string> Cosmetic(const std::string& username,
                                        const std::string& slot) const;

    /**
     * @brief Equip an owned cosmetic item in `slot`, replacing any previous one.
     *
     * `slot` must be `card_face`, `card_back` or `playmat`, and `item_id` must
     * be a catalogue item in that same slot. The default look is no row.
     * @return false for an unknown account, slot, item, or an unowned item.
     */
    bool SetCosmetic(const std::string& username, const std::string& slot,
                     const std::string& item_id);

    /** @brief True for a starter item or an owned `item_ownership` row. */
    bool OwnsItem(const std::string& username,
                  const std::string& item_id) const override;

    /** @brief True for a starter swatch or an owned item_ownership row. */
    bool OwnsSwatch(const std::string& username,
                    const std::string& swatch_id) const override;

  private:
    Database& db_;
    const uni::avatar::Catalogue& catalogue_;
};

}  // namespace uni::economy
