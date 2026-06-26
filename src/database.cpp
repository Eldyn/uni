#include "database.hpp"

void DbRow::Set(const std::string& col, DbValue val) {
    data_[col] = std::move(val);
}

bool DbRow::Has(const std::string& col) const {
    return data_.contains(col);
}

namespace {

struct Migration {
    int         version;
    const char* sql;
};

static constexpr Migration MIGRATIONS[] = {
    { 1, R"sql(
        CREATE TABLE IF NOT EXISTS users (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            username  TEXT    NOT NULL UNIQUE,
            pass_hash TEXT    NOT NULL,
            salt      TEXT    NOT NULL,
            email     TEXT    NOT NULL UNIQUE
        );
        CREATE TABLE IF NOT EXISTS matches (
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            winner_username  TEXT    NOT NULL,
            ended_at         DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS match_participants (
            match_id INTEGER NOT NULL,
            username TEXT    NOT NULL,
            FOREIGN KEY(match_id) REFERENCES matches(id) ON DELETE CASCADE,
            PRIMARY KEY(match_id, username)
        );
        CREATE TABLE IF NOT EXISTS player_stats (
            username          TEXT PRIMARY KEY,
            total_wins        INTEGER DEFAULT 0,
            total_losses      INTEGER DEFAULT 0,
            cards_played_red     INTEGER DEFAULT 0,
            cards_played_blue    INTEGER DEFAULT 0,
            cards_played_green   INTEGER DEFAULT 0,
            cards_played_yellow  INTEGER DEFAULT 0,
            cards_played_0    INTEGER DEFAULT 0,
            cards_played_1    INTEGER DEFAULT 0,
            cards_played_2    INTEGER DEFAULT 0,
            cards_played_3    INTEGER DEFAULT 0,
            cards_played_4    INTEGER DEFAULT 0,
            cards_played_5    INTEGER DEFAULT 0,
            cards_played_6    INTEGER DEFAULT 0,
            cards_played_7    INTEGER DEFAULT 0,
            cards_played_8    INTEGER DEFAULT 0,
            cards_played_9    INTEGER DEFAULT 0,
            cards_played_skip        INTEGER DEFAULT 0,
            cards_played_reverse     INTEGER DEFAULT 0,
            cards_played_draw2       INTEGER DEFAULT 0,
            cards_played_draw4       INTEGER DEFAULT 0,
            cards_played_colorswitch INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS saved_matches (
            id         TEXT PRIMARY KEY,
            state_json TEXT     NOT NULL,
            saved_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
            expires_at DATETIME DEFAULT (datetime('now', '+1 day'))
        );
        CREATE TABLE IF NOT EXISTS saved_match_participants (
            match_id TEXT NOT NULL,
            username TEXT NOT NULL,
            FOREIGN KEY(match_id) REFERENCES saved_matches(id) ON DELETE CASCADE,
            PRIMARY KEY(match_id, username)
        );
    )sql" },
    { 2, "ALTER TABLE player_stats RENAME COLUMN cards_played_colorswitch TO cards_played_jolly;" },
};

} // namespace

// Database implementation

Database& Database::Get() {
    static Database instance;
    return instance;
}

VoidResult Database::Open(std::string_view path) {
    if (sqlite3_open(path.data(), &db_) != SQLITE_OK) {
        return std::unexpected(Error::DatabaseFail(sqlite3_errmsg(db_)));
    }

    // INFO: WAL lets a backup reader take a consistent snapshot while the
    //       server keeps writing; the default rollback journal can tear a
    //       concurrent file copy. synchronous=NORMAL is the safe pairing
    //       with WAL, and busy_timeout avoids spurious SQLITE_BUSY when the
    //       backup reader and the server briefly overlap.
    const char* pragmas =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA busy_timeout=5000;";
    char* err = nullptr;
    if (sqlite3_exec(db_, pragmas, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "Unknown error";
        sqlite3_free(err);
        return std::unexpected(Error::DatabaseFail(msg));
    }
    return {};
}

void Database::Close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::IsOpen() const {
    return db_ != nullptr;
}

VoidResult Database::ApplySchema(const char* sql) {
    char* zErrMsg = nullptr;
    // INFO: sqlite3_exec runs everything in the string until the end
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &zErrMsg);
    
    if (rc != SQLITE_OK) {
        std::string err = zErrMsg ? zErrMsg : "Unknown error";
        sqlite3_free(zErrMsg); // Must free this!
        return std::unexpected(Error::DatabaseFail(err));
    }
    return {};
}

sqlite3_stmt* Database::Prepare(const char* sql, const std::vector<DbValue>& params) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return nullptr;
    }

    for (size_t i = 0; i < params.size(); ++i) {
        int index = static_cast<int>(i) + 1; // SQLite uses 1-based indexing for params
        
        // INFO: C++23 std::visit handles the variant types safely
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int>) {
                sqlite3_bind_int(stmt, index, arg);
            } else if constexpr (std::is_same_v<T, double>) {
                sqlite3_bind_double(stmt, index, arg);
            } else if constexpr (std::is_same_v<T, std::string>) {
                sqlite3_bind_text(stmt, index, arg.c_str(), -1, SQLITE_TRANSIENT);
            } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                sqlite3_bind_null(stmt, index);
            }
        }, params[i]);
    }
    return stmt;
}

DbRow Database::ReadRow(sqlite3_stmt* stmt) {
    DbRow row;
    int col_count = sqlite3_column_count(stmt);

    for (int i = 0; i < col_count; ++i) {
        std::string col_name = sqlite3_column_name(stmt, i);
        int type = sqlite3_column_type(stmt, i);

        if (type == SQLITE_INTEGER) {
            row.Set(col_name, sqlite3_column_int(stmt, i));
        } else if (type == SQLITE_FLOAT) {
            row.Set(col_name, sqlite3_column_double(stmt, i));
        } else if (type == SQLITE_TEXT) {
            const unsigned char* text = sqlite3_column_text(stmt, i);
            // INFO: Null guard for text columns
            row.Set(col_name, text ? std::string(reinterpret_cast<const char*>(text)) : DbValue(nullptr));
        } else {
            row.Set(col_name, nullptr);
        }
    }
    return row;
}


VoidResult Database::Exec(const char* sql, std::vector<DbValue> params) {
    sqlite3_stmt* stmt = Prepare(sql, params);
    if (!stmt) return std::unexpected(Error::DatabaseFail(sqlite3_errmsg(db_)));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        return std::unexpected(Error::DatabaseFail(sqlite3_errmsg(db_)));
    }
    return {};
}

Result<std::vector<DbRow>> Database::Query(const char* sql, std::vector<DbValue> params) {
    sqlite3_stmt* stmt = Prepare(sql, params);
    if (!stmt) return std::unexpected(Error::DatabaseFail(sqlite3_errmsg(db_)));

    std::vector<DbRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rows.push_back(ReadRow(stmt));
    }

    sqlite3_finalize(stmt);
    return rows;
}

Result<std::optional<DbRow>> Database::QueryOne(const char* sql, std::vector<DbValue> params) {
    sqlite3_stmt* stmt = Prepare(sql, params);
    if (!stmt) return std::unexpected(Error::DatabaseFail(sqlite3_errmsg(db_)));

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        DbRow row = ReadRow(stmt);
        sqlite3_finalize(stmt);
        return row;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

int Database::GetSchemaVersion() {
    auto result = QueryOne("PRAGMA user_version;", {});
    if (!result || !result.value()) return 0;
    return result.value()->Get<int>("user_version");
}

VoidResult Database::SetSchemaVersion(int version) {
    return ApplySchema(("PRAGMA user_version = " + std::to_string(version) + ";").c_str());
}

VoidResult Database::RunMigrations() {
    int current = GetSchemaVersion();
    for (const auto& m : MIGRATIONS) {
        if (m.version <= current) continue;
        TransactionGuard tx(*this);
        if (auto r = ApplySchema(m.sql); !r) return r;
        if (auto r = SetSchemaVersion(m.version); !r) return r;
        tx.Commit();
        Logger::Info("[DB] Migration v", m.version, " applied");
    }
    return {};
}
