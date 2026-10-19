#include "economy/economy_service.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file economy_service.cpp
 * @brief Implements the ledger service: writes run in one transaction and each
 *        is idempotent through the ledger and ownership keys.
 */

namespace uni::economy {

namespace {

using uni::avatar::Item;

/** @brief One account's primary key and cached balance. */
struct UserRow {
    int id = 0;
    std::int64_t balance = 0;
};

/** @brief Look `username` up; `std::nullopt` when there is no account row. */
std::optional<UserRow> FindUser(Database& db, const std::string& username) {
    auto row = db.QueryOne(
        "SELECT id, cash_balance FROM users WHERE username = ?;", {username});
    if (!row.has_value() || !row->has_value()) return std::nullopt;
    return UserRow{row.value()->Get<int>("id"),
                   row.value()->Get<int>("cash_balance")};
}

/** @brief True when `username` already has an `item_ownership` row. */
bool HasOwnershipRow(Database& db, int user_id, const std::string& item_id) {
    auto row = db.QueryOne(
        "SELECT 1 AS present FROM item_ownership WHERE user_id = ? AND "
        "item_id = ?;",
        {user_id, item_id});
    return row.has_value() && row->has_value();
}

/** @brief True when the ledger already holds `(user_id, source, ref)`. */
bool HasLedgerRow(Database& db, int user_id, const std::string& source,
                  const std::string& ref) {
    auto row = db.QueryOne(
        "SELECT 1 AS present FROM cash_ledger WHERE user_id = ? AND source = ? "
        "AND ref = ?;",
        {user_id, source, ref});
    return row.has_value() && row->has_value();
}

}  // namespace

EconomyService::EconomyService(Database& db,
                               const uni::avatar::Catalogue& catalogue)
    : db_(db), catalogue_(catalogue) {}

std::optional<std::int64_t> EconomyService::Balance(
    const std::string& username) const {
    const std::optional<UserRow> user = FindUser(db_, username);
    if (!user.has_value()) return std::nullopt;
    return user->balance;
}

std::vector<std::string> EconomyService::Owned(
    const std::string& username) const {
    std::vector<std::string> ids;
    auto rows = db_.Query(
        "SELECT item_id FROM item_ownership WHERE user_id = "
        "(SELECT id FROM users WHERE username = ?) "
        "ORDER BY acquired_at, item_id;",
        {username});
    if (!rows.has_value()) return ids;
    for (const DbRow& row : *rows) ids.push_back(row.Get<std::string>("item_id"));
    return ids;
}

std::vector<LedgerRow> EconomyService::Recent(const std::string& username,
                                              int limit) const {
    std::vector<LedgerRow> entries;
    auto rows = db_.Query(
        "SELECT amount, source, ref, created_at FROM cash_ledger WHERE user_id = "
        "(SELECT id FROM users WHERE username = ?) ORDER BY id DESC LIMIT ?;",
        {username, limit});
    if (!rows.has_value()) return entries;
    for (const DbRow& row : *rows) {
        entries.push_back(LedgerRow{row.Get<int>("amount"),
                                    row.Get<std::string>("source"),
                                    row.Get<std::string>("ref"),
                                    row.Get<int>("created_at")});
    }
    return entries;
}

bool EconomyService::Credit(const std::string& username, std::int64_t amount,
                            const std::string& source, const std::string& ref,
                            std::int64_t now) {
    TransactionGuard tx(db_);
    if (!tx.Ok()) return false;

    const std::optional<UserRow> user = FindUser(db_, username);
    if (!user.has_value()) return false;

    auto insert = db_.Exec(
        "INSERT INTO cash_ledger (user_id, amount, source, ref, created_at) "
        "VALUES (?, ?, ?, ?, ?);",
        {user->id, static_cast<int>(amount), source, ref,
         static_cast<int>(now)});
    if (!insert.has_value()) return false;  // the ref is already recorded

    auto update = db_.Exec(
        "UPDATE users SET cash_balance = cash_balance + ? WHERE id = ?;",
        {static_cast<int>(amount), user->id});
    if (!update.has_value()) return false;
    return tx.Commit().has_value();
}

BuyResult EconomyService::Buy(const std::string& username,
                              const std::string& item_id,
                              const std::string& request_id, std::int64_t now) {
    TransactionGuard tx(db_);
    if (!tx.Ok()) return BuyResult{BuyStatus::kNoAccount, 0};

    const std::optional<UserRow> user = FindUser(db_, username);
    if (!user.has_value()) return BuyResult{BuyStatus::kNoAccount, 0};

    /* A replay of a completed request is not a second purchase: return the
     * stored balance before ownership would report `kAlreadyOwned`. */
    if (HasLedgerRow(db_, user->id, "purchase", request_id)) {
        return BuyResult{BuyStatus::kOk, user->balance};
    }

    const Item* item = catalogue_.FindItem(item_id);
    const bool is_item = item != nullptr;
    if (!is_item && !catalogue_.HasSwatch(item_id)) {
        return BuyResult{BuyStatus::kUnknownItem, user->balance};
    }
    if ((is_item && item->starter)
        || (!is_item && catalogue_.IsStarterSwatch(item_id))
        || HasOwnershipRow(db_, user->id, item_id)) {
        return BuyResult{BuyStatus::kAlreadyOwned, user->balance};
    }
    if (!catalogue_.IsBuyable(item_id, now)) {
        return BuyResult{BuyStatus::kNotAvailable, user->balance};
    }

    const std::optional<int> price = catalogue_.PriceOf(item_id);
    if (!price.has_value()) {
        return BuyResult{BuyStatus::kNotAvailable, user->balance};
    }
    if (user->balance < *price) {
        return BuyResult{BuyStatus::kInsufficient, user->balance};
    }

    auto ledger = db_.Exec(
        "INSERT INTO cash_ledger (user_id, amount, source, ref, created_at) "
        "VALUES (?, ?, 'purchase', ?, ?);",
        {user->id, static_cast<int>(-*price), request_id,
         static_cast<int>(now)});
    if (!ledger.has_value()) {
        /* A concurrent replay won the race; the stored balance stands. */
        const std::optional<UserRow> fresh = FindUser(db_, username);
        return BuyResult{BuyStatus::kOk,
                         fresh.has_value() ? fresh->balance : user->balance};
    }

    auto owned = db_.Exec(
        "INSERT INTO item_ownership (user_id, item_id, source, acquired_at) "
        "VALUES (?, ?, 'purchase', ?);",
        {user->id, item_id, static_cast<int>(now)});
    if (!owned.has_value()) {
        return BuyResult{BuyStatus::kAlreadyOwned, user->balance};
    }

    auto update = db_.Exec(
        "UPDATE users SET cash_balance = cash_balance - ? WHERE id = ?;",
        {static_cast<int>(*price), user->id});
    if (!update.has_value()) {
        return BuyResult{BuyStatus::kNoAccount, user->balance};
    }

    if (!tx.Commit().has_value()) {
        return BuyResult{BuyStatus::kNoAccount, user->balance};
    }
    return BuyResult{BuyStatus::kOk, user->balance - *price};
}

bool EconomyService::GrantItem(const std::string& username,
                               const std::string& item_id,
                               const std::string& /*reason_key*/,
                               std::int64_t now) {
    TransactionGuard tx(db_);
    if (!tx.Ok()) return false;

    const std::optional<UserRow> user = FindUser(db_, username);
    if (!user.has_value()) return false;

    auto insert = db_.Exec(
        "INSERT INTO item_ownership (user_id, item_id, source, acquired_at) "
        "VALUES (?, ?, 'grant', ?);",
        {user->id, item_id, static_cast<int>(now)});
    if (!insert.has_value()) return false;  // already owned
    return tx.Commit().has_value();
}

bool EconomyService::OwnsItem(const std::string& username,
                              const std::string& item_id) const {
    if (catalogue_.IsStarter(item_id)) return true;
    auto row = db_.QueryOne(
        "SELECT 1 AS present FROM item_ownership WHERE user_id = "
        "(SELECT id FROM users WHERE username = ?) AND item_id = ?;",
        {username, item_id});
    return row.has_value() && row->has_value();
}

bool EconomyService::OwnsSwatch(const std::string& username,
                                const std::string& swatch_id) const {
    if (catalogue_.IsStarterSwatch(swatch_id)) return true;
    auto row = db_.QueryOne(
        "SELECT 1 AS present FROM item_ownership WHERE user_id = "
        "(SELECT id FROM users WHERE username = ?) AND item_id = ?;",
        {username, swatch_id});
    return row.has_value() && row->has_value();
}

}  // namespace uni::economy
