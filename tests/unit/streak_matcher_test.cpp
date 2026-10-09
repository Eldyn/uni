#include <doctest/doctest.h>

#include <match/modload/artifacts.hpp>
#include <match/streak/matcher.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

/**
 * @file streak_matcher_test.cpp
 * @brief The streak matcher: equals, in, exists, has_tag, negate, state
 *        values, and the one test it refuses (relation).
 */

using match::modload::StreakMatcherDef;
using match::modload::StreakTestDef;
using match::streak::ClientEventView;
using match::streak::MatcherMatches;
using nlohmann::json;

namespace {

ClientEventView SkipEvent(const std::string& target) {
    ClientEventView view;
    view.id = "turn_skipped";
    view.payload = json{{"target", target}, {"count", 2}};
    view.actor = "ann";
    view.state.turn = 5;
    view.state.round = 1;
    view.state.hand_size = 3;
    return view;
}

StreakMatcherDef Matcher(const std::string& event,
                         std::vector<StreakTestDef> tests = {}) {
    StreakMatcherDef matcher;
    matcher.event = event;
    matcher.tests = std::move(tests);
    return matcher;
}

StreakTestDef Test(const std::string& word) {
    StreakTestDef test;
    test.test = word;
    return test;
}

}  // namespace

TEST_CASE("matcher: the event id must be the same") {
    CHECK(MatcherMatches(Matcher("turn_skipped"), SkipEvent("bob")));
    CHECK_FALSE(MatcherMatches(Matcher("direction_changed"), SkipEvent("bob")));
}

TEST_CASE("matcher: equals compares a payload field") {
    StreakTestDef test = Test("equals");
    test.field = "target";
    test.value = "bob";
    CHECK(MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("cat")));
    test.field = "count";
    test.value = 2;
    CHECK(MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
}

TEST_CASE("matcher: in looks for the field value in a list") {
    StreakTestDef test = Test("in");
    test.field = "target";
    test.values = json::array({"bob", "cat"});
    CHECK(MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("cat")));
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("dan")));
}

TEST_CASE("matcher: exists needs the field in the payload") {
    StreakTestDef test = Test("exists");
    test.field = "count";
    CHECK(MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
    test.field = "secret";
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
}

TEST_CASE("matcher: a field that the view lacks never matches equals") {
    StreakTestDef test = Test("equals");
    test.field = "secret";
    test.value = nullptr;
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
    test.negate = true;
    CHECK(MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
}

TEST_CASE("matcher: has_tag reads the tags of the played card") {
    ClientEventView view;
    view.id = "card_played";
    view.payload = json{{"tags", json::array({"colored", "uni:skip"})}};
    StreakTestDef test = Test("has_tag");
    test.tag = "uni:skip";
    CHECK(MatcherMatches(Matcher("card_played", {test}), view));
    test.tag = "uni:reverse";
    CHECK_FALSE(MatcherMatches(Matcher("card_played", {test}), view));
    test.negate = true;
    CHECK(MatcherMatches(Matcher("card_played", {test}), view));
}

TEST_CASE("matcher: has_tag is false when the event carries no tags") {
    StreakTestDef test = Test("has_tag");
    test.tag = "uni:skip";
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
}

TEST_CASE("matcher: a state value is read from the closed list") {
    StreakTestDef test = Test("equals");
    test.state = "turn";
    test.value = 5;
    CHECK(MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
    test.state = "hand_size";
    test.value = 3;
    CHECK(MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
    test.state = "round";
    test.value = 2;
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
    test.state = "no_such_value";
    test.value = 0;
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
}

TEST_CASE("matcher: a missing hand size never matches") {
    ClientEventView view = SkipEvent("bob");
    view.state.hand_size.reset();
    StreakTestDef test = Test("equals");
    test.state = "hand_size";
    test.value = 0;
    CHECK_FALSE(MatcherMatches(Matcher("turn_skipped", {test}), view));
}

TEST_CASE("matcher: every test must hold") {
    StreakTestDef first = Test("equals");
    first.field = "target";
    first.value = "bob";
    StreakTestDef second = Test("equals");
    second.field = "count";
    second.value = 9;
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {first, second}),
                       SkipEvent("bob")));
}

TEST_CASE("matcher: relation never matches, whatever the viewer") {
    StreakTestDef test = Test("relation");
    test.relation = "self";
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
    test.negate = true;
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
}

TEST_CASE("matcher: an unknown test never matches") {
    StreakTestDef test = Test("greater_than");
    test.field = "count";
    test.value = 1;
    CHECK_FALSE(
        MatcherMatches(Matcher("turn_skipped", {test}), SkipEvent("bob")));
}
