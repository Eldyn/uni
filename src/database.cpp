#include "database.hpp"

void DbRow::Set(const std::string& col, DbValue val) {
    data_[col] = std::move(val);
}

bool DbRow::Has(const std::string& col) const {
    return data_.contains(col);
}

// --- Database Implementation ---

Database& Database::Get() {
    static Database instance;
    return instance;
}

VoidResult Database::Open(std::string_view path) {
    if (sqlite3_open(path.data(), &db_) != SQLITE_OK) {
        return std::unexpected(Error::DatabaseFail(sqlite3_errmsg(db_)));
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
    // sqlite3_exec runs everything in the string until the end
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
        
        // C++23 std::visit: Handles the variant types safely
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
            // Null guard for text columns
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
