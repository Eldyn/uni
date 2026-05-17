#include <common/base64.hpp>
#include <common/http.hpp>
#include <controllers/auth_controller.hpp>
#include <common/env.hpp>
#include <logger.hpp>
#include <protocol.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <stdexcept>
#include <vector>

using json = nlohmann::json;

AuthController::AuthController(HttpRouter& router) {
    router.Post("/auth/register", [this](AppResponse* res, AppRequest* req) {
        HandleRegister(res, req);
    });

    router.Post("/auth/login", [this](AppResponse* res, AppRequest* req) {
        HandleLogin(res, req);
    });
}

//  ReadBody  —  async body accumulation helper
//
//  uWS delivers HTTP bodies in chunks via an onData callback.  The bool
//  `isLast` tells us when the final chunk has arrived.  We accumulate all
//  chunks into a single std::string, then invoke the caller's callback.
//
//  The shared_ptr<bool> `is_alive` pattern is the standard uWS idiom for
//  detecting an aborted connection mid-read: the onAborted callback flips
//  the flag to false, so the onData callback can bail out safely without
//  touching a dangling `res` pointer.

void AuthController::HandleRegister(AppResponse* res, AppRequest* /*req*/) {
    http::ReadBody(res, kMaxBodyBytes, [res](const std::string& body) { 
        json data;
        try {
            data = json::parse(body);
        } catch (...) {
            res->writeStatus("400 Bad Request")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", "Invalid JSON"}}).dump());
            return;
        }

        std::string username = data.value("username", "");
        std::string email    = data.value("email",    "");
        std::string password = data.value("password", "");

        if (username.size() < kMinUsernameLen || username.size() > kMaxUsernameLen) {
            res->writeStatus("422 Unprocessable Entity")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", "Username must be 3–32 characters"}}).dump());
            return;
        }

        if (password.size() < static_cast<size_t>(kMinPasswordLen)) {
            res->writeStatus("422 Unprocessable Entity")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", "Password must be at least 8 characters"}}).dump());
            return;
        }

        auto duplicate = Database::Get().QueryOne(
            "SELECT id FROM users WHERE username = ? OR email = ?;",
            {username, email}
        );

        if (!duplicate) {
            Logger::Error("[Auth] DB error during register: " + duplicate.error().message);
            res->writeStatus("500 Internal Server Error")->end();
            return;
        }

        if (duplicate->has_value()) {
            res->writeStatus("409 Conflict")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", "Username or email already taken"}}).dump());
            return;
        }

        std::string stored = HashPassword(password);

        // The stored string is "<base64_salt>:<base64_hash>".
        // We split it here so the DB schema keeps salt and hash in separate columns
        auto colon = stored.find(':');
        std::string salt_b64 = stored.substr(0, colon);
        std::string hash_b64 = stored.substr(colon + 1);

        auto result = Database::Get().Exec(
            "INSERT INTO users (username, pass_hash, salt, email) VALUES (?, ?, ?, ?);",
            {username, hash_b64, salt_b64, email.empty() ? DbValue(nullptr) : DbValue(email)}
        );

        if (!result) {
            Logger::Error("[Auth] DB insert failed: " + result.error().message);
            res->writeStatus("500 Internal Server Error")->end();
            return;
        }

        Logger::Info("[Auth] Registered user: " + username);
        res->writeHeader("Content-Type", "application/json")
           ->end(json({{"status", "ok"}, {"message", "Registration successful"}}).dump());
    });
}

// ---------------------------------------------------------------------------
//  HandleLogin
// ---------------------------------------------------------------------------
void AuthController::HandleLogin(AppResponse* res, AppRequest* /*req*/) {
    http::ReadBody(res, kMaxBodyBytes, [res](const std::string& body) {
        json data;
        try {
            data = json::parse(body);
        } catch (...) {
            res->writeStatus("400 Bad Request")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", "Invalid JSON"}}).dump());
            return;
        }

        std::string email    = data.value("email",    "");
        std::string password = data.value("password", "");

        if (email.empty() || password.empty()) {
            res->writeStatus("422 Unprocessable Entity")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", "email and password are required"}}).dump());
            return;
        }

        auto row_result = Database::Get().QueryOne(
            "SELECT username, pass_hash, salt FROM users WHERE email = ?;",
            {email}
        );

        if (!row_result) {
            Logger::Error("[Auth] DB error during login: " + row_result.error().message);
            res->writeStatus("500 Internal Server Error")->end();
            return;
        }

        if (!row_result->has_value()) {
            res->writeStatus("401 Unauthorized")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", "Invalid credentials"}}).dump());
            return;
        }

        const DbRow& row       = row_result->value();
        std::string  username  = row.Get<std::string>("username");
        std::string  hash_b64  = row.Get<std::string>("pass_hash");
        std::string  salt_b64  = row.Get<std::string>("salt");

        // Reconstruct the "<salt>:<hash>" format that VerifyPassword expects
        std::string stored = salt_b64 + ":" + hash_b64;

        if (!VerifyPassword(password, stored)) {
            res->writeStatus("401 Unauthorized")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", "Invalid credentials"}}).dump());
            return;
        }

        std::string token = IssueToken(username);

        Logger::Info("[Auth] Login successful: " + username);
        res->writeHeader("Content-Type", "application/json")
           ->end(json({{"status", "ok"}, {"token", token}}).dump());
    });
}

// ---------------------------------------------------------------------------
//  Password hashing — PBKDF2-HMAC-SHA256
// ---------------------------------------------------------------------------
//
//  Why PBKDF2?  It is battle-tested, available directly in OpenSSL (already
//  linked), and acceptable for this project scale.  The main knob is
//  kIterations: more iterations = more CPU per guess = slower brute force.
//
//  The "pepper" is an application-level secret from the environment.
//  Unlike a salt (stored in the DB, unique per user, public), a pepper is
//  *not* stored anywhere — it lives only in memory, loaded from the env at
//  startup.  An attacker who steals the DB but not the server config cannot
//  run offline dictionary attacks even with the salts in hand.
std::string AuthController::HashPassword(const std::string& password) {
    // 1. Generate a cryptographically random salt.
    //    RAND_bytes fills the buffer with random bytes from OpenSSL's CSPRNG.
    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), kSaltBytes) != 1) {
    }

    // 2. Read the pepper from the environment and append it to the password.
    //    std::string + std::string = concatenation; no UB here.
    std::string pepper         = Env::Get("PASSWORD_PEPPER", "");
    std::string peppered_pass  = password + pepper;

    // 3. Derive the hash.
    //    PKCS5_PBKDF2_HMAC(password, passlen,
    //                       salt, saltlen,
    //                       iterations,
    //                       digest,        ← EVP_sha256() here
    //                       keylen,
    //                       output_buffer)
    //    Returns 1 on success, 0 on failure.
    std::vector<unsigned char> hash(kHashBytes);
    if (PKCS5_PBKDF2_HMAC(
            peppered_pass.c_str(),
            static_cast<int>(peppered_pass.size()),
            salt.data(),
            kSaltBytes,
            kIterations,
            EVP_sha256(),
            kHashBytes,
            hash.data()) != 1) {
        throw std::runtime_error("[Auth] PBKDF2 failed");
    }

    // 4. Encode both salt and hash to base64 and return as "<salt>:<hash>".
    //    Storing them together makes retrieval a single DB read.
    return Base64::Encode(salt) + ":" + Base64::Encode(hash);
}

bool AuthController::VerifyPassword(const std::string& password,
                                const std::string& stored) {
    auto colon = stored.find(':');
    if (colon == std::string::npos) return false;

    std::vector<unsigned char> salt      = Base64::Decode(stored.substr(0, colon));
    std::vector<unsigned char> ref_hash  = Base64::Decode(stored.substr(colon + 1));

    std::string pepper        = Env::Get("PASSWORD_PEPPER", "");
    std::string peppered_pass = password + pepper;

    std::vector<unsigned char> candidate(kHashBytes);
    if (PKCS5_PBKDF2_HMAC(
            peppered_pass.c_str(),
            static_cast<int>(peppered_pass.size()),
            salt.data(),
            static_cast<int>(salt.size()),
            kIterations,
            EVP_sha256(),
            kHashBytes,
            candidate.data()) != 1) {
        return false;
    }

    // CRYPTO_memcmp performs a constant-time comparison.
    // A regular `==` on std::vector short-circuits on the first mismatch,
    // which leaks timing information — a timing-side-channel attack can use
    // that to infer how many bytes of the hash match.
    // constant-time compare: returns 0 if equal (same convention as memcmp).
    return (CRYPTO_memcmp(candidate.data(), ref_hash.data(), kHashBytes) == 0);
}

// ---------------------------------------------------------------------------
//  JWT — issue + verify
// ---------------------------------------------------------------------------
//
//  jwt-cpp uses a fluent builder API.  We create a JWT with:
//    - algorithm  : HS256 (HMAC-SHA256) — symmetric, using JWT_SECRET
//    - subject    : username (who this token represents)
//    - issued_at  : current UTC time (for audit / debugging)
//    - expires_at : now + 24 hours (after which the token is rejected)
//
//  The secret is fetched via Env::Require every call rather than cached
//  so that secret rotation (SIGHUP + Env::Load) works without restart.
//  In a hot path you'd cache it, but token issuance / verification happen
//  only at login and upgrade — not per-message.

std::string AuthController::IssueToken(const std::string& username) {
    using namespace std::chrono;

    std::string secret = Env::Require("JWT_SECRET");

    auto now    = system_clock::now();
    auto expiry = now + hours(24);

    // jwt::create() returns a builder; each method returns *this for chaining.
    // sign() finalises the builder and returns the encoded token string.
    return jwt::create<jwt::traits::nlohmann_json>()
        .set_type("JWT")
        .set_subject(username)
        // system_clock::time_point is the type jwt-cpp expects for dates
        .set_issued_at(now)
        .set_expires_at(expiry)
        .sign(jwt::algorithm::hs256{secret});
}

Result<JwtPayload> AuthController::VerifyToken(const std::string& token) {
    std::string secret;
    try {
        secret = Env::Require("JWT_SECRET");
    } catch (const std::exception& e) {
        return std::unexpected(Error::Internal(e.what()));
    }

    try {
        // jwt::verify() builds a verifier; verify(decoded) does the actual check.
        // It throws jwt::token_verification_exception (or subclasses) on failure.
        auto verifier = jwt::verify<jwt::traits::nlohmann_json>()
            .allow_algorithm(jwt::algorithm::hs256{secret})
            // with_type("JWT") rejects tokens with a different typ header
            .with_type("JWT");

        // jwt::decode() parses the three dot-separated base64url segments.
        // This step throws if the token is structurally malformed.
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);

        // This throws if signature is invalid, token is expired, etc.
        verifier.verify(decoded);

        JwtPayload payload;
        // get_subject() returns the "sub" claim as std::string.
        payload.username = decoded.get_subject();
        return payload;

    } catch (const std::exception& e) {
        // Collapse all jwt-cpp exceptions into our Error type so callers
        // don't need to know about jwt-cpp internals.
        return std::unexpected(Error::Unauthorised(
            std::string("JWT verification failed: ") + e.what()));
    }
}
