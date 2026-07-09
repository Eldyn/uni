#pragma once
#include <chrono>
#include <string>
#include <result.hpp>
#include <database.hpp>
#include <common/login_throttle.hpp>

/**
 * @file auth_service.hpp
 * @brief Domain layer for authentication: password hashing, JWT issuance and
 * verification, login throttling, guest identity, and account registration.
 */

/**
 * @struct JwtPayload
 * @brief Represents the data (payload) extracted from a successfully verified JWT token.
 * @tag SVC-AUTH-STR-001
 */
struct JwtPayload {
    /**< The identifying name of the authenticated user (the token's 'sub' field). */
    std::string username;
};

/**
 * @struct AuthSession
 * @brief A freshly-established session: the identity it belongs to and its
 * signed token.
 * @tag SVC-AUTH-STR-002
 */
struct AuthSession {
    std::string username;
    std::string token;
};

/**
 * @class AuthService
 * @brief Owns the authentication domain logic, independent of the HTTP/wire
 * layer: password hashing (PBKDF2-HMAC-SHA256 with a per-user salt and a
 * global pepper), HS256 JWT issuance/verification, per-(email,ip) login
 * throttling, guest identity generation, and account registration.
 * @tag SVC-AUTH-CLS-001
 */
class AuthService {
public:
    /**
     * @param db Database to read/write user accounts against.
     * @tag SVC-AUTH-MTH-001
     */
    explicit AuthService(Database& db = Database::Get());

    /**
     * @brief Validates and registers a new account.
     * @param username Desired username (length-checked, rejected if reserved
     * for AI bots).
     * @param email Optional email (stored as NULL if empty).
     * @param password Plaintext password (length-checked, then hashed).
     * @return VoidResult Empty on success, or the reason registration was
     * rejected/failed.
     * @tag SVC-AUTH-MTH-002
     */
    VoidResult Register(const std::string& username, const std::string& email,
                         const std::string& password);

    /**
     * @brief Verifies credentials against the throttle and the stored hash,
     * issuing a session token on success.
     * @param email Account email to look up.
     * @param password Plaintext password to verify.
     * @param client_ip Resolved client IP, used as part of the throttle key.
     * @return Result<AuthSession> The issued session, or the reason login
     * was rejected (invalid credentials, throttled, DB/internal failure).
     * @tag SVC-AUTH-MTH-003
     */
    Result<AuthSession> Login(const std::string& email, const std::string& password,
                               const std::string& client_ip);

    /**
     * @brief Issues an ephemeral, account-less guest session with a freshly
     * generated display name.
     * @return Result<AuthSession> The guest session, or an error if name/token
     * generation failed.
     * @tag SVC-AUTH-MTH-004
     */
    Result<AuthSession> CreateGuestSession();

    /**
     * @brief Generates a cryptographic salt and computes the hash of the password.
     * @param password The plaintext password entered by the user.
     * @return Result<std::string> Composite format stored in the DB:
     * `<base64_salt>:<base64_hash>`, or an error if the CSPRNG/PBKDF2 failed.
     * @tag SVC-AUTH-MTH-005
     */
    static Result<std::string> HashPassword(const std::string& password);

    /**
     * @brief Checks whether a plaintext password matches the one stored in the DB.
     * @param password The plaintext password entered at login.
     * @param stored The string from the DB in the format `<salt>:<hash>`.
     * @return true if the credentials match, false otherwise.
     * @tag SVC-AUTH-MTH-006
     */
    static bool VerifyPassword(const std::string& password, const std::string& stored);

    /**
     * @brief Generates a new signed JWT token.
     * @param username The username to insert into the payload (the 'sub' field).
     * @return Result<std::string> The complete token string, or an error if
     * the signing secret could not be read.
     * @tag SVC-AUTH-MTH-007
     */
    static Result<std::string> IssueToken(const std::string& username);

    /**
     * @brief Verifies a JWT token and extracts its payload. Called by
     * `WebServer` at the beginning of every WebSocket upgrade attempt.
     * @param token The raw JWT string.
     * @return Result<JwtPayload> The extracted payload on success, or an Error.
     * @tag SVC-AUTH-MTH-008
     */
    static Result<JwtPayload> VerifyToken(const std::string& token);

    /**
     * @brief Generates a random guest display name ("Guest#" + 5 base32
     * characters). The '#' falls outside the registration username pattern,
     * so a guest can never collide with a registered account.
     * @return Result<std::string> The generated name, or an error if the
     * CSPRNG failed.
     * @tag SVC-AUTH-MTH-009
     */
    static Result<std::string> GenerateGuestName();

private:
    Database& db_;
    LoginThrottle login_throttle_;
    LoginThrottle::Clock::time_point last_evict_;

    // PBKDF2 parameters (increasing kIterations raises the cost of brute-force attempts)
    static constexpr int kSaltBytes  = 16;
    static constexpr int kHashBytes  = 32;
    static constexpr int kIterations = 200'000;
};
