#pragma once
#include <string>
#include <result.hpp>
#include <database.hpp>

/**
 * @file verification_service.hpp
 * @brief Domain layer for email verification: one-time code generation,
 * hashing, issuance, and confirmation.
 */

/**
 * @struct IssuedCode
 * @brief A freshly issued verification code, including the plaintext value
 * needed to send the email. The plaintext must never be logged or persisted
 * anywhere beyond this struct.
 */
struct IssuedCode {
    int         user_id;
    std::string email;
    std::string username;
    std::string locale;
    std::string plaintext_code;   // in-memory only, never logged
};

/**
 * @class VerificationService
 * @brief Owns the email verification domain logic: issuing one-time codes,
 * hashing them (SHA-256 hex) for storage, confirming submitted codes with
 * attempt-cap and expiry enforcement, and tracking send volume for
 * rate-limiting the "resend" endpoint.
 */
class VerificationService {
public:
    /**
     * @param db Database to read/write users and verification codes against.
     */
    explicit VerificationService(Database& db = Database::Get());

    /**
     * @brief Issues a fresh verification code for the given username,
     * invalidating any previously issued code for that user.
     * @param username Account username to look up.
     * @return Result<IssuedCode> The issued code (with plaintext), or the
     * reason issuance was rejected (not found, already verified, DB failure).
     */
    Result<IssuedCode> IssueCode(const std::string& username);

    /**
     * @brief Same as IssueCode, but looks up the account by email instead of
     * username (used by the magic link and the migration blast).
     * @param email Account email to look up.
     * @return Result<IssuedCode> The issued code, or the rejection reason.
     */
    Result<IssuedCode> IssueCodeForEmail(const std::string& email);

    /**
     * @brief Confirms a submitted code against the stored hash, enforcing the
     * attempt cap and expiry. On success, marks the account verified and
     * deletes the pending code row.
     * @param user_id Account to confirm.
     * @param submitted Plaintext code entered by the user.
     * @return VoidResult Empty on success, or the reason confirmation failed
     * (no code, too many attempts, expired, or wrong code).
     */
    VoidResult ConfirmCode(int user_id, const std::string& submitted);

    /**
     * @brief Counts verification emails sent to this user in the last 24h,
     * used to rate-limit resends.
     * @param user_id Account to check.
     * @return Result<int> The count, or a DB failure.
     */
    Result<int> SendsInLast24h(int user_id);

    /**
     * @brief Records that a verification email was sent to this user, for
     * SendsInLast24h accounting.
     * @param user_id Account the email was sent to.
     */
    void RecordSend(int user_id);

    /**
     * @brief Hashes a plaintext code with SHA-256, hex-encoded lowercase.
     * Hex is chosen over base64: the value is only ever compared, never
     * decoded, and hex stays greppable in DB inspection without exposing the
     * code.
     * @param code Plaintext code to hash.
     * @return std::string The 64-character lowercase hex digest.
     */
    static std::string HashCode(const std::string& code);

    /**
     * @brief Generates a cryptographically random 6-digit code via rejection
     * sampling over RAND_bytes output.
     * @return Result<std::string> The zero-padded 6-digit code, or an error
     * if the CSPRNG failed.
     */
    static Result<std::string> GenerateCode();

    static constexpr int kCodeDigits     = 6;
    static constexpr int kExpirySeconds  = 15 * 60;
    static constexpr int kMaxAttempts    = 5;
    static constexpr int kMaxSendsPerDay = 5;

private:
    Database& db_;
};
