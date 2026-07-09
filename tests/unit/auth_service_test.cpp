#include <doctest/doctest.h>
#include <services/auth_service.hpp>
#include <database.hpp>

#include <cstdlib>
#include <string>

namespace {
struct AuthEnvInit {
    AuthEnvInit() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
    }
} g_auth_env_init;

// auth_test_* usernames/emails avoid colliding with rows other test files insert.
void CleanupTestRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM users WHERE username LIKE 'auth_test_%';");
    REQUIRE(result.has_value());
}

struct AuthFixture {
    AuthFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupTestRows();
    }
    ~AuthFixture() { CleanupTestRows(); }
};
}  // namespace

TEST_SUITE("AuthService") {

TEST_CASE("HashPassword/VerifyPassword: correct password verifies") {
    auto stored = AuthService::HashPassword("hunter2");
    REQUIRE(stored.has_value());
    CHECK(AuthService::VerifyPassword("hunter2", *stored));
}

TEST_CASE("HashPassword/VerifyPassword: wrong password fails") {
    auto stored = AuthService::HashPassword("hunter2");
    REQUIRE(stored.has_value());
    CHECK_FALSE(AuthService::VerifyPassword("wrong-password", *stored));
}

TEST_CASE("HashPassword: same password hashes differently across calls (salted)") {
    auto first  = AuthService::HashPassword("hunter2");
    auto second = AuthService::HashPassword("hunter2");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first != *second);
    CHECK(AuthService::VerifyPassword("hunter2", *first));
    CHECK(AuthService::VerifyPassword("hunter2", *second));
}

TEST_CASE("VerifyPassword: malformed stored string (no colon) returns false") {
    CHECK_FALSE(AuthService::VerifyPassword("hunter2", "not-a-valid-stored-hash"));
}

TEST_CASE("IssueToken/VerifyToken: round-trip carries the username") {
    auto token = AuthService::IssueToken("alice");
    REQUIRE(token.has_value());
    auto payload = AuthService::VerifyToken(*token);
    REQUIRE(payload.has_value());
    CHECK(payload->username == "alice");
}

TEST_CASE("VerifyToken: syntactically invalid token yields an Error") {
    auto payload = AuthService::VerifyToken("not.a.jwt");
    CHECK_FALSE(payload.has_value());
}

TEST_CASE("VerifyToken: token signed with a different secret is rejected") {
    auto token = AuthService::IssueToken("bob");
    REQUIRE(token.has_value());

    setenv("JWT_SECRET", "a-completely-different-secret", 1);
    auto payload = AuthService::VerifyToken(*token);
    setenv("JWT_SECRET", "test-jwt-secret", 1);

    CHECK_FALSE(payload.has_value());
}

TEST_CASE("Register: valid username/password succeeds") {
    AuthFixture f;
    AuthService svc;
    auto result = svc.Register("auth_test_alice", "auth_test_alice@example.com", "hunter22");
    CHECK(result.has_value());
}

TEST_CASE("Register: password too short is rejected") {
    AuthFixture f;
    AuthService svc;
    auto result = svc.Register("auth_test_bob", "auth_test_bob@example.com", "short");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Error::Code::kInvalidInput);
}

TEST_CASE("Register: duplicate username is rejected") {
    AuthFixture f;
    AuthService svc;
    REQUIRE(svc.Register("auth_test_carol", "auth_test_carol@example.com", "hunter22").has_value());
    auto result = svc.Register("auth_test_carol", "auth_test_carol2@example.com", "hunter22");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Error::Code::kConflict);
}

TEST_CASE("Register: reserved bot-prefixed username is rejected") {
    AuthFixture f;
    AuthService svc;
    auto result = svc.Register("bot_dan", "auth_test_dan@example.com", "hunter22");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Error::Code::kInvalidInput);
}

TEST_CASE("Login: correct credentials issue a session token") {
    AuthFixture f;
    AuthService svc;
    REQUIRE(svc.Register("auth_test_erin", "auth_test_erin@example.com", "hunter22").has_value());

    auto session = svc.Login("auth_test_erin@example.com", "hunter22", "127.0.0.1");
    REQUIRE(session.has_value());
    CHECK(session->username == "auth_test_erin");
    CHECK_FALSE(session->token.empty());
}

TEST_CASE("Login: wrong password is rejected and does not leak the DB error path") {
    AuthFixture f;
    AuthService svc;
    REQUIRE(svc.Register("auth_test_frank", "auth_test_frank@example.com", "hunter22").has_value());

    auto session = svc.Login("auth_test_frank@example.com", "wrong-password", "127.0.0.1");
    REQUIRE_FALSE(session.has_value());
    CHECK(session.error().code == Error::Code::kUnauthorised);
}

TEST_CASE("Login: unknown email is rejected the same way as a wrong password") {
    AuthFixture f;
    AuthService svc;
    auto session = svc.Login("auth_test_ghost@example.com", "hunter22", "127.0.0.1");
    REQUIRE_FALSE(session.has_value());
    CHECK(session.error().code == Error::Code::kUnauthorised);
}

TEST_CASE("Login: repeated failures trip the per-(email,ip) throttle") {
    AuthFixture f;
    setenv("LOGIN_MAX_FAILS", "3", 1);
    setenv("LOGIN_LOCKOUT_SEC", "300", 1);
    AuthService svc;
    REQUIRE(svc.Register("auth_test_henry", "auth_test_henry@example.com", "hunter22").has_value());

    for (int i = 0; i < 3; ++i) {
        auto session = svc.Login("auth_test_henry@example.com", "wrong-password", "10.0.0.1");
        REQUIRE_FALSE(session.has_value());
        CHECK(session.error().code == Error::Code::kUnauthorised);
    }

    auto locked = svc.Login("auth_test_henry@example.com", "hunter22", "10.0.0.1");
    REQUIRE_FALSE(locked.has_value());
    CHECK(locked.error().code == Error::Code::kTooManyRequests);
}

TEST_CASE("CreateGuestSession: issues a token for a generated Guest# name") {
    AuthService svc;
    auto session = svc.CreateGuestSession();
    REQUIRE(session.has_value());
    CHECK(session->username.starts_with("Guest#"));
    CHECK_FALSE(session->token.empty());

    auto payload = AuthService::VerifyToken(session->token);
    REQUIRE(payload.has_value());
    CHECK(payload->username == session->username);
}

}  // TEST_SUITE
