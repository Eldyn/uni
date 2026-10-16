#pragma once
#include "logger.hpp"
#include <common/db_worker.hpp>
#include <sqlite3.h>
#include <atomic>
#include <cstdint>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
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
 */
using DbValue = std::variant<int, double, std::string, std::nullptr_t>;

/**
 * @class DbRow
 * @brief Represents a single row returned by a SQL query.
 * Stores the data using the column keys as indices of the internal map.
 */
class DbRow {
public:
    /**
     * @brief Sets a value in the row for a specific column.
     * @param col The name of the column.
     * @param val The typed value.
     */
    void Set(const std::string& col, DbValue val);

    /**
     * @brief Extracts a value, forcing its expected type (throws an exception if the column is missing).
     * @tparam T Expected data type (e.g. int, std::string).
     * @param col Name of the column.
     * @return T Extracted value.
     * @throws std::runtime_error if the column does not exist.
     */
    template<typename T>
    T Get(const std::string& col) const {
        if (!Has(col)) {
            throw std::runtime_error("Column not found: " + col);
        }
        return std::get<T>(data_.at(col));
    }

    /**
     * @brief Safely extracts a value, providing a fallback if it is absent or NULL.
     * @tparam T Expected data type.
     * @param col Name of the column.
     * @param fallback Default value in case of a NULL/missing column.
     * @return T The extracted value or the fallback.
     */
    template<typename T>
    T GetOr(const std::string& col, T fallback) const {
        if (!Has(col) || std::holds_alternative<std::nullptr_t>(data_.at(col))) {
            return fallback;
        }
        return std::get<T>(data_.at(col));
    }

    /**
     * @brief Checks whether the row possesses the specified column.
     * @param col Name of the column.
     * @return true if present, false otherwise.
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
 */
class Database {
public:
    /**
     * @brief Retrieves the single active instance of the Database.
     * @return Database& Reference to the Singleton.
     */
    static Database& Get();

    /**
     * @brief Opens or creates the SQLite database file at the provided path.
     * @param path Path on disk.
     * @return VoidResult Result of the open attempt.
     */
    VoidResult Open(std::string_view path);

    /**
     * @brief Applies direct SQL commands to the database (used mainly for Schema files).
     * @param sql Raw SQL query string.
     * @return VoidResult.
     */
    VoidResult ApplySchema(const char* sql);

    /**
     * @brief Explicitly closes the active SQLite connection.
     */
    void       Close();

    /**
     * @brief Checks whether the DB is currently ready for use.
     * @return true if open.
     */
    bool       IsOpen() const;

    /**
     * @brief Executes a query that does not return rows (e.g. INSERT, UPDATE, DELETE).
     * @param sql The SQL string (which may contain `?` placeholders).
     * @param params The optional typed parameters to bind to the query.
     * @return VoidResult Error in case of query failure or constraint violation.
     */
    VoidResult                          Exec(const char* sql,
                                               std::vector<DbValue> params = {});

    /**
     * @brief Executes a query that returns a list of results (e.g. generic SELECTs).
     * @param sql Parametrized query string.
     * @param params List of parameters associated with the placeholders.
     * @return Result<std::vector<DbRow>> The vector of resulting rows.
     */
    Result<std::vector<DbRow>>          Query(const char* sql,
                                               std::vector<DbValue> params = {});

    /**
     * @brief Executes a query aimed at returning at most 1 record (e.g. SELECT with LIMIT 1).
     * @param sql Parametrized query string.
     * @param params List of parameters.
     * @return Result<std::optional<DbRow>> Result containing the record or empty (std::nullopt).
     */
    Result<std::optional<DbRow>>        QueryOne(const char* sql,
                                                  std::vector<DbValue> params = {});

    /**
     * @brief Async `Exec`: the statement runs on the DB worker thread.
     * @param sql Owned copy — safe to pass temporaries; the caller must not
     * rely on the pointed-to text outliving the call (unlike `Exec`).
     */
    std::future<VoidResult> ExecAsync(std::string sql,
                                      std::vector<DbValue> params = {});

    /**
     * @brief Async `Query`: runs on the DB worker thread.
     */
    std::future<Result<std::vector<DbRow>>> QueryAsync(std::string sql,
                                                       std::vector<DbValue> params = {});

    /**
     * @brief Async `QueryOne`: runs on the DB worker thread.
     */
    std::future<Result<std::optional<DbRow>>> QueryOneAsync(std::string sql,
                                                            std::vector<DbValue> params = {});

    /**
     * @brief Runs `fn(db)` wholly on the DB worker thread inside a
     * `TransactionGuard` (BEGIN … COMMIT/ROLLBACK all on the worker).
     *
     * `fn` must return `Result<T>`/`VoidResult`; a falsy return rolls the
     * transaction back. Never block on the returned future from inside
     * another worker task — nested calls from the worker thread run inline.
     *
     * @return Future holding `fn`'s value, or the BEGIN/COMMIT error.
     */
    template <typename Fn>
    auto RunInTxn(Fn&& fn) -> std::future<std::invoke_result_t<Fn, Database&>>;

    /** @brief True when called on the DB worker thread itself. */
    bool OnWorkerThread() const;

    /**
     * @brief Cache hits since process start (test/bench metric).
     * Incremented on the worker thread each time a cached prepared
     * statement is reused; read from any thread.
     */
    std::uint64_t StmtCacheHitsForTests() const;

    /**
     * @brief Applies all pending schema migrations in version order.
     * @return VoidResult Error if any migration or version update fails.
     */
    VoidResult RunMigrations();

private:
    Database();
    ~Database();
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    // Direct (worker-thread-only) implementations. Every one holds db_mutex_
    // for its whole body, so Close() can never interleave with an in-flight
    // sqlite3_* call. Never call these from the loop thread.
    VoidResult                   ExecDirect(const char* sql, const std::vector<DbValue>& params);
    Result<std::vector<DbRow>>   QueryDirect(const char* sql, const std::vector<DbValue>& params);
    Result<std::optional<DbRow>> QueryOneDirect(const char* sql, const std::vector<DbValue>& params);
    VoidResult                   ApplySchemaDirect(const char* sql);

    sqlite3* db_ = nullptr;
    // Recursive: Close() from inside a worker task must not self-deadlock.
    mutable std::recursive_mutex db_mutex_;
    DbWorker worker_;

    int        GetSchemaVersion();
    VoidResult SetSchemaVersion(int version);
    sqlite3_stmt* Prepare(const char* sql, const std::vector<DbValue>& params);
    // Finalizes the stale cached stmt for `sql` (if any), prepares + binds a
    // fresh one, and stores it back. Used for a one-shot SQLITE_SCHEMA retry
    // when DDL executed through another cached stmt invalidated this one.
    sqlite3_stmt* ReprepareAfterSchemaChange(const char* sql,
                                             const std::vector<DbValue>& params);
    DbRow         ReadRow(sqlite3_stmt* stmt);
    // Finalizes every cached stmt and empties the cache. Caller must hold
    // db_mutex_ (Close/ApplySchemaDirect do); the map itself is only touched
    // on the worker thread via the *Direct methods.
    void ClearStmtCache();

    // Worker-local prepared-statement cache: SQL text -> prepared stmt.
    // Only ever touched on the DbWorker thread (all *Direct methods run
    // there; db_mutex_ additionally serializes against Close()).
    std::unordered_map<std::string, sqlite3_stmt*> stmt_cache_;
    std::atomic<std::uint64_t> stmt_cache_hits_{0};
};

/**
 * @class TransactionGuard
 * @brief Implements the safe execution of database transactions through the RAII paradigm.
 * * Ensures that any blocking failure within its C++ scope
 * automatically triggers a ROLLBACK (if `Commit()` was not called explicitly),
 * avoiding inconsistencies in the database. Construction never throws; callers
 * must check `Ok()` before using the guard.
 */
class TransactionGuard {
public:
    /**
     * @brief Begins the transaction by executing "BEGIN TRANSACTION".
     * Never throws; check `Ok()` to determine whether the transaction began.
     * @param db The database to operate on.
     */
    explicit TransactionGuard(Database& db) : db_(db), committed_(false) {
        auto res = db_.Exec("BEGIN TRANSACTION;");
        if (!res) {
            error_ = res.error();
        }
    }

    /**
     * @brief Destructor: Executes "ROLLBACK" if a started transaction was not committed.
     */
    ~TransactionGuard() {
        if (Ok() && !committed_) {
            auto _ = db_.Exec("ROLLBACK;");
            Logger::Error("[DB] Transaction failed, rolling back...");
        }
    }

    /**
     * @brief Checks whether the transaction was successfully started.
     * @return true if "BEGIN TRANSACTION" succeeded.
     */
    bool Ok() const { return !error_.has_value(); }

    /**
     * @brief Returns the error captured during construction or Commit().
     * Only valid to call when `Ok()` (or the last `Commit()`) returned false.
     * @return const Error& The stored error.
     */
    const Error& GetError() const { return *error_; }

    /**
     * @brief Marks the transaction for a successful completion (executes "COMMIT").
     * If called, it cancels the rollback effect upon destruction of the Guard.
     * @return VoidResult Error if the transaction never began or COMMIT failed.
     */
    VoidResult Commit() {
        if (!Ok()) {
            return std::unexpected(*error_);
        }
        if (!committed_) {
            auto res = db_.Exec("COMMIT;");
            if (!res) {
                error_ = res.error();
                return std::unexpected(*error_);
            }
            committed_ = true;
        }
        return {};
    }

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

private:
    Database& db_;
    bool committed_;
    std::optional<Error> error_;
};

/**
 * @brief `Database::RunInTxn` implementation, defined here (after
 * `TransactionGuard`) so the guard is complete at the template definition
 * point — GCC checks non-dependent names eagerly (`-Wtemplate-body`).
 */
template <typename Fn>
auto Database::RunInTxn(Fn&& fn) -> std::future<std::invoke_result_t<Fn, Database&>> {
    using Out = std::invoke_result_t<Fn, Database&>;
    auto run = [this, fn = std::forward<Fn>(fn)]() mutable -> Out {
        TransactionGuard tx(*this);
        if (!tx.Ok()) return Out{std::unexpected(tx.GetError())};
        Out r = fn(*this);
        if (!r) return r;
        if (auto c = tx.Commit(); !c) return Out{std::unexpected(c.error())};
        return r;
    };
    if (OnWorkerThread()) {
        // Nested call from inside a worker task: run inline so the
        // closure still executes wholly on the worker (enqueue + block
        // here would deadlock on our own queue).
        std::promise<Out> p;
        p.set_value(run());
        return p.get_future();
    }
    return worker_.Enqueue(std::move(run));
}
