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
    member.file = std::move(file);
    return member;
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
          "mods/uni/attributes/color/red.json#/: unknown attribute \"uni:colour\" "
          "(did you mean \"uni:color\")");
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
          "mods/magenta/attributes/color/green.json#/: attribute \"uni:color\" is not extensible; "
          "member \"magenta:green\" belongs to another mod");
}

TEST_CASE("local_name_collision") {
    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({Uni(), Magenta()});

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/magenta/attributes/color/red.json#/: members \"uni:red\" and \"magenta:red\" "
          "share the local name \"red\"; set identifier_alias on one");
}

TEST_CASE("alias_resolves_collision") {
    ModSchema magenta = Magenta();
    magenta.members[0].identifier_alias = "crimson";

    const std::vector<match::expr::Diagnostic> diagnostics = ValidateCatalog({Uni(), magenta});

    CHECK(diagnostics.empty());
}

TEST_CASE("when_expression_checked") {
    ModSchema mod = Uni();
    PropertyDef revealed;
    revealed.identifier = "uni:revealed";
    revealed.scope = "card";
    revealed.type = "boolean";
    revealed.file = "mods/uni/properties/revealed.json";
    mod.properties = {revealed};

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
