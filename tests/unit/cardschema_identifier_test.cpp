#include <doctest/doctest.h>

#include <map>
#include <string>

#include <match/cardschema/identifier.hpp>
#include <match/cardschema/model.hpp>

using match::cardschema::CanonicalizeSymmetric;
using match::cardschema::KindTemplate;
using match::cardschema::SameCard;

TEST_SUITE("cardschema identifier") {

TEST_CASE("canonical_sorts_group") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right"}};

    const std::map<std::string, std::string> members = {{"left", "green"}, {"right", "blue"}};
    const std::map<std::string, std::string> canonical = CanonicalizeSymmetric(kind, members);

    const std::map<std::string, std::string> expected = {{"left", "blue"}, {"right", "green"}};
    CHECK(canonical == expected);
}

TEST_CASE("same_card_both_orders") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right"}};

    CHECK(SameCard(kind, {{"left", "green"}, {"right", "blue"}},
                   {{"left", "blue"}, {"right", "green"}}));
}

TEST_CASE("non_symmetric_slots_untouched") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right"}};

    const std::map<std::string, std::string> members = {
        {"value", "5"}, {"left", "green"}, {"right", "blue"}};
    const std::map<std::string, std::string> canonical = CanonicalizeSymmetric(kind, members);

    CHECK(canonical.at("value") == "5");
    CHECK(canonical.at("left") == "blue");
    CHECK(canonical.at("right") == "green");
}

TEST_CASE("no_group_is_identity") {
    KindTemplate kind;
    kind.identifier = "uni:card";

    const std::map<std::string, std::string> members = {{"left", "green"}, {"right", "blue"}};
    CHECK(CanonicalizeSymmetric(kind, members) == members);
}

TEST_CASE("canonical_three_slot_group_assigns_sorted_members") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"a", "b", "c"}};

    const std::map<std::string, std::string> members = {{"a", "c"}, {"b", "a"}, {"c", "b"}};
    const std::map<std::string, std::string> expected = {{"a", "a"}, {"b", "b"}, {"c", "c"}};
    CHECK(CanonicalizeSymmetric(kind, members) == expected);
}

TEST_CASE("canonical_partial_assignment_sorts_the_present_slots") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right"}};

    const std::map<std::string, std::string> expected = {{"left", "green"}};
    CHECK(CanonicalizeSymmetric(kind, {{"left", "green"}}) == expected);
}

TEST_CASE("canonical_duplicate_members_keep_both_slots") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right"}};

    const std::map<std::string, std::string> expected = {{"left", "blue"}, {"right", "blue"}};
    CHECK(CanonicalizeSymmetric(kind, {{"left", "blue"}, {"right", "blue"}}) == expected);
}

TEST_CASE("canonical_slot_absent_from_assignment_is_skipped") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right", "center"}};

    const std::map<std::string, std::string> expected = {{"left", "a"}, {"right", "b"}};
    CHECK(CanonicalizeSymmetric(kind, {{"left", "b"}, {"right", "a"}}) == expected);
}

TEST_CASE("canonical_two_disjoint_groups_sort_independently") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"a", "b"}, {"c", "d"}};

    const std::map<std::string, std::string> members = {
        {"a", "2"}, {"b", "1"}, {"c", "4"}, {"d", "3"}};
    const std::map<std::string, std::string> expected = {
        {"a", "1"}, {"b", "2"}, {"c", "3"}, {"d", "4"}};
    CHECK(CanonicalizeSymmetric(kind, members) == expected);
}

TEST_CASE("canonical_overlapping_groups_see_earlier_writes") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"a", "b"}, {"b", "c"}};

    const std::map<std::string, std::string> members = {{"a", "2"}, {"b", "1"}, {"c", "0"}};
    const std::map<std::string, std::string> expected = {{"a", "1"}, {"b", "0"}, {"c", "2"}};
    CHECK(CanonicalizeSymmetric(kind, members) == expected);
}

TEST_CASE("canonical_is_idempotent") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right"}};

    const std::map<std::string, std::string> members = {{"left", "green"}, {"right", "blue"}};
    const std::map<std::string, std::string> once = CanonicalizeSymmetric(kind, members);
    CHECK(CanonicalizeSymmetric(kind, once) == once);
}

TEST_CASE("same_card_different_members_is_false") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right"}};

    CHECK_FALSE(SameCard(kind, {{"left", "green"}, {"right", "blue"}},
                         {{"left", "green"}, {"right", "red"}}));
}

}  // TEST_SUITE("cardschema identifier")
