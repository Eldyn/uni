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

}  // TEST_SUITE("cardschema identifier")
