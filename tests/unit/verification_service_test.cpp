#include <doctest/doctest.h>
#include <services/verification_service.hpp>
#include <services/auth_service.hpp>
#include <database.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>

namespace {
// verify_test_* usernames/emails avoid colliding with rows other test files insert.
void CleanupTestRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM users WHERE username LIKE 'verify_test_%';");
    REQUIRE(result.has_value());
}

struct VerifyFixture {
    VerifyFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupTestRows();
    }
    ~VerifyFixture() { CleanupTestRows(); }
};

// Registers a fresh account and returns its username, ready for IssueCode.
std::string RegisterTestUser(const std::string& username) {
    AuthService auth;
    auto result = auth.Register(username, username + "@example.com", "hunter22");
    REQUIRE(result.has_value());
    return username;
}
}  // namespace

TEST_SUITE("VerificationService") {

TEST_CASE("GenerateCode produces 6-digit codes") {
    for (int i = 0; i < 1000; ++i) {
        auto code = VerificationService::GenerateCode();
        REQUIRE(code.has_value());
        CHECK(code->size() == 6);
        CHECK(std::all_of(code->begin(), code->end(), ::isdigit));
    }
}

TEST_CASE("HashCode is deterministic") {
    CHECK(VerificationService::HashCode("123456") == VerificationService::HashCode("123456"));
    CHECK(VerificationService::HashCode("123456") != VerificationService::HashCode("654321"));
}

TEST_CASE("IssueCode then ConfirmCode with correct code succeeds") {
    VerifyFixture f;
    RegisterTestUser("verify_test_user1");
    VerificationService svc;
    auto issued = svc.IssueCode("verify_test_user1");
    REQUIRE(issued.has_value());
    auto result = svc.ConfirmCode(issued->user_id, issued->plaintext_code);
    CHECK(result.has_value());
    auto row = Database::Get().QueryOne("SELECT email_verified FROM users WHERE id = ?;", {issued->user_id});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    CHECK(row->value().Get<int>("email_verified") == 1);
}

TEST_CASE("Re-issuing a code invalidates the previous one") {
    VerifyFixture f;
    RegisterTestUser("verify_test_user2");
    VerificationService svc;
    auto first = svc.IssueCode("verify_test_user2");
    auto second = svc.IssueCode("verify_test_user2");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    auto count = Database::Get().QueryOne(
        "SELECT COUNT(*) as c FROM email_verification_codes WHERE user_id = ?;", {first->user_id});
    REQUIRE(count.has_value());
    REQUIRE(count->has_value());
    CHECK(count->value().Get<int>("c") == 1);
    auto stale = svc.ConfirmCode(first->user_id, first->plaintext_code);
    CHECK(!stale.has_value());
}

TEST_CASE("5 wrong attempts then correct code fails with TooManyRequests, row gone") {
    VerifyFixture f;
    RegisterTestUser("verify_test_user3");
    VerificationService svc;
    auto issued = svc.IssueCode("verify_test_user3");
    REQUIRE(issued.has_value());
    for (int i = 0; i < 5; ++i) {
        auto r = svc.ConfirmCode(issued->user_id, "000000");
        CHECK(!r.has_value());
    }
    auto final_try = svc.ConfirmCode(issued->user_id, issued->plaintext_code);
    REQUIRE(!final_try.has_value());
    CHECK(final_try.error().code == Error::Code::kTooManyRequests);
}

TEST_CASE("Expired code is rejected") {
    VerifyFixture f;
    RegisterTestUser("verify_test_user4");
    VerificationService svc;
    auto issued = svc.IssueCode("verify_test_user4");
    REQUIRE(issued.has_value());
    auto expire = Database::Get().Exec(
        "UPDATE email_verification_codes SET expires_at = ? WHERE user_id = ?;",
        {static_cast<int>(std::time(nullptr)) - 1, issued->user_id});
    REQUIRE(expire.has_value());
    auto result = svc.ConfirmCode(issued->user_id, issued->plaintext_code);
    CHECK(!result.has_value());
}

}  // TEST_SUITE
