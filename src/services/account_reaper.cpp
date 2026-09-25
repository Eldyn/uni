#include "services/account_reaper.hpp"
#include "database.hpp"
#include "logger.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <ctime>

AccountReaper::AccountReaper(Database& db, std::chrono::seconds interval,
                             std::chrono::seconds grace)
    : db_(db), interval_(interval), grace_(grace) {
    if (sqlite3_threadsafe() != 1) {
        throw std::runtime_error(
            "AccountReaper requires sqlite3 to be compiled with threadsafe=1 (serialized)");
    }
}

AccountReaper::~AccountReaper() {
    Stop();
}

void AccountReaper::Start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread(&AccountReaper::Run, this);
}

void AccountReaper::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

int AccountReaper::SweepAt(std::int64_t now_unix) {
    TransactionGuard tx(db_);
    if (!tx.Ok()) {
        Logger::Error("[Reaper] Failed to begin transaction");
        return 0;
    }

    std::int64_t grace_threshold = now_unix - grace_.count();
    std::int64_t log_threshold = now_unix - 86400;

    // Must ensure pre-existing accounts (created_at = 0) are never deleted!
    auto res_codes = db_.Exec(
        "DELETE FROM email_verification_codes WHERE user_id IN (SELECT id FROM users "
        "WHERE email_verified = 0 AND created_at > 0 AND created_at < ?);",
        {static_cast<int>(grace_threshold)});
    if (!res_codes) {
        Logger::Error("[Reaper] Failed to delete codes: " + res_codes.error().message);
        return 0;
    }

    auto res_resets = db_.Exec(
        "DELETE FROM password_reset_tokens WHERE user_id IN (SELECT id FROM users "
        "WHERE email_verified = 0 AND created_at > 0 AND created_at < ?);",
        {static_cast<int>(grace_threshold)});
    if (!res_resets) {
        Logger::Error("[Reaper] Failed to delete reset tokens: " + res_resets.error().message);
        return 0;
    }

    auto res_users = db_.Exec(
        "DELETE FROM users WHERE email_verified = 0 AND created_at > 0 AND created_at < ?;",
        {static_cast<int>(grace_threshold)});
    if (!res_users) {
        Logger::Error("[Reaper] Failed to delete users: " + res_users.error().message);
        return 0;
    }

    int deleted = 0;
    auto changes_res = db_.QueryOne("SELECT changes() AS c;");
    if (changes_res && changes_res.value()) {
        deleted = changes_res.value()->GetOr<int>("c", 0);
    }

    auto res_log = db_.Exec("DELETE FROM email_send_log WHERE sent_at < ?;",
                            {static_cast<int>(log_threshold)});
    if (!res_log) {
        Logger::Error("[Reaper] Failed to delete send log: " + res_log.error().message);
        return 0;
    }

    if (auto cr = tx.Commit(); !cr) {
        Logger::Error("[Reaper] Failed to commit transaction: " + cr.error().message);
        return 0;
    }

    if (deleted > 0) {
        Logger::Log("[Reaper] purged ", deleted, " unverified account(s)");
    }

    return deleted;
}

void AccountReaper::Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_.load()) {
        cv_.wait_for(lock, interval_, [this] { return !running_.load(); });
        if (!running_.load()) {
            break;
        }
        SweepAt(std::time(nullptr));
    }
}
