#pragma once
#include <string>
#include <result.hpp>
#include <database.hpp>

/**
 * @file password_reset_service.hpp
 * @brief Domain layer for password reset: opaque magic-link token generation,
 * hashing, issuance, and single-use consumption.
 */

/**
 * @struct IssuedReset
 * @brief A freshly issued reset token, including the plaintext value needed to
 * send the email. The plaintext must never be logged or persisted anywhere
 * beyond this struct.
 */
struct IssuedReset {
    int         user_id;
    std::string email;
    std::string username;
    std::string locale;
    std::string plaintext_token;  // in-memory only, never logged
};

/**
 * @class PasswordResetService
 * @brief Owns the password-reset domain logic: issuing single-use magic-link
 * tokens (32 CSPRNG bytes, base64url, SHA-256-hex at rest), and consuming them
 * to set a new password and mark the account verified.
 *
 * A deliberate sibling of VerificationService: same hashing/send-log
 * infrastructure, separate table and lifecycle.
 */
class PasswordResetService {
public:
    /**
     * @param db Database to read/write users and reset tokens against.
     */
    explicit PasswordResetService(Database& db = Database::Get());

    /**
     * @brief Issues a fresh reset token for the account with this email
     * (any verification state), replacing any prior token for that user.
     * @param email Account email to look up.
     * @return Result<IssuedReset> The issued token (with plaintext), or
     * Error::NotFound for an unknown email.
     */
    Result<IssuedReset> IssueTokenForEmail(const std::string& email);

    /**
     * @brief Consumes a token in one transaction: verifies it is unexpired,
     * sets the new password (PBKDF2, reusing AuthService::HashPassword), marks
     * the account verified, and deletes the token row (single use).
     * @param token Plaintext magic-link token.
     * @param new_password New plaintext password.
     * @return Result<std::string> The account username on success, or
     * Error::Unauthorised for any invalid/expired/consumed token.
     */
    Result<std::string> ConsumeToken(const std::string& token,
                                     const std::string& new_password);

    /**
     * @brief Generates a 32-byte token via RAND_bytes, base64url encoded with
     * no padding (43 characters).
     * @return Result<std::string> The token, or an error if the CSPRNG failed.
     */
    static Result<std::string> GenerateToken();

    /**
     * @brief Hashes a plaintext token with SHA-256, lowercase hex.
     * @param token Plaintext token to hash.
     * @return std::string The 64-character lowercase hex digest.
     */
    static std::string HashToken(const std::string& token);

    /**
     * @brief Effective token TTL in seconds: RESET_TOKEN_TTL_SEC, defaulting
     * to kTokenTtlSeconds.
     */
    static int TokenTtlSeconds();

    static constexpr int kTokenBytes      = 32;
    static constexpr int kTokenTtlSeconds = 60 * 60;

private:
    Database& db_;
};
