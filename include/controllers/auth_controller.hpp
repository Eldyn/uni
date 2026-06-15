#pragma once
#include <string>
#include <result.hpp>
#include <database.hpp>
#include <http_router.hpp>

/**
 * @file auth_controller.hpp
 * @brief Controller for authentication, registration and JWT token management.
 * * Manages the identity of the users in the system by implementing secure HTTP endpoints.
 * It handles the cryptographic hashing of passwords and the generation/validation
 * of the JSON Web Tokens (JWT) used for WebSocket authorization.
 */

/**
 * @struct JwtPayload
 * @brief Represents the data (payload) extracted from a successfully verified JWT token.
 * @tag CTRL-AUTH-STR-001
 */
struct JwtPayload {
    std::string username; /**< The identifying name of the authenticated user (the token's 'sub' field). */
};

/**
 * @class AuthController
 * @brief Handles the HTTP routes for login/registration and cryptographic security.
 * * **Password storage strategy:**
 * Uses the PBKDF2-HMAC-SHA256 algorithm (via OpenSSL) with a random 16-byte "salt"
 * for each user and a configurable number of iterations (200,000, per NIST standard).
 * The resulting hash (32 bytes) is stored base64-encoded together with the salt.
 * Additionally, a global "pepper" (read from the `JWT_SECRET` environment variable) is
 * appended to the password before hashing, making the theft of the database alone
 * insufficient to crack the passwords.
 * * **JWT strategy:**
 * The tokens are signed with HS256 using `jwt-cpp`.
 * The payload carries: `sub` (username), `iat` (issued-at), `exp` (expiry set to 24 hours).
 * @tag CTRL-AUTH-CLS-001
 */
class AuthController {
public:
    /**
     * @brief Constructor of the AuthController.
     * Automatically registers the POST and GET routes on the provided HTTP router (`/auth/register`, `/auth/login`, etc.).
     * * @param router Reference to the central HTTP router. Must outlive this class.
     * @tag CTRL-AUTH-MTH-001
     */
    explicit AuthController(HttpRouter& router);

    /**
     * @brief Mathematically verifies a JWT token and extracts its payload.
     * Static method called by the `WebServer` at the beginning of every upgrade attempt
     * to the WebSocket protocol, to block unauthorized clients.
     * * @param token The raw JWT string passed by the client (usually extracted from the cookies or the header).
     * @return Result<JwtPayload> The extracted payload on success, or an Error.
     * @tag CTRL-AUTH-MTH-002
     */
    static Result<JwtPayload> VerifyToken(const std::string& token);

private:
    // --- HTTP Route Handlers ---

    /**
     * @brief Handles the POST route `/auth/register`.
     * Validates the inputs (length and special characters), computes the secure hash of the password
     * and stores the new user in the database.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     * @tag CTRL-AUTH-ACT-001
     */
    void HandleRegister(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the POST route `/auth/login`.
     * Looks up the user in the database, recomputes the hash to verify the provided password
     * and, on success, issues and returns a signed JWT.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     * @tag CTRL-AUTH-ACT-002
     */
    void HandleLogin   (AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the POST route `/auth/logout`.
     * Invalidates the session on the client side (e.g. by requesting deletion of the JWT cookie).
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     * @tag CTRL-AUTH-ACT-003
     */
    void HandleLogout  (AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the GET route `/auth/me`.
     * Returns the details of the currently authenticated user by evaluating their token.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     * @tag CTRL-AUTH-ACT-004
     */
    void HandleMe      (AppResponse* res, AppRequest* req);

    // --- Cryptographic Helpers ---

    /**
     * @brief Generates a cryptographic salt and computes the hash of the password.
     * @param password The plaintext password entered by the user.
     * @return std::string Composite format stored in the DB: `<base64_salt>:<base64_hash>`.
     * @tag CTRL-AUTH-CRYP-001
     */
    static std::string HashPassword (const std::string& password);

    /**
     * @brief Checks whether a plaintext password matches the one stored in the DB.
     * @param password The plaintext password entered at login.
     * @param stored The string from the DB in the format `<salt>:<hash>`.
     * @return true if the credentials match, false otherwise.
     * @tag CTRL-AUTH-CRYP-002
     */
    static bool        VerifyPassword(const std::string& password, const std::string& stored);

    /**
     * @brief Generates a new signed JWT token using the secret key (pepper).
     * @param username The username to insert into the payload (the 'sub' field).
     * @return std::string The complete token string.
     * @tag CTRL-AUTH-CRYP-003
     */
    static std::string IssueToken(const std::string& username);

    // --- Validation and Security Parameters ---

    static constexpr int kMinUsernameLen = 3;       /**< @brief Minimum length of the username. @tag CTRL-AUTH-CFG-001 */
    static constexpr int kMaxUsernameLen = 32;      /**< @brief Maximum length of the username. @tag CTRL-AUTH-CFG-002 */
    static constexpr int kMinPasswordLen = 8;       /**< @brief Minimum length of the password. @tag CTRL-AUTH-CFG-003 */
    static constexpr int kMaxBodyBytes   = 4096;    /**< @brief Limit in bytes for the HTTP payload (Anti-DDoS). @tag CTRL-AUTH-CFG-004 */

    // PBKDF2 parameters (increasing kIterations raises the cost of brute-force attempts)

    static constexpr int kSaltBytes   = 16;         /**< @brief Size in bytes of the cryptographic Salt. @tag CTRL-AUTH-CFG-005 */
    static constexpr int kHashBytes   = 32;         /**< @brief Size in bytes of the resulting Hash. @tag CTRL-AUTH-CFG-006 */
    static constexpr int kIterations  = 200'000;    /**< @brief Number of iterations of the PBKDF2 algorithm (NIST SP 800-132 minimum). @tag CTRL-AUTH-CFG-007 */
};
