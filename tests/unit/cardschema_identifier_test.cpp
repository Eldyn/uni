#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <match/cardschema/identifier.hpp>
#include <match/cardschema/model.hpp>
#include <match/expr/diagnostic.hpp>

using match::cardschema::CanonicalizeSymmetric;
using match::cardschema::FillTemplate;
using match::cardschema::FillTitle;
using match::cardschema::GenerateIdentifier;
using match::cardschema::KindTemplate;
using match::cardschema::SameCard;
using match::cardschema::SlotBinding;
using match::expr::Diagnostic;
using match::expr::FormatDiagnostic;

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

TEST_CASE("explicit_format") {
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{color}_{value}";
    kind.file = "kinds/numbered.json";

    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> identifier =
        GenerateIdentifier(kind, {{"color", "red"}, {"value", "5"}}, diagnostics);

    CHECK(diagnostics.empty());
    REQUIRE(identifier.has_value());
    CHECK(*identifier == "uni:red_5");
}

TEST_CASE("default_format_no_slots") {
    KindTemplate kind;
    kind.identifier = "uni:wild_draw4";
    kind.file = "kinds/wild_draw4.json";

    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> identifier = GenerateIdentifier(kind, {}, diagnostics);

    CHECK(diagnostics.empty());
    REQUIRE(identifier.has_value());
    CHECK(*identifier == "uni:wild_draw4");
}

TEST_CASE("default_format_with_slots") {
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.slot_order = {"color", "value"};
    kind.slots = {SlotBinding{"uni:color", "color", "", ""},
                  SlotBinding{"uni:value", "value", "", ""}};
    kind.file = "kinds/numbered.json";

    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> identifier =
        GenerateIdentifier(kind, {{"color", "red"}, {"value", "5"}}, diagnostics);

    CHECK(diagnostics.empty());
    REQUIRE(identifier.has_value());
    CHECK(*identifier == "uni:numbered_red_5");
}

TEST_CASE("symmetric_default_sorted") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.symmetric_groups = {{"left", "right"}};
    kind.slot_order = {"left", "right"};
    kind.file = "kinds/split.json";

    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> identifier =
        GenerateIdentifier(kind, {{"left", "green"}, {"right", "blue"}}, diagnostics);

    CHECK(diagnostics.empty());
    REQUIRE(identifier.has_value());
    CHECK(*identifier == "uni:split_blue_green");
}

TEST_CASE("unknown_placeholder") {
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{colour}_x";
    kind.file = "kinds/numbered.json";

    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> identifier =
        GenerateIdentifier(kind, {{"color", "red"}}, diagnostics);

    CHECK_FALSE(identifier.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "kinds/numbered.json#/kind/identifier_format: unknown placeholder \"{colour}\" "
          "(did you mean \"color\")");
}

TEST_CASE("bad_character") {
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{color}";
    kind.file = "kinds/numbered.json";

    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> identifier =
        GenerateIdentifier(kind, {{"color", "a-b"}}, diagnostics);

    CHECK_FALSE(identifier.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "kinds/numbered.json#/kind/identifier_format: card identifier \"uni:a-b\" is not a "
          "valid identifier; a local name uses [a-z0-9_] (1 to 32 characters)");
}

TEST_CASE("alias_used") {
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{color}_{value}";
    kind.file = "kinds/numbered.json";

    // INFO: Task 11 resolves the alias "crimson" of the member "uni:red" before
    // it calls this function, so the value here is already the alias.
    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> identifier =
        GenerateIdentifier(kind, {{"color", "crimson"}, {"value", "5"}}, diagnostics);

    CHECK(diagnostics.empty());
    REQUIRE(identifier.has_value());
    CHECK(*identifier == "uni:crimson_5");
}

TEST_CASE("unclosed_placeholder") {
    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> filled =
        FillTemplate("{color", {{"color", "red"}}, "kinds/numbered.json",
                     "/kind/identifier_format", diagnostics);

    CHECK_FALSE(filled.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "kinds/numbered.json#/kind/identifier_format: unclosed placeholder \"{\"");
}

TEST_CASE("title_fill") {
    const std::map<std::string, std::string> values = {{"color", "red"}, {"value", "5"}};

    std::vector<Diagnostic> diagnostics;
    const std::optional<std::string> filled =
        FillTemplate("{color} {value}", values, "kinds/numbered.json", "/kind/title_format",
                     diagnostics);
    CHECK(diagnostics.empty());
    REQUIRE(filled.has_value());
    CHECK(*filled == "red 5");

    const std::optional<std::string> title = FillTitle(
        "{color} {value}", values, "kinds/numbered.json", "/kind/title_format", diagnostics);
    CHECK(diagnostics.empty());
    REQUIRE(title.has_value());
    CHECK(*title == "Red 5");
}

}  // TEST_SUITE("cardschema identifier")
