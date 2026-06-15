#pragma once
#include "logger.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <variant>
#include <vector>
#include <optional>
#include <unordered_map>
#include <result.hpp>

/**
 * @file database.hpp
 * @brief Lightweight, type-safe abstraction around the SQLite3 library.
 * * Provides Singleton access to the database, with a query system based
 * on `std::variant` to prevent SQL Injection, and a RAII construct for transactions.
 */

/**
 * @typedef DbValue
 * @brief Variant that encapsulates the base data types allowed in the SQLite database.
 * @tag DB-TYP-001
 */
using DbValue = std::variant<int, double, std::string, std::nullptr_t>;

/**
 * @class DbRow
 * @brief Represents a single row returned by a SQL query.
 * Stores the data using the column keys as indices of the internal map.
 * @tag DB-ROW-001
 */
class DbRow {
public:
    /**
     * @brief Sets a value in the row for a specific column.
     * @param col The name of the column.
     * @param val The typed value.
     * @tag DB-ROW-MTH-001
     */
    void Set(const std::string& col, DbValue val);

    /**
     * @brief Extracts a value, forcing its expected type (throws an exception if the column is missing).
     * @tparam T Expected data type (e.g. int, std::string).
     * @param col Name of the column.
     * @return T Extracted value.
     * @throws std::runtime_error if the column does not exist.
     * @tag DB-ROW-MTH-002
     */
    template<typename T>
    T Get(const std::string& col) const {
        if (!Has(col)) {
            throw std::runtime_error("Column not found: " + col);
        }
        return std::get<T>(data_.at(col));
    };

    /**
     * @brief Safely extracts a value, providing a fallback if it is absent or NULL.
     * @tparam T Expected data type.
     * @param col Name of the column.
     * @param fallback Default value in case of a NULL/missing column.
     * @return T The extracted value or the fallback.
     * @tag DB-ROW-MTH-003
     */
    template<typename T>
    T GetOr(const std::string& col, T fallback) const {
        if (!Has(col) || std::holds_alternative<std::nullptr_t>(data_.at(col))) {
            return fallback;
        }
        return std::get<T>(data_.at(col));
    };

    /**
     * @brief Checks whether the row possesses the specified column.
     * @param col Name of the column.
     * @return true if present, false otherwise.
     * @tag DB-ROW-MTH-004
     */
    bool Has(const std::string& col) const;

private:
    std::unordered_map<std::string, DbValue> data_;
};

/**
 * @class Database
 * @brief Singleton pattern that encapsulates the unique, shared connection to the SQLite DB.
 * * All the query methods are "No-Throw" and return monadic results (Result) for
 * proper error handling without C++ exceptions.
 * @tag DB-CLS-001
 */
class Database {
public:
    /**
     * @brief Retrieves the single active instance of the Database.
     * @return Database& Reference to the Singleton.
     * @tag DB-MTH-001
     */
    static Database& Get();

    /**
     * @brief Opens or creates the SQLite database file at the provided path.
     * @param path Path on disk.
     * @return VoidResult Result of the open attempt.
     * @tag DB-MTH-002
     */
    VoidResult Open(std::string_view path);

    /**
     * @brief Applies direct SQL commands to the database (used mainly for Schema files).
     * @param sql Raw SQL query string.
     * @return VoidResult.
     * @tag DB-MTH-003
     */
    VoidResult ApplySchema(const char* sql);

    /**
     * @brief Explicitly closes the active SQLite connection.
     * @tag DB-MTH-004
     */
    void       Close();

    /**
     * @brief Checks whether the DB is currently ready for use.
     * @return true if open.
     * @tag DB-MTH-005
     */
    bool       IsOpen() const;

    /**
     * @brief Executes a query that does not return rows (e.g. INSERT, UPDATE, DELETE).
     * @param sql The SQL string (which may contain `?` placeholders).
     * @param params The optional typed parameters to bind to the query.
     * @return VoidResult Error in case of query failure or constraint violation.
     * @tag DB-MTH-006
     */
    VoidResult                          Exec     (const char* sql, std::vector<DbValue> params = {});

    /**
     * @brief Executes a query that returns a list of results (e.g. generic SELECTs).
     * @param sql Parametrized query string.
     * @param params List of parameters associated with the placeholders.
     * @return Result<std::vector<DbRow>> The vector of resulting rows.
     * @tag DB-MTH-007
     */
    Result<std::vector<DbRow>>          Query    (const char* sql, std::vector<DbValue> params = {});

    /**
     * @brief Executes a query aimed at returning at most 1 record (e.g. SELECT with LIMIT 1).
     * @param sql Parametrized query string.
     * @param params List of parameters.
     * @return Result<std::optional<DbRow>> Result containing the record or empty (std::nullopt).
     * @tag DB-MTH-008
     */
    Result<std::optional<DbRow>>        QueryOne (const char* sql, std::vector<DbValue> params = {});

private:
    Database()  = default;
    ~Database() { Close(); }
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* db_ = nullptr;

    sqlite3_stmt* Prepare(const char* sql, const std::vector<DbValue>& params);
    DbRow         ReadRow(sqlite3_stmt* stmt);
};

/**
 * @class TransactionGuard
 * @brief Implements the safe execution of database transactions through the RAII paradigm.
 * * Ensures that any blocking failure within its C++ scope
 * automatically triggers a ROLLBACK (if `Commit()` was not called explicitly),
 * avoiding inconsistencies in the database.
 * @tag DB-TX-001
 */
class TransactionGuard {
public:
    /**
     * @brief Begins the transaction by executing "BEGIN TRANSACTION".
     * @param db The database to operate on.
     * @tag DB-TX-MTH-001
     */
    explicit TransactionGuard(Database& db) : db_(db), committed_(false) {
        auto res = db_.Exec("BEGIN TRANSACTION;");
        if (!res) {
            throw std::runtime_error("Failed to begin transaction: " + res.error().message);
        }
    }

    /**
     * @brief Destructor: Executes "ROLLBACK" if the transaction was not completed normally.
     * @tag DB-TX-MTH-002
     */
    ~TransactionGuard() {
        if (!committed_) {
            auto _ = db_.Exec("ROLLBACK;");
            Logger::Error("[DB] Transaction failed, rolling back...");
        }
    }

    /**
     * @brief Marks the transaction for a successful completion (executes "COMMIT").
     * If called, it cancels the rollback effect upon destruction of the Guard.
     * @tag DB-TX-MTH-003
     */
    void Commit() {
        if (!committed_) {
            auto res = db_.Exec("COMMIT;");
            if (!res) {
                throw std::runtime_error("Failed to COMMIT transaction: " + res.error().message);
            }
            committed_ = true;
        }
    }

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

private:
    Database& db_;
    bool committed_;
};
