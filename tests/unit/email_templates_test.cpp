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

}
