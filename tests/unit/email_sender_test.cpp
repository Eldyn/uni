#include <doctest/doctest.h>
#include <common/email_sender.hpp>

#include <filesystem>
#include <cstdlib>

TEST_SUITE("EmailSender") {

TEST_CASE("DevFileEmailSender writes html and txt files, no network") {
    auto tmp = std::filesystem::temp_directory_path() / "email_test";
    DevFileEmailSender sender(tmp.string());
    OutboundEmail mail{"user@example.com", "User", "Subject", "<p>html</p>", "text"};
    auto result = sender.Send(mail);
    CHECK(result.has_value());
    bool found_html = false, found_txt = false;
    for (auto& entry : std::filesystem::directory_iterator(tmp)) {
        if (entry.path().extension() == ".html") found_html = true;
        if (entry.path().extension() == ".txt") found_txt = true;
    }
    CHECK(found_html);
    CHECK(found_txt);
}

TEST_CASE("DevFileEmailSender sanitizes to_address, no path traversal") {
    auto tmp = std::filesystem::temp_directory_path() / "email_test_traversal";
    std::filesystem::remove_all(tmp);
    DevFileEmailSender sender(tmp.string());
    OutboundEmail mail{"../../etc/passwd", "User", "Subject", "<p>html</p>", "text"};
    auto result = sender.Send(mail);
    CHECK(result.has_value());

    bool found_html = false, found_txt = false;
    for (auto& entry : std::filesystem::directory_iterator(tmp)) {
        CHECK(std::filesystem::equivalent(entry.path().parent_path(), tmp));
        if (entry.path().extension() == ".html") found_html = true;
        if (entry.path().extension() == ".txt") found_txt = true;
    }
    CHECK(found_html);
    CHECK(found_txt);

    // Nothing should have been written outside tmp (e.g. no "etc" dir created
    // as a sibling/parent by an unsanitized "../../etc/passwd" stem).
    CHECK_FALSE(std::filesystem::exists(tmp.parent_path() / "etc"));
}

TEST_CASE("MakeEmailSender defaults to dev sender") {
    setenv("EMAIL_MODE", "", 1);
    auto sender = MakeEmailSender();
    CHECK(dynamic_cast<DevFileEmailSender*>(sender.get()) != nullptr);
}

TEST_CASE("MakeEmailSender falls back to dev when live has no key") {
    setenv("EMAIL_MODE", "live", 1);
    setenv("EMAIL_KEY", "", 1);
    auto sender = MakeEmailSender();
    CHECK(dynamic_cast<DevFileEmailSender*>(sender.get()) != nullptr);
    unsetenv("EMAIL_MODE");
}

}  // TEST_SUITE
