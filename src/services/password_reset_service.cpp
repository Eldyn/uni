#include <services/password_reset_service.hpp>
#include <services/auth_service.hpp>
#include <common/crypto_hash.hpp>
#include <common/env.hpp>
#include <logger.hpp>
#include <openssl/rand.h>
#include <chrono>
#include <cstdint>

namespace {
int NowSeconds() {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}  // namespace

PasswordResetService::PasswordResetService(Database& db) : db_(db) {}

Result<std::string> PasswordResetService::GenerateToken() {
    // INFO: 32 bytes of CSPRNG output = 256-bit token space. Base64url without
    //       padding encodes 32 bytes to exactly 43 characters (10 full
    //       3-byte groups -> 40 chars, plus 2 trailing bytes -> 3 chars).
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    unsigned char raw[kTokenBytes];
    if (RAND_bytes(raw, sizeof(raw)) != 1) {
        return std::unexpected(
            Error::Internal("[Reset] CSPRNG failure: RAND_bytes returned 0"));
    }

    std::string out;
    out.reserve(43);
    for (std::size_t i = 0; i < kTokenBytes; i += 3) {
        uint32_t b = static_cast<uint32_t>(raw[i]) << 16;
        if (i + 1 < kTokenBytes) b |= static_cast<uint32_t>(raw[i + 1]) << 8;
        if (i + 2 < kTokenBytes) b |= static_cast<uint32_t>(raw[i + 2]);

        out += kAlphabet[(b >> 18) & 0x3F];
        out += kAlphabet[(b >> 12) & 0x3F];
        if (i + 1 < kTokenBytes) out += kAlphabet[(b >> 6) & 0x3F];
        if (i + 2 < kTokenBytes) out += kAlphabet[b & 0x3F];
    }
    return out;
}

std::string PasswordResetService::HashToken(const std::string& token) {
    return Sha256Hex(token);
}

int PasswordResetService::TokenTtlSeconds() {
    return Env::GetInt("RESET_TOKEN_TTL_SEC", kTokenTtlSeconds);
}

Result<IssuedReset> PasswordResetService::IssueTokenForEmail(const std::string& email) {
    auto row_result = db_.QueryOne(
        "SELECT id, email, username, locale FROM users WHERE email = ?;",
        {email});
    if (!row_result) return std::unexpected(row_result.error());
    if (!row_result->has_value()) {
        return std::unexpected(Error::NotFound("User not found"));
    }

    const DbRow& row = row_result->value();

    auto token = GenerateToken();
    if (!token) return std::unexpected(token.error());

    std::string hash       = HashToken(*token);
    int         now        = NowSeconds();
    int         expires_at = now + TokenTtlSeconds();

    TransactionGuard tx(db_);
    if (!tx.Ok()) return std::unexpected(tx.GetError());

    // INFO: INSERT OR REPLACE on the user_id PK replaces any prior token, so
    //       re-requesting atomically invalidates the old link.
    auto insert = db_.Exec(
        "INSERT OR REPLACE INTO password_reset_tokens "
        "(user_id, token_hash, expires_at, created_at) VALUES (?, ?, ?, ?);",
        {row.Get<int>("id"), hash, expires_at, now});
    if (!insert) return std::unexpected(insert.error());

    auto commit = tx.Commit();
    if (!commit) return std::unexpected(commit.error());

    return IssuedReset{
        row.Get<int>("id"),
        row.Get<std::string>("email"),
        row.Get<std::string>("username"),
        row.Get<std::string>("locale"),
        *token,
    };
}

Result<std::string> PasswordResetService::ConsumeToken(const std::string& token,
                                                       const std::string& new_password) {
    // INFO: The lookup, password write, and token delete all happen inside one
    //       TransactionGuard: the delete is what makes the token single-use, so
    //       it must commit atomically with the password change. N concurrent
    //       redemptions of the same link would otherwise all read the row
    //       before any delete landed.
    TransactionGuard tx(db_);
    if (!tx.Ok()) return std::unexpected(tx.GetError());

    std::string hash = HashToken(token);

    auto row_result = db_.QueryOne(
        "SELECT user_id FROM password_reset_tokens WHERE token_hash = ? AND expires_at > ?;",
        {hash, NowSeconds()});
    if (!row_result) return std::unexpected(row_result.error());
    if (!row_result->has_value()) {
        Logger::Info("[Reset] consume failed reason=invalid_or_expired");
        return std::unexpected(Error::Unauthorised("Invalid or expired link"));
    }

    int user_id = row_result->value().Get<int>("user_id");

    auto hashed = AuthService::HashPassword(new_password);
    if (!hashed) return std::unexpected(hashed.error());

    auto colon = hashed->find(':');
    if (colon == std::string::npos) {
        return std::unexpected(Error::Internal("[Reset] malformed password hash"));
    }
    std::string salt_b64 = hashed->substr(0, colon);
    std::string hash_b64 = hashed->substr(colon + 1);

    // INFO: A successful reset also sets email_verified = 1: clicking a link
    //       delivered to the address is the same proof as the verify flow.
    auto update = db_.Exec(
        "UPDATE users SET pass_hash = ?, salt = ?, email_verified = 1 WHERE id = ?;",
        {hash_b64, salt_b64, user_id});
    if (!update) return std::unexpected(update.error());

    auto username_result = db_.QueryOne("SELECT username FROM users WHERE id = ?;", {user_id});
    if (!username_result) return std::unexpected(username_result.error());
    if (!username_result->has_value()) {
        return std::unexpected(Error::Internal("[Reset] user vanished mid-transaction"));
    }
    std::string username = username_result->value().Get<std::string>("username");

    auto del = db_.Exec("DELETE FROM password_reset_tokens WHERE user_id = ?;", {user_id});
    if (!del) return std::unexpected(del.error());

    auto commit = tx.Commit();
    if (!commit) return std::unexpected(commit.error());

    Logger::Info("[Reset] password reset consumed user_id=" + std::to_string(user_id));
    return username;
}
