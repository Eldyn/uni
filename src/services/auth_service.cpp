#include <services/auth_service.hpp>
#include <common/base64.hpp>
#include <common/env.hpp>
#include <common/contract.hpp>
#include <common/bot_names.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include <logger.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>

AuthService::AuthService(Database& db)
    : db_(db),
      login_throttle_(std::stoi(Env::Get("LOGIN_MAX_FAILS", "5")),
                      std::chrono::seconds(std::stoi(Env::Get("LOGIN_LOCKOUT_SEC", "300")))),
      last_evict_(LoginThrottle::Clock::now()) {}

VoidResult AuthService::Register(const std::string& username, const std::string& email,
                                  const std::string& password) {
    if (username.size() < contract::kUsernameMin || username.size() > contract::kUsernameMax) {
        return std::unexpected(Error::InvalidInput("Username must be 3–32 characters"));
    }

    if (password.size() < static_cast<size_t>(contract::kPasswordMin)) {
        return std::unexpected(Error::InvalidInput("Password must be at least 8 characters"));
    }

    auto duplicate = db_.QueryOne(
        "SELECT id FROM users WHERE username = ? OR email = ?;",
        {username, email});

    if (!duplicate) {
        Logger::Error("[Auth] DB error during register: " + duplicate.error().message);
        return std::unexpected(Error::DatabaseFail(duplicate.error().message));
    }

    std::string lower_username = username;
    std::transform(lower_username.begin(), lower_username.end(), lower_username.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    bool is_reserved = lower_username.starts_with("bot_");

    if (!is_reserved) {
        for (const auto& bot_name : match::kReservedBotNames) {
            std::string lower_bot = bot_name;
            std::transform(lower_bot.begin(), lower_bot.end(), lower_bot.begin(),
                           [](unsigned char c){ return std::tolower(c); });

            if (lower_username == lower_bot) {
                is_reserved = true;
                break;
            }
        }
    }

    if (is_reserved) {
        return std::unexpected(Error::InvalidInput("This username is reserved for AI bots"));
    }

    if (duplicate->has_value()) {
        return std::unexpected(Error::Conflict("Username or email already taken"));
    }

    auto hashed = HashPassword(password);
    if (!hashed) return std::unexpected(hashed.error());

    // INFO: The stored string is "<base64_salt>:<base64_hash>". We split it
    //       here so the DB schema keeps salt and hash in separate columns.
    auto colon = hashed->find(':');
    std::string salt_b64 = hashed->substr(0, colon);
    std::string hash_b64 = hashed->substr(colon + 1);

    auto result = db_.Exec(
        "INSERT INTO users (username, pass_hash, salt, email) VALUES (?, ?, ?, ?);",
        {username, hash_b64, salt_b64, email.empty() ? DbValue(nullptr) : DbValue(email)});

    if (!result) {
        Logger::Error("[Auth] DB insert failed: " + result.error().message);
        return std::unexpected(Error::DatabaseFail(result.error().message));
    }

    Logger::Info("[Auth] Registered user: " + username);
    return {};
}

Result<AuthSession> AuthService::Login(const std::string& email, const std::string& password,
                                        const std::string& client_ip) {
    // INFO: Bound the throttle map: sweep idle entries at most once a minute.
    const auto now = LoginThrottle::Clock::now();
    if (now - last_evict_ >= std::chrono::seconds(60)) {
        last_evict_ = now;
        login_throttle_.Evict();
    }

    // INFO: Per-(email,ip) lockout, checked before any DB lookup or PBKDF2 so
    //       a locked-out attacker costs nothing. Per-IP keying avoids letting
    //       a third party lock a victim out by spamming their email.
    const std::string throttle_key = email + "|" + client_ip;
    if (login_throttle_.IsLocked(throttle_key)) {
        return std::unexpected(Error::TooManyRequests(
            "Too many failed attempts. Try again later."));
    }

    auto row_result = db_.QueryOne(
        "SELECT username, pass_hash, salt FROM users WHERE email = ?;",
        {email});

    if (!row_result) {
        Logger::Error("[Auth] DB error during login: " + row_result.error().message);
        return std::unexpected(Error::DatabaseFail(row_result.error().message));
    }

    if (!row_result->has_value()) {
        login_throttle_.RecordFailure(throttle_key);
        return std::unexpected(Error::Unauthorised("Invalid credentials"));
    }

    const DbRow& row       = row_result->value();
    std::string  username  = row.Get<std::string>("username");
    std::string  hash_b64  = row.Get<std::string>("pass_hash");
    std::string  salt_b64  = row.Get<std::string>("salt");

    // INFO: Reconstruct the "<salt>:<hash>" format that VerifyPassword expects.
    std::string stored = salt_b64 + ":" + hash_b64;

    if (!VerifyPassword(password, stored)) {
        login_throttle_.RecordFailure(throttle_key);
        return std::unexpected(Error::Unauthorised("Invalid credentials"));
    }

    login_throttle_.Reset(throttle_key);

    auto token = IssueToken(username);
    if (!token) return std::unexpected(token.error());

    Logger::Info("[Auth] Login successful: " + username);
    return AuthSession{username, *token};
}

Result<AuthSession> AuthService::CreateGuestSession() {
    auto username = GenerateGuestName();
    if (!username) return std::unexpected(username.error());

    auto token = IssueToken(*username);
    if (!token) return std::unexpected(token.error());

    Logger::Info("[Auth] Guest session issued: " + *username);
    return AuthSession{*username, *token};
}

Result<std::string> AuthService::GenerateGuestName() {
    // INFO: 5 random base32 chars ≈ 33M names — enough entropy that two
    //       concurrent guests won't collide at this project's scale.
    static constexpr char kNameAlphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    static constexpr int  kNameLen        = 5;

    unsigned char raw[kNameLen];
    if (RAND_bytes(raw, kNameLen) != 1) {
        return std::unexpected(Error::Internal("[Auth] CSPRNG failure: RAND_bytes returned 0"));
    }

    std::string username = "Guest#";
    for (int i = 0; i < kNameLen; ++i) {
        username += kNameAlphabet[raw[i] % (sizeof(kNameAlphabet) - 1)];
    }
    return username;
}

// INFO: PBKDF2 is battle-tested and acceptable for this project scale. The
//       main knob is kIterations: more iterations = more CPU per guess =
//       slower brute force.
//
//       The "pepper" is an application-level secret from the environment.
//       Unlike a salt (stored in the DB, unique per user, public), a pepper
//       is *not* stored anywhere — it lives only in memory, loaded from the
//       env at startup. An attacker who steals the DB but not the server
//       config cannot run offline dictionary attacks even with the salts in
//       hand.
Result<std::string> AuthService::HashPassword(const std::string& password) {
    // INFO: 1. Generate a cryptographically random salt. RAND_bytes fills the
    //       buffer with random bytes from OpenSSL's CSPRNG.
    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), kSaltBytes) != 1) {
        return std::unexpected(Error::Internal("[Auth] CSPRNG failure: RAND_bytes returned 0"));
    }

    // INFO: 2. Read the pepper from the environment and append it to the
    //       password. std::string + std::string = concatenation; no UB here.
    std::string pepper         = Env::Get("PASSWORD_PEPPER", "");
    std::string peppered_pass  = password + pepper;

    // INFO: 3. Derive the hash.
    //          PKCS5_PBKDF2_HMAC(password, passlen,
    //                            salt, saltlen,
    //                            iterations,
    //                            digest,        ← EVP_sha256() here
    //                            keylen,
    //                            output_buffer)
    //          Returns 1 on success, 0 on failure.
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
        return std::unexpected(Error::Internal("[Auth] PBKDF2 failed"));
    }

    // INFO: 4. Encode both salt and hash to base64 and return as
    //       "<salt>:<hash>". Storing them together makes retrieval a single
    //       DB read.
    return Base64::Encode(salt) + ":" + Base64::Encode(hash);
}

bool AuthService::VerifyPassword(const std::string& password, const std::string& stored) {
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

    // INFO: CRYPTO_memcmp performs a constant-time comparison. A regular `==`
    //       on std::vector short-circuits on the first mismatch, which leaks
    //       timing information — a timing-side-channel attack can use that to
    //       infer how many bytes of the hash match. Returns 0 if equal (same
    //       convention as memcmp).
    return (CRYPTO_memcmp(candidate.data(), ref_hash.data(), kHashBytes) == 0);
}

// JWT — issue + verify
//
// INFO: jwt-cpp uses a fluent builder API. We create a JWT with:
//         - algorithm  : HS256 (HMAC-SHA256) — symmetric, using JWT_SECRET
//         - subject    : username (who this token represents)
//         - issued_at  : current UTC time (for audit / debugging)
//         - expires_at : now + 24 hours (after which the token is rejected)
//
//       The secret is fetched via Env::Require every call rather than cached
//       so that secret rotation (SIGHUP + Env::Load) works without restart.
//       In a hot path you'd cache it, but token issuance / verification
//       happen only at login and upgrade — not per-message.
Result<std::string> AuthService::IssueToken(const std::string& username) {
    using namespace std::chrono;

    std::string secret;
    try {
        secret = Env::Require("JWT_SECRET");
    } catch (const std::exception& e) {
        return std::unexpected(Error::Internal(e.what()));
    }

    auto now    = system_clock::now();
    auto expiry = now + hours(24);

    // INFO: jwt::create() returns a builder; each method returns *this for
    //       chaining. sign() finalises the builder and returns the encoded
    //       token string.
    return jwt::create<jwt::traits::nlohmann_json>()
        .set_type("JWT")
        .set_subject(username)
        .set_issued_at(now)
        .set_expires_at(expiry)
        .sign(jwt::algorithm::hs256{secret});
}

Result<JwtPayload> AuthService::VerifyToken(const std::string& token) {
    std::string secret;
    try {
        secret = Env::Require("JWT_SECRET");
    } catch (const std::exception& e) {
        return std::unexpected(Error::Internal(e.what()));
    }

    try {
        // INFO: jwt::verify() builds a verifier; verify(decoded) does the
        //       actual check. It throws jwt::token_verification_exception (or
        //       subclasses) on failure.
        auto verifier = jwt::verify<jwt::traits::nlohmann_json>()
            .allow_algorithm(jwt::algorithm::hs256{secret})
            // INFO: with_type("JWT") rejects tokens with a different typ header
            .with_type("JWT");

        // INFO: jwt::decode() parses the three dot-separated base64url
        //       segments. This step throws if the token is structurally
        //       malformed.
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);

        // INFO: This throws if signature is invalid, token is expired, etc.
        verifier.verify(decoded);

        JwtPayload payload;
        // INFO: get_subject() returns the "sub" claim as std::string.
        payload.username = decoded.get_subject();
        return payload;
    } catch (const std::exception& e) {
        // INFO: Collapse all jwt-cpp exceptions into our Error type so
        //       callers don't need to know about jwt-cpp internals.
        return std::unexpected(Error::Unauthorised(
            std::string("JWT verification failed: ") + e.what()));
    }
}
