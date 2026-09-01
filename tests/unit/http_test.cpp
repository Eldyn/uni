#include <doctest/doctest.h>
#include "common/http.hpp"
#include "common/http_utils.hpp"

TEST_CASE("UnwrapIpv4MappedIpv6 strips the ::ffff: prefix") {
    CHECK(http::UnwrapIpv4MappedIpv6("::ffff:127.0.0.1") == "127.0.0.1");
    CHECK(http::UnwrapIpv4MappedIpv6("::ffff:203.0.113.42") == "203.0.113.42");
}

TEST_CASE("UnwrapIpv4MappedIpv6 leaves genuine IPv6 addresses untouched") {
    CHECK(http::UnwrapIpv4MappedIpv6("::1") == "::1");
    CHECK(http::UnwrapIpv4MappedIpv6("2001:db8::1") == "2001:db8::1");
    CHECK(http::UnwrapIpv4MappedIpv6("fe80::1234:5678") == "fe80::1234:5678");
}

TEST_CASE("UnwrapIpv4MappedIpv6 leaves plain IPv4 and empty input untouched") {
    CHECK(http::UnwrapIpv4MappedIpv6("127.0.0.1") == "127.0.0.1");
    CHECK(http::UnwrapIpv4MappedIpv6("") == "");
}

TEST_CASE("IsClientRoute accepts a bare route with no extension") {
    CHECK(http::IsClientRoute("browse"));
    CHECK(http::IsClientRoute("profile/stats"));
    CHECK(http::IsClientRoute("profile/stats/all"));
}

TEST_CASE("IsClientRoute rejects anything that looks like a real asset") {
    CHECK_FALSE(http::IsClientRoute("assets/index-8FzcvdAa.js"));
    CHECK_FALSE(http::IsClientRoute("favicon.ico"));
    CHECK_FALSE(http::IsClientRoute("fonts/pixel.woff2"));
    CHECK_FALSE(http::IsClientRoute("about.html"));
}

TEST_CASE("IsClientRoute rejects a path with a dot anywhere in its final segment") {
    CHECK_FALSE(http::IsClientRoute("some.weird.path"));
}

TEST_CASE("IsClientRoute rejects the empty path") {
    CHECK_FALSE(http::IsClientRoute(""));
}
