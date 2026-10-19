#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include <match/cardschema/model.hpp>
#include <match/cardschema/validate.hpp>
#include <match/expr/diagnostic.hpp>

using match::cardschema::AttributeDef;
using match::cardschema::Contribution;
using match::cardschema::DeckEntry;
using match::cardschema::DeckSpec;
using match::cardschema::KindTemplate;
using match::cardschema::MemberDef;
using match::cardschema::MemberSelector;
using match::cardschema::ModSchema;
using match::cardschema::PropertyDef;
using match::cardschema::SlotBinding;
using match::cardschema::ValidateCatalog;
using match::expr::FormatDiagnostic;

namespace {

AttributeDef ColorAttribute(bool extensible) {
    AttributeDef attribute;
    attribute.identifier = "uni:color";
    attribute.extensible = extensible;
    attribute.visibility = "public";
    attribute.file = "mods/uni/attributes/color.json";
    return attribute;
}

MemberDef ColorMember(std::string identifier, std::string file) {
    MemberDef member;
    member.attribute = "uni:color";
    member.identifier = std::move(identifier);
    member.display_name = "test";
    member.order = 0;
    member.attribute_pointer = "/attribute_member/attribute";
    member.file = std::move(file);
    return member;
}

PropertyDef RevealedProperty() {
    PropertyDef property;
    property.identifier = "uni:revealed";
    property.scope = "card";
    property.type = "boolean";
    property.type_pointer = "/property/type";
    property.attribute_pointer = "/property/attribute";
    property.file = "mods/uni/properties/revealed.json";
    return property;
}

ModSchema Uni() {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    mod.attributes = {ColorAttribute(true)};
    mod.members = {ColorMember("uni:red", "mods/uni/attributes/color/red.json")};
    return mod;
}

ModSchema Magenta() {
    ModSchema mod;
    mod.mod_id = "magenta";
    mod.directory = "mods/magenta";
    mod.members = {ColorMember("magenta:red", "mods/magenta/attributes/color/red.json")};
    return mod;
}

KindTemplate Numbered() {
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{color}";
    kind.slots = {SlotBinding{"uni:color", "color", "", ""}};
    kind.slot_order = {"color"};
    kind.file = "mods/uni/kinds/numbered.json";
    return kind;
}

DeckSpec ClassicDeck(std::vector<DeckEntry> cards) {
    DeckSpec deck;
    deck.identifier = "uni:classic";
    deck.name = "Classic";
    deck.file = "mods/uni/decks/classic.json";
    deck.cards = std::move(cards);
    return deck;
}

DeckEntry CardEntry(std::string kind, std::string pointer) {
    DeckEntry entry;
    entry.kind = std::move(kind);
    entry.copies = 1;
    entry.pointer = std::move(pointer);
    return entry;
}

}  // namespace

TEST_SUITE("cardschema validate") {

TEST_CASE("unknown_attribute_member") {
    ModSchema mod = Uni();
    mod.members[0].attribute = "uni:colour";

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/attribute: unknown attribute "
          "\"uni:colour\" (did you mean \"uni:color\")");
}

TEST_CASE("non_extensible_foreign_member") {
    ModSchema uni = Uni();
    uni.attributes[0].extensible = false;
    ModSchema magenta = Magenta();
    magenta.members[0] =
        ColorMember("magenta:green", "mods/magenta/attributes/color/green.json");

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({uni, magenta});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/magenta/attributes/color/green.json#/attribute_member/attribute: attribute "
          "\"uni:color\" is not extensible; member \"magenta:green\" belongs to another mod");
}

TEST_CASE("local_name_collision") {
    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({Uni(), Magenta()});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/magenta/attributes/color/red.json#/attribute_member/attribute: members \"uni:red\" "
          "and \"magenta:red\" share the local name \"red\"; set identifier_alias on one");
}

TEST_CASE("alias_resolves_collision") {
    ModSchema magenta = Magenta();
    magenta.members[0].identifier_alias = "crimson";

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({Uni(), magenta});

    CHECK(diagnostics.empty());
}

TEST_CASE("in_mod_collision_is_skipped") {
    ModSchema mod = Uni();
    MemberDef first = ColorMember("uni:red", "mods/uni/attributes/color/red.json");
    MemberDef second =
        ColorMember("uni:crimson", "mods/uni/attributes/color/crimson.json");
    second.identifier_alias = "red";
    mod.members = {first, second};

    CHECK(ValidateCatalog({mod}).empty());
}

TEST_CASE("third_mod_versus_first_collision") {
    ModSchema cyan;
    cyan.mod_id = "cyan";
    cyan.directory = "mods/cyan";
    cyan.members = {ColorMember("cyan:red", "mods/cyan/attributes/color/red.json")};

    const std::vector<match::expr::Diagnostic> diagnostics =
        ValidateCatalog({Uni(), Magenta(), cyan});

    REQUIRE(diagnostics.size() == 2);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/magenta/attributes/color/red.json#/attribute_member/attribute: members \"uni:red\" "
          "and \"magenta:red\" share the local name \"red\"; set identifier_alias on one");
    CHECK(FormatDiagnostic(diagnostics[1]) ==
          "mods/cyan/attributes/color/red.json#/attribute_member/attribute: members \"uni:red\" "
          "and \"cyan:red\" share the local name \"red\"; set identifier_alias on one");
}

TEST_CASE("two_attributes_sharing_a_local_name_do_not_collide") {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    mod.attributes = {{"uni:color", true, "public", "mods/uni/attributes/color.json"},
                      {"uni:value", true, "public", "mods/uni/attributes/value.json"}};
    MemberDef color;
    color.attribute = "uni:color";
    color.identifier = "uni:zero";
    color.display_name = "test";
    color.attribute_pointer = "/attribute_member/attribute";
    color.file = "mods/uni/attributes/color/zero.json";
    MemberDef value;
    value.attribute = "uni:value";
    value.identifier = "uni:red_two";
    value.identifier_alias = "zero";
    value.display_name = "test";
    value.attribute_pointer = "/attribute_member/attribute";
    value.file = "mods/uni/attributes/value/red_two.json";
    mod.members = {color, value};

    CHECK(ValidateCatalog({mod}).empty());
}

TEST_CASE("type_member_property_unknown_attribute") {
    ModSchema mod = Uni();
    PropertyDef points;
    points.identifier = "uni:points";
    points.scope = "card";
    points.type = "member";
    points.member_attribute = "uni:colour";
    points.type_pointer = "/property/type";
    points.attribute_pointer = "/property/attribute";
    points.file = "mods/uni/properties/points.json";
    mod.properties = {points};

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/properties/points.json#/property/attribute: property \"uni:points\" names "
          "unknown attribute \"uni:colour\" (did you mean \"uni:color\")");
}

TEST_CASE("duplicate_property_in_one_mod") {
    ModSchema mod = Uni();
    PropertyDef first;
    first.identifier = "uni:points";
    first.scope = "card";
    first.type = "int";
    first.type_pointer = "/property/type";
    first.attribute_pointer = "/property/attribute";
    first.file = "mods/uni/properties/points.json";
    PropertyDef second = first;
    mod.properties = {first, second};

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/properties/points.json#/property/type: property \"uni:points\" is declared "
          "twice in mod \"uni\"");
}

TEST_CASE("when_expression_checked") {
    ModSchema mod = Uni();
    mod.properties = {RevealedProperty()};

    KindTemplate kind = Numbered();
    Contribution contribution;
    contribution.when = "$card.uni:revealed";
    contribution.pointer = "/kind/contribute/all";
    kind.contribute["all"] = contribution;
    mod.kinds = {kind};

    CHECK(ValidateCatalog({mod}).empty());

    mod.kinds[0].contribute["all"].when = "$card.uni:reveled";
    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/kinds/numbered.json#/kind/contribute/all/when: unknown property "
          "\"uni:reveled\" on card (did you mean \"uni:revealed\")");
}

TEST_CASE("when_on_member_contribution_checked") {
    ModSchema mod = Uni();
    mod.properties = {RevealedProperty()};
    MemberDef member = ColorMember("uni:red", "mods/uni/attributes/color/red.json");
    Contribution contribution;
    contribution.when = "$card.uni:reveled";
    contribution.pointer = "/attribute_member/contribute/all";
    member.contribute["all"] = contribution;
    mod.members = {member};

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/contribute/all/when: unknown "
          "property \"uni:reveled\" on card (did you mean \"uni:revealed\")");
}

TEST_CASE("when_on_deck_back_contribution_checked") {
    ModSchema mod = Uni();
    mod.properties = {RevealedProperty()};
    DeckSpec deck = ClassicDeck({});
    Contribution contribution;
    contribution.when = "$card.uni:reveled";
    contribution.pointer = "/deck/contribute/back";
    deck.contribute["back"] = contribution;
    mod.decks = {deck};

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/contribute/back/when: unknown property "
          "\"uni:reveled\" on card (did you mean \"uni:revealed\")");
}

TEST_CASE("when_must_be_boolean") {
    ModSchema mod = Uni();
    PropertyDef points;
    points.identifier = "uni:points";
    points.scope = "card";
    points.type = "int";
    points.type_pointer = "/property/type";
    points.attribute_pointer = "/property/attribute";
    points.file = "mods/uni/properties/points.json";
    mod.properties = {points};

    KindTemplate kind = Numbered();
    Contribution contribution;
    contribution.when = "$card.uni:points";
    contribution.pointer = "/kind/contribute/all";
    kind.contribute["all"] = contribution;
    mod.kinds = {kind};

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/kinds/numbered.json#/kind/contribute/all/when: expected bool, got int");
}

TEST_CASE("diagnostic_cap_in_validate") {
    ModSchema mod = Uni();
    std::vector<MemberDef> members;
    for (int index = 0; index < 150; ++index) {
        MemberDef member;
        member.attribute = "uni:missing" + std::to_string(index);
        member.identifier = "uni:member" + std::to_string(index);
        member.display_name = "test";
        member.attribute_pointer = "/attribute_member/attribute";
        member.file = "mods/uni/attributes/missing.json";
        members.push_back(std::move(member));
    }
    mod.members = std::move(members);

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 101);
    CHECK(diagnostics[100].message == "more errors not shown");
}

TEST_CASE("deck_unknown_kind") {
    ModSchema mod = Uni();
    mod.kinds = {Numbered()};
    mod.decks = {ClassicDeck({CardEntry("uni:numberd", "/deck/cards/0")})};

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/0: unknown kind \"uni:numberd\" "
          "(did you mean \"uni:numbered\")");
}

TEST_CASE("abstract_deck_kind") {
    ModSchema mod = Uni();
    KindTemplate base;
    base.identifier = "uni:base";
    base.is_abstract = true;
    base.file = "mods/uni/kinds/base.json";
    mod.kinds = {base};
    mod.decks = {ClassicDeck({CardEntry("uni:base", "/deck/cards/0")})};

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/0: kind \"uni:base\" is abstract");
}

TEST_CASE("deck_member_slot_unknown") {
    ModSchema mod = Uni();
    mod.kinds = {Numbered()};
    DeckEntry entry = CardEntry("uni:numbered", "/deck/cards/0");
    MemberSelector selector;
    selector.mode = "all";
    selector.pointer = "/deck/cards/0/members/colour";
    entry.members["colour"] = selector;
    mod.decks = {ClassicDeck({entry})};

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({mod});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/0/members/colour: unknown slot \"colour\" "
          "for kind \"uni:numbered\" (did you mean \"color\")");
}

}  // TEST_SUITE("cardschema validate")
