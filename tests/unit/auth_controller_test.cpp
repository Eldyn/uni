#include <doctest/doctest.h>
#include <controllers/auth_controller.hpp>

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

TEST_SUITE("AuthController") {

TEST_CASE("HashPassword/VerifyPassword: correct password verifies") {
    std::string stored = AuthController::HashPassword("hunter2");
    CHECK(AuthController::VerifyPassword("hunter2", stored));
}

TEST_CASE("HashPassword/VerifyPassword: wrong password fails") {
    std::string stored = AuthController::HashPassword("hunter2");
    CHECK_FALSE(AuthController::VerifyPassword("wrong-password", stored));
}

TEST_CASE("HashPassword: same password hashes differently across calls (salted)") {
    std::string first  = AuthController::HashPassword("hunter2");
    std::string second = AuthController::HashPassword("hunter2");
    CHECK(first != second);
    CHECK(AuthController::VerifyPassword("hunter2", first));
    CHECK(AuthController::VerifyPassword("hunter2", second));
}

TEST_CASE("VerifyPassword: malformed stored string (no colon) returns false") {
    CHECK_FALSE(AuthController::VerifyPassword("hunter2", "not-a-valid-stored-hash"));
}

TEST_CASE("IssueToken/VerifyToken: round-trip carries the username") {
    std::string token = AuthController::IssueToken("alice");
    auto payload = AuthController::VerifyToken(token);
    REQUIRE(payload.has_value());
    CHECK(payload->username == "alice");
}

TEST_CASE("VerifyToken: syntactically invalid token yields an Error") {
    auto payload = AuthController::VerifyToken("not.a.jwt");
    CHECK_FALSE(payload.has_value());
}

TEST_CASE("VerifyToken: token signed with a different secret is rejected") {
    std::string token = AuthController::IssueToken("bob");

    setenv("JWT_SECRET", "a-completely-different-secret", 1);
    auto payload = AuthController::VerifyToken(token);
    setenv("JWT_SECRET", "test-jwt-secret", 1);

    CHECK_FALSE(payload.has_value());
}

}  // TEST_SUITE
