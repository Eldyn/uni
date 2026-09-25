#include <services/verification_service.hpp>
#include <common/crypto_hash.hpp>
#include <common/email_send_log.hpp>
#include <logger.hpp>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <chrono>
#include <cstdint>
#include <format>

VerificationService::VerificationService(Database& db) : db_(db) {}

namespace {
int NowSeconds() {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}  // namespace

Result<std::string> VerificationService::GenerateCode() {
    // INFO: Rejection sampling avoids modulo bias: values >= 4'294'000'000u
    //       are redrawn so that the remaining range divides evenly into
    //       1'000'000 buckets (each digit combination equally likely).
    static constexpr uint32_t kRejectionCeiling = 4'294'000'000u;

    uint32_t value;
    do {
        unsigned char raw[4];
        if (RAND_bytes(raw, sizeof(raw)) != 1) {
            return std::unexpected(
                Error::Internal("[Verify] CSPRNG failure: RAND_bytes returned 0"));
        }
        value = (static_cast<uint32_t>(raw[0]) << 24) |
                (static_cast<uint32_t>(raw[1]) << 16) |
                (static_cast<uint32_t>(raw[2]) << 8)  |
                (static_cast<uint32_t>(raw[3]));
    } while (value >= kRejectionCeiling);

    uint32_t code = value % 1'000'000u;
    return std::format("{:06}", code);
}

std::string VerificationService::HashCode(const std::string& code) {
    // INFO: Delegates to the shared SHA-256 helper so verification codes and
    //       password-reset tokens cannot drift apart.
    return Sha256Hex(code);
}

namespace {
// Shared by IssueCode/IssueCodeForEmail: given the looked-up user row,
// generates+hashes a fresh code and upserts it as the user's sole active
// verification code.
Result<IssuedCode> IssueCodeForUser(Database& db, const DbRow& row) {
    int user_id = row.Get<int>("id");

    if (row.Get<int>("email_verified") == 1) {
        return std::unexpected(Error::Conflict("Already verified"));
    }

    auto code = VerificationService::GenerateCode();
    if (!code) return std::unexpected(code.error());

    std::string hash = VerificationService::HashCode(*code);
    int now        = NowSeconds();
    int expires_at = now + VerificationService::kExpirySeconds;

    TransactionGuard tx(db);
    if (!tx.Ok()) return std::unexpected(tx.GetError());

    auto insert = db.Exec(
        "INSERT OR REPLACE INTO email_verification_codes "
        "(user_id, code_hash, expires_at, attempt_count, created_at) "
        "VALUES (?, ?, ?, 0, ?);",
        {user_id, hash, expires_at, now});
    if (!insert) return std::unexpected(insert.error());

    auto commit = tx.Commit();
    if (!commit) return std::unexpected(commit.error());

    Logger::Info("[Verify] code issued user_id=" + std::to_string(user_id));

    return IssuedCode{
        user_id,
        row.Get<std::string>("email"),
        row.Get<std::string>("username"),
        row.Get<std::string>("locale"),
        *code,
    };
}
}  // namespace

Result<IssuedCode> VerificationService::IssueCode(const std::string& username) {
    auto row_result = db_.QueryOne(
        "SELECT id, email, username, locale, email_verified FROM users WHERE username = ?;",
        {username});
    if (!row_result) return std::unexpected(row_result.error());
    if (!row_result->has_value()) {
        return std::unexpected(Error::NotFound("User not found"));
    }
    return IssueCodeForUser(db_, row_result->value());
}

Result<IssuedCode> VerificationService::IssueCodeForEmail(const std::string& email) {
    auto row_result = db_.QueryOne(
        "SELECT id, email, username, locale, email_verified FROM users WHERE email = ?;",
        {email});
    if (!row_result) return std::unexpected(row_result.error());
    if (!row_result->has_value()) {
        return std::unexpected(Error::NotFound("User not found"));
    }
    return IssueCodeForUser(db_, row_result->value());
}

VoidResult VerificationService::ConfirmCode(int user_id, const std::string& submitted) {
    // INFO: The read (attempt_count/expires_at), the cap/expiry branch, and
    //       the resulting write (increment/delete/confirm) all happen inside
    //       one TransactionGuard so the whole check-then-act sequence is
    //       atomic. Without this, N concurrent guesses could all read the
    //       same pre-increment attempt_count before any of their increments
    //       committed, letting more than kMaxAttempts guesses land before the
    //       cap trips.
    TransactionGuard tx(db_);
    if (!tx.Ok()) return std::unexpected(tx.GetError());

    auto row_result = db_.QueryOne(
        "SELECT code_hash, expires_at, attempt_count FROM email_verification_codes "
        "WHERE user_id = ?;",
        {user_id});
    if (!row_result) return std::unexpected(row_result.error());

    if (!row_result->has_value()) {
        Logger::Info("[Verify] confirm failed user_id=" + std::to_string(user_id) +
                     " reason=wrong");
        return std::unexpected(Error::Unauthorised("Invalid or expired code"));
    }

    const DbRow& row = row_result->value();
    int attempt_count = row.Get<int>("attempt_count");

    if (attempt_count >= kMaxAttempts) {
        auto del = db_.Exec("DELETE FROM email_verification_codes WHERE user_id = ?;", {user_id});
        if (!del) return std::unexpected(del.error());
        auto commit = tx.Commit();
        if (!commit) return std::unexpected(commit.error());
        Logger::Info("[Verify] confirm failed user_id=" + std::to_string(user_id) +
                     " reason=locked");
        return std::unexpected(Error::TooManyRequests("Too many attempts. Request a new code."));
    }

    int expires_at = row.Get<int>("expires_at");
    if (NowSeconds() >= expires_at) {
        auto del = db_.Exec("DELETE FROM email_verification_codes WHERE user_id = ?;", {user_id});
        if (!del) return std::unexpected(del.error());
        auto commit = tx.Commit();
        if (!commit) return std::unexpected(commit.error());
        Logger::Info("[Verify] confirm failed user_id=" + std::to_string(user_id) +
                     " reason=expired");
        return std::unexpected(Error::Unauthorised("Invalid or expired code"));
    }

    std::string stored_hash    = row.Get<std::string>("code_hash");
    std::string submitted_hash = HashCode(submitted);

    bool match = stored_hash.size() == submitted_hash.size() &&
                 stored_hash.size() == 64 &&
                 CRYPTO_memcmp(stored_hash.data(), submitted_hash.data(), stored_hash.size()) == 0;

    if (!match) {
        auto update = db_.Exec(
            "UPDATE email_verification_codes SET attempt_count = attempt_count + 1 "
            "WHERE user_id = ?;",
            {user_id});
        if (!update) return std::unexpected(update.error());
        auto commit = tx.Commit();
        if (!commit) return std::unexpected(commit.error());
        Logger::Info("[Verify] confirm failed user_id=" + std::to_string(user_id) +
                     " reason=wrong");
        return std::unexpected(Error::Unauthorised("Invalid or expired code"));
    }

    auto update_user = db_.Exec("UPDATE users SET email_verified = 1 WHERE id = ?;", {user_id});
    if (!update_user) return std::unexpected(update_user.error());

    auto delete_code = db_.Exec("DELETE FROM email_verification_codes WHERE user_id = ?;",
                                {user_id});
    if (!delete_code) return std::unexpected(delete_code.error());

    auto commit = tx.Commit();
    if (!commit) return std::unexpected(commit.error());

    return {};
}

Result<int> VerificationService::SendsInLast24h(int user_id) {
    return EmailSendLog(db_).SendsInLast24h(user_id);
}

void VerificationService::RecordSend(int user_id) {
    EmailSendLog(db_).RecordSend(user_id);
}
