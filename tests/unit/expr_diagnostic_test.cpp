#include <doctest/doctest.h>

#include <match/expr/diagnostic.hpp>

#include <chrono>
#include <string>
#include <vector>

TEST_SUITE("expr diagnostics") {

TEST_CASE("format_with_suggestion") {
    match::expr::Diagnostic d{"mods/a/rules/x.json", "/rule/when",
                              "unknown property \"hand_sze\" on seat", "hand_size"};
    const std::string expected =
        "mods/a/rules/x.json#/rule/when: unknown property \"hand_sze\" on seat "
        "(did you mean \"hand_size\")";
    CHECK(match::expr::FormatDiagnostic(d) == expected);
}

TEST_CASE("format_without_suggestion") {
    match::expr::Diagnostic d{"file", "/p", "message", ""};
    CHECK(match::expr::FormatDiagnostic(d) == "file#/p: message");
}

TEST_CASE("an_empty_key_token_keeps_its_separator") {
    CHECK(match::expr::AppendPointerToken(match::expr::kRootPointer, "") == "//");
    CHECK(match::expr::AppendPointerToken("", "") == "//");
    CHECK(match::expr::AppendPointerToken("/a", "") == "/a/");
    CHECK(match::expr::AppendPointerToken(
              match::expr::AppendPointerToken(match::expr::kRootPointer, ""), "b") == "//b");
}

TEST_CASE("suggest_closest_finds_typo") {
    CHECK(match::expr::SuggestClosest("hand_sze", {"hand_size", "seat", "name"}) == "hand_size");
}

TEST_CASE("suggest_closest_rejects_far_word") {
    CHECK(match::expr::SuggestClosest("zzz", {"hand_size", "seat"}) == "");
}

TEST_CASE("suggest_closest_keeps_a_candidate_of_a_near_length") {
    CHECK(match::expr::SuggestClosest("sea", {"seat"}) == "seat");
}

TEST_CASE("suggest_closest_skips_a_candidate_of_a_far_length") {
    CHECK(match::expr::SuggestClosest("hand", {"hand_size"}) == "");
}

TEST_CASE("a_huge_word_against_many_candidates_stays_fast") {
    const std::string word(64 * 1024, 'x');
    std::vector<std::string> candidates;
    candidates.reserve(20000);
    for (int index = 0; index < 20000; ++index) {
        candidates.push_back("hand_size_" + std::to_string(index));
    }
    const auto started = std::chrono::steady_clock::now();
    CHECK(match::expr::SuggestClosest(word, candidates) == "");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < std::chrono::seconds(1));
}

}  // TEST_SUITE
