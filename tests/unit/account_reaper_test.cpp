#include <doctest/doctest.h>
#include "services/account_reaper.hpp"
#include "database.hpp"
#include <chrono>
#include <ctime>

namespace {
    int InsertTestUser(const std::string& username, int email_verified, std::int64_t created_at) {
        auto& db = Database::Get();
        db.RunMigrations();
        auto res = db.Exec("INSERT INTO users (username, pass_hash, salt, email, email_verified, created_at) VALUES (?, 'hash', 'salt', ?, ?, ?);",
            {username, username + "@example.com", email_verified, static_cast<int>(created_at)});
        if (!res) throw std::runtime_error("InsertTestUser failed: " + res.error().message);
        
        auto row = db.QueryOne("SELECT id FROM users WHERE username = ?;", {username});
        if (!row || !row.value()) throw std::runtime_error("InsertTestUser couldn't find user");
        return row.value()->Get<int>("id");
    }

    bool UserExists(const std::string& username) {
        auto& db = Database::Get();
        auto res = db.QueryOne("SELECT id FROM users WHERE username = ?;", {username});
        return res.has_value() && res.value().has_value();
    }

    void IssueTestCode(int user_id) {
        auto& db = Database::Get();
        auto res = db.Exec("INSERT INTO email_verification_codes (user_id, code_hash, expires_at, created_at) VALUES (?, 'hash', 9999999999, 0);", {user_id});
        if (!res) throw std::runtime_error("IssueTestCode failed: " + res.error().message);
    }

    bool CodeRowExists(int user_id) {
        auto& db = Database::Get();
        auto res = db.QueryOne("SELECT id FROM email_verification_codes WHERE user_id = ?;", {user_id});
        return res.has_value() && res.value().has_value();
    }

    void IssueTestResetToken(int user_id) {
        auto& db = Database::Get();
        auto res = db.Exec("INSERT OR REPLACE INTO password_reset_tokens (user_id, token_hash, expires_at, created_at) VALUES (?, 'resethash', 9999999999, 0);", {user_id});
        if (!res) throw std::runtime_error("IssueTestResetToken failed: " + res.error().message);
    }

    bool ResetRowExists(int user_id) {
        auto& db = Database::Get();
        auto res = db.QueryOne("SELECT user_id FROM password_reset_tokens WHERE user_id = ?;", {user_id});
        return res.has_value() && res.value().has_value();
    }
}

TEST_CASE("SweepAt deletes unverified accounts past grace, keeps others") {
    // Database is assumed to be already opened in test_main.cpp or similar
    // setup.
    AccountReaper reaper(Database::Get(), std::chrono::hours(1), std::chrono::hours(24 * 7));
    auto now = std::time(nullptr);
    
    // Also insert a created_at = 0 user
    InsertTestUser("reap_legacy", /*email_verified=*/0, /*created_at=*/0);
    
    InsertTestUser("reap_old", /*email_verified=*/0, /*created_at=*/now - 8 * 86400);
    InsertTestUser("reap_recent", /*email_verified=*/0, /*created_at=*/now - 6 * 86400);
    InsertTestUser("reap_verified", /*email_verified=*/1, /*created_at=*/now - 30 * 86400);
    
    int deleted = reaper.SweepAt(now);
    
    CHECK(deleted == 1);
    CHECK(!UserExists("reap_old"));
    CHECK(UserExists("reap_legacy"));
    CHECK(UserExists("reap_recent"));
    CHECK(UserExists("reap_verified"));
}

TEST_CASE("SweepAt cascades to email_verification_codes") {
    AccountReaper reaper(Database::Get(), std::chrono::hours(1), std::chrono::hours(24 * 7));
    auto now = std::time(nullptr);
    int user_id = InsertTestUser("reap_cascade", 0, now - 8 * 86400);
    IssueTestCode(user_id);
    reaper.SweepAt(now);
    CHECK(!CodeRowExists(user_id));
}

TEST_CASE("SweepAt deletes reset tokens for reaped accounts, keeps live ones") {
    AccountReaper reaper(Database::Get(), std::chrono::hours(1), std::chrono::hours(24 * 7));
    auto now = std::time(nullptr);

    int reap_id = InsertTestUser("reap_reset_reaped", 0, now - 8 * 86400);
    IssueTestResetToken(reap_id);

    int live_id = InsertTestUser("reap_reset_live", 0, now - 6 * 86400);
    IssueTestResetToken(live_id);

    reaper.SweepAt(now);

    CHECK(!UserExists("reap_reset_reaped"));
    CHECK(!ResetRowExists(reap_id));
    CHECK(UserExists("reap_reset_live"));
    CHECK(ResetRowExists(live_id));
}

TEST_CASE("Start/Stop does not hang") {
    AccountReaper reaper(Database::Get(), std::chrono::hours(1), std::chrono::hours(24 * 7));
    reaper.Start();
    reaper.Stop();
    CHECK(true);
}
