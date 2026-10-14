#include <doctest/doctest.h>
#include <services/auth_service.hpp>
#include <database.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>

namespace {
struct CacheEnvInit {
    CacheEnvInit() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
    }
} g_cache_env_init;

// cache_test_* usernames avoid colliding with rows other test files insert.
void CleanupCacheRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM users WHERE username LIKE 'cache_test_%';");
    REQUIRE(result.has_value());
}

struct CacheFixture {
    CacheFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        AuthService::ClearTokenVersionCacheForTests();
        CleanupCacheRows();
    }
    ~CacheFixture() {
        AuthService::ClearTokenVersionCacheForTests();
        CleanupCacheRows();
    }
};
}  // namespace

TEST_CASE("token version cached, invalidated on revoke") {
    CacheFixture f;
    AuthService auth;
    REQUIRE(auth.Register("cache_test_user", "cache_test_user@example.com",
                          "hunter22")
                .has_value());

    auto token = AuthService::IssueToken("cache_test_user");
    REQUIRE(token.has_value());

    // Isolate the miss/hit measurement from the IssueToken read above.
    AuthService::ClearTokenVersionCacheForTests();
    const std::uint64_t hits_before = AuthService::TokenVersionCacheHitsForTests();

    // First verify: miss — serves from the DB, consumes no cache hit.
    auto first = AuthService::VerifyToken(*token);
    REQUIRE(first.has_value());
    CHECK(first->username == "cache_test_user");
    CHECK(AuthService::TokenVersionCacheHitsForTests() == hits_before);

    // Second verify: hit — zero DB hops, so the worker's prepared-statement
    // reuse counter cannot move while the cache hit counter advances by one.
    const std::uint64_t stmt_before = Database::Get().StmtCacheHitsForTests();
    auto second = AuthService::VerifyToken(*token);
    REQUIRE(second.has_value());
    CHECK(AuthService::TokenVersionCacheHitsForTests() == hits_before + 1);
    CHECK(Database::Get().StmtCacheHitsForTests() == stmt_before);

    // Revoke write-through-invalidates: the next verify re-reads the bumped
    // version (no hit consumed) and rejects the stale token.
    const std::uint64_t hits_pre_revoke = AuthService::TokenVersionCacheHitsForTests();
    REQUIRE(auth.RevokeAllSessions("cache_test_user").has_value());
    auto after = AuthService::VerifyToken(*token);
    CHECK_FALSE(after.has_value());
    CHECK(AuthService::TokenVersionCacheHitsForTests() == hits_pre_revoke);
}
