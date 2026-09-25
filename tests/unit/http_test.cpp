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

TEST_CASE("IsAllowedWsOrigin: empty allowlist requires same-origin") {
    CHECK(http::IsAllowedWsOrigin("https://playuni.app", "playuni.app", ""));
    CHECK_FALSE(http::IsAllowedWsOrigin("https://evil.example", "playuni.app", ""));
}

TEST_CASE("IsAllowedWsOrigin: explicit allowlist is honored") {
    CHECK(http::IsAllowedWsOrigin("https://itch.io", "playuni.app",
                                  "https://playuni.app,https://itch.io"));
    CHECK_FALSE(http::IsAllowedWsOrigin("https://evil.example", "playuni.app",
                                        "https://playuni.app,https://itch.io"));
}

TEST_CASE("IsAllowedWsOrigin: non-browser clients (no Origin) are allowed") {
    CHECK(http::IsAllowedWsOrigin("", "playuni.app", ""));
}

TEST_CASE("HasForbiddenDotSegment allows .well-known but denies other dot paths") {
    CHECK_FALSE(http::HasForbiddenDotSegment(".well-known/atproto-did"));
    CHECK(http::HasForbiddenDotSegment(".env"));
    CHECK(http::HasForbiddenDotSegment("assets/.git/config"));
    CHECK_FALSE(http::HasForbiddenDotSegment("assets/app.js"));
}

#ifndef _WIN32
TEST_CASE("IpInIpv4Cidr matches inside the range and rejects outside") {
    CHECK(http::IpInIpv4Cidr("172.16.0.1", "172.16.0.0/12"));
    CHECK(http::IpInIpv4Cidr("172.31.255.254", "172.16.0.0/12"));
    CHECK_FALSE(http::IpInIpv4Cidr("172.32.0.1", "172.16.0.0/12"));
    CHECK_FALSE(http::IpInIpv4Cidr("192.168.1.1", "172.16.0.0/12"));
}

TEST_CASE("IpInIpv4Cidr honours /32 exact matches") {
    CHECK(http::IpInIpv4Cidr("10.0.0.7", "10.0.0.7/32"));
    CHECK_FALSE(http::IpInIpv4Cidr("10.0.0.8", "10.0.0.7/32"));
}

TEST_CASE("IpInIpv4Cidr /0 matches the whole space") {
    CHECK(http::IpInIpv4Cidr("203.0.113.9", "0.0.0.0/0"));
}

TEST_CASE("IpInIpv4Cidr fails closed on a malformed prefix") {
    CHECK_FALSE(http::IpInIpv4Cidr("172.16.0.1", "172.16.0.0/"));
    CHECK_FALSE(http::IpInIpv4Cidr("172.16.0.1", "172.16.0.0/x"));
    CHECK_FALSE(http::IpInIpv4Cidr("172.16.0.1", "172.16.0.0/-1"));
}

TEST_CASE("IpInIpv4Cidr rejects a non-IPv4 address") {
    CHECK_FALSE(http::IpInIpv4Cidr("2001:db8::1", "172.16.0.0/12"));
    CHECK_FALSE(http::IpInIpv4Cidr("not-an-ip", "172.16.0.0/12"));
}

TEST_CASE("ResolveClientIp honours XFF only from a trusted peer") {
    // Trusted peer: the right-most (proxy-observed) XFF entry wins, trimmed.
    CHECK(http::ResolveClientIp("172.16.0.5", "203.0.113.7", "172.16.0.0/12") ==
          "203.0.113.7");
    CHECK(http::ResolveClientIp("172.16.0.5", "198.51.100.9, 203.0.113.7",
                                "172.16.0.0/12") == "203.0.113.7");
    // Untrusted peer, empty allowlist, or malformed CIDR: XFF is ignored.
    CHECK(http::ResolveClientIp("203.0.113.99", "203.0.113.7", "172.16.0.0/12") ==
          "203.0.113.99");
    CHECK(http::ResolveClientIp("172.16.0.5", "203.0.113.7", "") == "172.16.0.5");
    CHECK(http::ResolveClientIp("172.16.0.5", "203.0.113.7", "172.16.0.0/") ==
          "172.16.0.5");
}

TEST_CASE("ResolveClientIp unwraps a mapped-IPv6 peer before the CIDR check") {
    // uWS reports IPv4 peers as ::ffff:x.x.x.x on a dual-stack socket; the
    // mapped form must still match an IPv4 trusted CIDR.
    CHECK(http::ResolveClientIp("::ffff:172.16.5.5", "203.0.113.7",
                                "172.16.0.0/12") == "203.0.113.7");
    CHECK(http::ResolveClientIp("172.16.5.5", "203.0.113.7", "172.16.0.0/12") ==
          "203.0.113.7");
}
#endif  // !_WIN32
