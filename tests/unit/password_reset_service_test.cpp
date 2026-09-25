#include <doctest/doctest.h>
#include <services/password_reset_service.hpp>
#include <services/auth_service.hpp>
#include <common/crypto_hash.hpp>
#include <database.hpp>

#include <ctime>
#include <string>

namespace {
// reset_test_* usernames/emails avoid colliding with rows other test files insert.
void CleanupResetRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM users WHERE username LIKE 'reset_test_%';");
    REQUIRE(result.has_value());
}

struct ResetFixture {
    ResetFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupResetRows();
    }
    ~ResetFixture() { CleanupResetRows(); }
};

// Registers a fresh account and returns its username, ready for IssueTokenForEmail.
std::string RegisterResetUser(const std::string& username) {
    AuthService auth;
    auto result = auth.Register(username, username + "@example.com", "hunter22");
    REQUIRE(result.has_value());
    return username;
}

int UserIdFor(const std::string& username) {
    auto row = Database::Get().QueryOne("SELECT id FROM users WHERE username = ?;", {username});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    return row->value().Get<int>("id");
}

bool PasswordMatches(const std::string& username, const std::string& candidate) {
    auto row = Database::Get().QueryOne(
        "SELECT pass_hash, salt FROM users WHERE username = ?;", {username});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    std::string stored = row->value().Get<std::string>("salt") + ":" +
                         row->value().Get<std::string>("pass_hash");
    return AuthService::VerifyPassword(candidate, stored);
}
}  // namespace

TEST_SUITE("PasswordResetService") {

TEST_CASE("GenerateToken produces 43-char base64url tokens") {
    for (int i = 0; i < 100; ++i) {
        auto token = PasswordResetService::GenerateToken();
        REQUIRE(token.has_value());
        CHECK(token->size() == 43);
        for (char c : *token) {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_';
            CHECK(ok);
        }
    }
}

TEST_CASE("HashToken is deterministic SHA-256 hex") {
    CHECK(PasswordResetService::HashToken("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(PasswordResetService::HashToken("abc") != PasswordResetService::HashToken("abd"));
}

TEST_CASE("Sha256Hex matches a known vector and an empty input") {
    CHECK(Sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Hex("hello") ==
          "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST_CASE("IssueTokenForEmail stores a hash, never the plaintext") {
    ResetFixture f;
    RegisterResetUser("reset_test_user1");
    PasswordResetService svc;
    auto issued = svc.IssueTokenForEmail("reset_test_user1@example.com");
    REQUIRE(issued.has_value());

    auto row = Database::Get().QueryOne(
        "SELECT token_hash FROM password_reset_tokens WHERE user_id = ?;",
        {issued->user_id});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    const std::string stored = row->value().Get<std::string>("token_hash");
    CHECK(stored != issued->plaintext_token);
    CHECK(stored == PasswordResetService::HashToken(issued->plaintext_token));
}

TEST_CASE("unknown email returns NotFound") {
    ResetFixture f;
    PasswordResetService svc;
    auto result = svc.IssueTokenForEmail("reset_test_missing@example.com");
    REQUIRE(!result.has_value());
    CHECK(result.error().code == Error::Code::kNotFound);
}

TEST_CASE("Re-issuing replaces the previous token") {
    ResetFixture f;
    RegisterResetUser("reset_test_user2");
    PasswordResetService svc;
    auto first  = svc.IssueTokenForEmail("reset_test_user2@example.com");
    auto second = svc.IssueTokenForEmail("reset_test_user2@example.com");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    auto count = Database::Get().QueryOne(
        "SELECT COUNT(*) as c FROM password_reset_tokens WHERE user_id = ?;", {first->user_id});
    REQUIRE(count.has_value());
    REQUIRE(count->has_value());
    CHECK(count->value().Get<int>("c") == 1);

    auto stale = svc.ConsumeToken(first->plaintext_token, "newpassword1");
    CHECK(!stale.has_value());
}

TEST_CASE("Expired token is rejected") {
    ResetFixture f;
    RegisterResetUser("reset_test_user3");
    PasswordResetService svc;
    auto issued = svc.IssueTokenForEmail("reset_test_user3@example.com");
    REQUIRE(issued.has_value());

    auto expire = Database::Get().Exec(
        "UPDATE password_reset_tokens SET expires_at = ? WHERE user_id = ?;",
        {static_cast<int>(std::time(nullptr)) - 1, issued->user_id});
    REQUIRE(expire.has_value());

    auto result = svc.ConsumeToken(issued->plaintext_token, "newpassword1");
    REQUIRE(!result.has_value());
    CHECK(result.error().code == Error::Code::kUnauthorised);
}

TEST_CASE("Consume is single-use") {
    ResetFixture f;
    RegisterResetUser("reset_test_user4");
    PasswordResetService svc;
    auto issued = svc.IssueTokenForEmail("reset_test_user4@example.com");
    REQUIRE(issued.has_value());

    auto first = svc.ConsumeToken(issued->plaintext_token, "newpassword1");
    REQUIRE(first.has_value());
    CHECK(*first == "reset_test_user4");

    auto second = svc.ConsumeToken(issued->plaintext_token, "anotherpass1");
    REQUIRE(!second.has_value());
    CHECK(second.error().code == Error::Code::kUnauthorised);
}

TEST_CASE("Consume sets the new password and marks the account verified") {
    ResetFixture f;
    RegisterResetUser("reset_test_user5");
    PasswordResetService svc;
    auto issued = svc.IssueTokenForEmail("reset_test_user5@example.com");
    REQUIRE(issued.has_value());

    CHECK(PasswordMatches("reset_test_user5", "hunter22"));

    auto result = svc.ConsumeToken(issued->plaintext_token, "newpassword1");
    REQUIRE(result.has_value());

    CHECK(PasswordMatches("reset_test_user5", "newpassword1"));
    CHECK_FALSE(PasswordMatches("reset_test_user5", "hunter22"));

    auto row = Database::Get().QueryOne(
        "SELECT email_verified FROM users WHERE id = ?;", {issued->user_id});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    CHECK(row->value().Get<int>("email_verified") == 1);
}

TEST_CASE("Invalid token consume leaves no partial password change") {
    ResetFixture f;
    RegisterResetUser("reset_test_user6");
    PasswordResetService svc;

    auto result = svc.ConsumeToken("not-a-real-token", "newpassword1");
    REQUIRE(!result.has_value());

    // Original password still valid, nothing was written.
    CHECK(PasswordMatches("reset_test_user6", "hunter22"));
}

TEST_CASE("Consume bumps token_version, revoking prior sessions") {
    ResetFixture f;
    RegisterResetUser("reset_test_revoke");
    auto issued = PasswordResetService().IssueTokenForEmail("reset_test_revoke@example.com");
    REQUIRE(issued.has_value());
    auto old_token = AuthService::IssueToken("reset_test_revoke");
    REQUIRE(old_token.has_value());

    REQUIRE(PasswordResetService().ConsumeToken(issued->plaintext_token, "newpassword1").has_value());
    CHECK_FALSE(AuthService::VerifyToken(*old_token).has_value());
}

}  // TEST_SUITE
