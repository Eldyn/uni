#include <doctest/doctest.h>
#include <services/auth_service.hpp>

#include <cstdlib>
#include <string>

namespace {
struct AuthEnvInit {
    AuthEnvInit() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
    }
} g_auth_env_init;
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

}  // TEST_SUITE
