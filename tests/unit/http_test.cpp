#include <doctest/doctest.h>
#include "common/http.hpp"

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
