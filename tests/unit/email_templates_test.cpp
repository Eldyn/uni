#include <doctest/doctest.h>
#include <common/email_templates.hpp>

TEST_SUITE("EmailTemplates") {

TEST_CASE("RenderVerifyEmail contains code in both html and text") {
    VerifyEmailData d{"alice", "123456", "https://playuni.app/profile/verify/123456", "en"};
    auto mail = RenderVerifyEmail(d);
    CHECK(mail.html_body.find("123456") != std::string::npos);
    CHECK(mail.text_body.find("123456") != std::string::npos);
    CHECK(mail.html_body.find("<link") == std::string::npos);
    CHECK(mail.html_body.find("<style") == std::string::npos);
}

TEST_CASE("RenderVerifyEmail localizes subject") {
    VerifyEmailData en{"alice", "123456", "https://x/123456", "en"};
    VerifyEmailData it{"alice", "123456", "https://x/123456", "it"};
    CHECK(RenderVerifyEmail(en).subject != RenderVerifyEmail(it).subject);
}

TEST_CASE("Unknown locale falls back to English") {
    VerifyEmailData d{"alice", "123456", "https://x/123456", "xx"};
    auto mail = RenderVerifyEmail(d);
    CHECK(mail.subject == RenderVerifyEmail({"alice", "123456", "https://x/123456", "en"}).subject);
}

TEST_CASE("RenderResetEmail contains the magic link in both html and text") {
    ResetEmailData d{"alice", "https://playuni.app/reset-password/tok123", "en"};
    auto mail = RenderResetEmail(d);
    CHECK(mail.html_body.find("https://playuni.app/reset-password/tok123") != std::string::npos);
    CHECK(mail.text_body.find("https://playuni.app/reset-password/tok123") != std::string::npos);
    CHECK(mail.html_body.find("<link") == std::string::npos);
    CHECK(mail.html_body.find("<style") == std::string::npos);
    // No numeric code block for reset mail.
    CHECK(mail.html_body.find("letter-spacing:10px") == std::string::npos);
}

TEST_CASE("RenderResetEmail localizes subject") {
    ResetEmailData en{"alice", "https://x/tok", "en"};
    ResetEmailData it{"alice", "https://x/tok", "it"};
    CHECK(RenderResetEmail(en).subject != RenderResetEmail(it).subject);
}

TEST_CASE("RenderResetEmail unknown locale falls back to English") {
    ResetEmailData d{"alice", "https://x/tok", "xx"};
    CHECK(RenderResetEmail(d).subject ==
          RenderResetEmail({"alice", "https://x/tok", "en"}).subject);
}

TEST_CASE("BuildResetMagicLink uses the reset-password path") {
    auto link = BuildResetMagicLink("abc123");
    CHECK(link.ends_with("/reset-password/abc123"));
}

}
