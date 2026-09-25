#include <doctest/doctest.h>
#include <common/boot_config.hpp>
#include <cstdlib>

TEST_SUITE("BootConfig") {
TEST_CASE("rejects a short JWT secret") {
    setenv("JWT_SECRET", "short", 1);
    setenv("PASSWORD_PEPPER", "pepper", 1);
    setenv("EMAIL_MODE", "dev", 1);
    CHECK_THROWS_AS(ValidateBootConfigOrThrow(), std::runtime_error);
}
TEST_CASE("accepts a dev configuration") {
    setenv("JWT_SECRET", std::string(32, 'a').c_str(), 1);
    setenv("PASSWORD_PEPPER", "pepper", 1);
    setenv("EMAIL_MODE", "dev", 1);
    unsetenv("UNI_ENV");
    CHECK_NOTHROW(ValidateBootConfigOrThrow());
}
TEST_CASE("rejects production without live email") {
    setenv("JWT_SECRET", std::string(32, 'a').c_str(), 1);
    setenv("PASSWORD_PEPPER", "pepper", 1);
    setenv("EMAIL_MODE", "dev", 1);
    setenv("UNI_ENV", "production", 1);
    CHECK_THROWS_AS(ValidateBootConfigOrThrow(), std::runtime_error);
    unsetenv("UNI_ENV");
}
}  // TEST_SUITE
