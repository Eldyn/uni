#include <doctest/doctest.h>

#include <match/expr/diagnostic.hpp>

#include <string>
#include <vector>

TEST_SUITE("expr diagnostics") {

TEST_CASE("format_with_suggestion") {
    match::expr::Diagnostic d{"mods/a/rules/x.json", "/rule/when",
                              "unknown property \"hand_sze\" on seat", "hand_size"};
    CHECK(match::expr::FormatDiagnostic(d) ==
          "mods/a/rules/x.json#/rule/when: unknown property \"hand_sze\" on seat (did you mean \"hand_size\")");
}

TEST_CASE("format_without_suggestion") {
    match::expr::Diagnostic d{"file", "/p", "message", ""};
    CHECK(match::expr::FormatDiagnostic(d) == "file#/p: message");
}

TEST_CASE("suggest_closest_finds_typo") {
    CHECK(match::expr::SuggestClosest("hand_sze", {"hand_size", "seat", "name"}) == "hand_size");
}

TEST_CASE("suggest_closest_rejects_far_word") {
    CHECK(match::expr::SuggestClosest("zzz", {"hand_size", "seat"}) == "");
}

}  // TEST_SUITE