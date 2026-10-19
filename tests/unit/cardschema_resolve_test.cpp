#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

#include <match/cardschema/loader.hpp>
#include <match/cardschema/resolve.hpp>
#include <match/expr/diagnostic.hpp>

#include "cardschema_test_support.hpp"

using match::cardschema::Band;
using match::cardschema::BuildExtendsChain;
using match::cardschema::CheckSurfaces;
using match::cardschema::Contribution;
using match::cardschema::DeckSpec;
using match::cardschema::Diagnostic;
using match::cardschema::KindTemplate;
using match::cardschema::LoadModSchema;
using match::cardschema::MemberDef;
using match::cardschema::ResolvedBack;
using match::cardschema::ResolvedFace;
using match::cardschema::ResolveBack;
using match::cardschema::ResolveTemplateSlots;
using match::cardschema::SlotBinding;
using match::cardschema::SurfaceDef;
using match::expr::FormatDiagnostic;

namespace {

Contribution Tint(const std::string& tint, Band band) {
    Contribution contribution;
    contribution.tint = tint;
    contribution.band = band;
    return contribution;
}

}  // namespace

TEST_SUITE("cardschema resolve") {

TEST_CASE("member_beats_kind") {
    KindTemplate kind;
    kind.identifier = "uni:draw2";
    kind.slot_order = {"base"};
    kind.contribute["base"] = Tint("#111111", Band::kBase);

    MemberDef member;
    member.identifier = "uni:red";
    member.contribute["base"] = Tint("#e5383b", Band::kAttribute);

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&member};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    CHECK(face.diagnostics.empty());
    REQUIRE(face.slots.count("base") == 1);
    CHECK(face.slots.at("base").winner.tint == "#e5383b");
    CHECK(face.slots.at("base").source == "uni:red");
}

TEST_CASE("override_band_beats_attribute") {
    KindTemplate kind;
    kind.identifier = "uni:draw2";
    kind.slot_order = {"base"};

    MemberDef attribute;
    attribute.identifier = "uni:red";
    attribute.contribute["base"] = Tint("#111111", Band::kAttribute);

    MemberDef overridden;
    overridden.identifier = "uni:blue";
    overridden.contribute["base"] = Tint("#e5383b", Band::kOverride);

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&attribute, &overridden};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    CHECK(face.diagnostics.empty());
    REQUIRE(face.slots.count("base") == 1);
    CHECK(face.slots.at("base").winner.tint == "#e5383b");
    CHECK(face.slots.at("base").source == "uni:blue");
}

TEST_CASE("same_band_conflict") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.slot_order = {"border"};

    MemberDef first;
    first.identifier = "uni:red";
    first.contribute["border"] = Tint("#111111", Band::kAttribute);

    MemberDef second;
    second.identifier = "uni:blue";
    second.contribute["border"] = Tint("#e5383b", Band::kAttribute);

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&first, &second};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    REQUIRE(face.diagnostics.size() == 1);
    CHECK(face.diagnostics[0].message ==
          "slot \"border\" has two contributions in band \"attribute\" (uni:red, uni:blue); "
          "set \"explicit\": true on one");
}

TEST_CASE("explicit_breaks_tie") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.slot_order = {"border"};

    MemberDef first;
    first.identifier = "uni:red";
    first.contribute["border"] = Tint("#111111", Band::kAttribute);
    first.contribute["border"].explicit_override = true;

    MemberDef second;
    second.identifier = "uni:blue";
    second.contribute["border"] = Tint("#e5383b", Band::kAttribute);

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&first, &second};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    CHECK(face.diagnostics.empty());
    REQUIRE(face.slots.count("border") == 1);
    CHECK(face.slots.at("border").source == "uni:red");
    CHECK(face.slots.at("border").winner.tint == "#111111");
}

TEST_CASE("all_key_expands") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.slot_order = {"base", "border"};
    Contribution all;
    all.sprite = "cards/base";
    kind.contribute["all"] = all;

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    CHECK(face.diagnostics.empty());
    CHECK(face.slot_order == std::vector<std::string>{"base", "border"});
    REQUIRE(face.slots.count("base") == 1);
    REQUIRE(face.slots.count("border") == 1);
    CHECK(face.slots.at("base").winner.sprite == "cards/base");
    CHECK(face.slots.at("border").winner.sprite == "cards/base");
    CHECK(face.slots.at("base").source == "uni:card");
}

TEST_CASE("child_overrides_parent") {
    KindTemplate parent;
    parent.identifier = "uni:card";
    parent.slot_order = {"base"};
    Contribution parent_contribution;
    parent_contribution.sprite = "p";
    parent.contribute["base"] = parent_contribution;

    KindTemplate child;
    child.identifier = "uni:wild";
    child.extends = "uni:card";
    Contribution child_contribution;
    child_contribution.sprite = "c";
    child.contribute["base"] = child_contribution;

    const std::vector<const KindTemplate*> chain = {&child, &parent};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/wild.json");

    CHECK(face.diagnostics.empty());
    CHECK(face.slot_order == std::vector<std::string>{"base"});
    REQUIRE(face.slots.count("base") == 1);
    CHECK(face.slots.at("base").winner.sprite == "c");
    CHECK(face.slots.at("base").source == "uni:wild");
}

TEST_CASE("extends_cycle") {
    KindTemplate a;
    a.identifier = "uni:a";
    a.extends = "uni:b";
    a.file = "kinds/a.json";

    KindTemplate b;
    b.identifier = "uni:b";
    b.extends = "uni:a";
    b.file = "kinds/b.json";

    const std::map<std::string, const KindTemplate*> by_id = {{"uni:a", &a}, {"uni:b", &b}};
    std::vector<Diagnostic> diagnostics;
    const std::vector<const KindTemplate*> chain = BuildExtendsChain(a, by_id, diagnostics);

    CHECK(chain.empty());
    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "kinds/a.json#/kind/extends: extends cycle: uni:a -> uni:b -> uni:a");
}

TEST_CASE("missing_parent_suggestion") {
    KindTemplate kind;
    kind.identifier = "uni:a";
    kind.extends = "uni:cardd";
    kind.file = "kinds/a.json";

    KindTemplate b;
    b.identifier = "uni:b";

    KindTemplate card;
    card.identifier = "uni:card";

    const std::map<std::string, const KindTemplate*> by_id = {
        {"uni:a", &kind}, {"uni:b", &b}, {"uni:card", &card}};
    std::vector<Diagnostic> diagnostics;
    const std::vector<const KindTemplate*> chain = BuildExtendsChain(kind, by_id, diagnostics);

    CHECK(chain.empty());
    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "kinds/a.json#/kind/extends: unknown parent \"uni:cardd\" "
          "(did you mean \"uni:card\")");
}

TEST_CASE("parent_slot_outside_slot_order_reports_the_parent_file") {
    KindTemplate parent;
    parent.identifier = "uni:card";
    parent.file = "kinds/card.json";
    parent.contribute["extra"] = Tint("#111111", Band::kBase);
    parent.contribute["extra"].pointer = "/kind/contribute/extra";

    KindTemplate child;
    child.identifier = "uni:draw2";
    child.file = "kinds/draw2.json";
    child.slot_order = {"base"};

    const std::vector<const KindTemplate*> chain = {&child, &parent};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    REQUIRE(face.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(face.diagnostics[0]) ==
          "kinds/card.json#/kind/contribute/extra: "
          "contribution to slot \"extra\" which is not in slot_order");
}

TEST_CASE("member_slot_outside_slot_order_reports_the_member_file") {
    KindTemplate kind;
    kind.identifier = "uni:draw2";
    kind.file = "kinds/draw2.json";
    kind.slot_order = {"base"};

    MemberDef member;
    member.identifier = "uni:red";
    member.file = "attributes/color/red.json";
    member.contribute["nope"] = Tint("#e5383b", Band::kAttribute);
    member.contribute["nope"].pointer = "/attribute_member/contribute/nope";

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&member};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    REQUIRE(face.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(face.diagnostics[0]) ==
          "attributes/color/red.json#/attribute_member/contribute/nope: "
          "contribution to slot \"nope\" which is not in slot_order");
}

TEST_CASE("conflict_reports_the_first_candidate_file") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.file = "kinds/card.json";
    kind.slot_order = {"border"};

    MemberDef first;
    first.identifier = "uni:red";
    first.file = "attributes/color/red.json";
    first.contribute["border"] = Tint("#111111", Band::kAttribute);
    first.contribute["border"].pointer = "/attribute_member/contribute/border";

    MemberDef second;
    second.identifier = "uni:blue";
    second.file = "attributes/color/blue.json";
    second.contribute["border"] = Tint("#e5383b", Band::kAttribute);
    second.contribute["border"].pointer = "/attribute_member/contribute/border";

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&first, &second};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    REQUIRE(face.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(face.diagnostics[0]) ==
          "attributes/color/red.json#/attribute_member/contribute/border: "
          "slot \"border\" has two contributions in band \"attribute\" (uni:red, uni:blue); "
          "set \"explicit\": true on one");
}

TEST_CASE("two_explicit_candidates_are_an_error") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.slot_order = {"border"};

    MemberDef first;
    first.identifier = "uni:red";
    first.contribute["border"] = Tint("#111111", Band::kAttribute);
    first.contribute["border"].explicit_override = true;

    MemberDef second;
    second.identifier = "uni:blue";
    second.contribute["border"] = Tint("#e5383b", Band::kAttribute);
    second.contribute["border"].explicit_override = true;

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&first, &second};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    REQUIRE(face.diagnostics.size() == 1);
    CHECK(face.diagnostics[0].message ==
          "slot \"border\" has 2 explicit contributions in band \"attribute\" "
          "(uni:red, uni:blue)");
    CHECK(face.slots.count("border") == 0);
}

TEST_CASE("three_conflicting_candidates_report_every_owner") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.slot_order = {"border"};

    MemberDef first;
    first.identifier = "uni:red";
    first.contribute["border"] = Tint("#111111", Band::kAttribute);

    MemberDef second;
    second.identifier = "uni:blue";
    second.contribute["border"] = Tint("#e5383b", Band::kAttribute);

    MemberDef third;
    third.identifier = "uni:green";
    third.contribute["border"] = Tint("#222b45", Band::kAttribute);

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&first, &second, &third};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    REQUIRE(face.diagnostics.size() == 1);
    CHECK(face.diagnostics[0].message ==
          "slot \"border\" has 3 contributions in band \"attribute\" "
          "(uni:red, uni:blue, uni:green); set \"explicit\": true on one");
    CHECK(face.slots.count("border") == 0);
}

TEST_CASE("too_many_members_is_rejected") {
    KindTemplate kind;
    kind.identifier = "uni:draw2";
    kind.slot_order = {"base"};

    std::vector<MemberDef> storage(33);
    std::vector<const MemberDef*> members;
    members.reserve(storage.size());
    for (std::size_t index = 0; index < storage.size(); ++index) {
        storage[index].identifier = "uni:m" + std::to_string(index);
        members.push_back(&storage[index]);
    }

    const std::vector<const KindTemplate*> chain = {&kind};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    REQUIRE(face.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(face.diagnostics[0]) ==
          "kinds/draw2.json#/: a card may have at most 32 members");
    CHECK(face.slot_order.empty());
    CHECK(face.slots.empty());
}

TEST_CASE("thirty_two_members_are_allowed") {
    KindTemplate kind;
    kind.identifier = "uni:draw2";
    kind.slot_order = {"base"};

    std::vector<MemberDef> storage(32);
    std::vector<const MemberDef*> members;
    members.reserve(storage.size());
    for (std::size_t index = 0; index < storage.size(); ++index) {
        storage[index].identifier = "uni:m" + std::to_string(index);
        members.push_back(&storage[index]);
    }

    const std::vector<const KindTemplate*> chain = {&kind};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    CHECK(face.diagnostics.empty());
}

TEST_CASE("same_member_listed_twice_is_deduped") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.slot_order = {"border"};

    MemberDef member;
    member.identifier = "uni:red";
    member.contribute["border"] = Tint("#111111", Band::kAttribute);

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&member, &member};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    CHECK(face.diagnostics.empty());
    REQUIRE(face.slots.count("border") == 1);
    CHECK(face.slots.at("border").source == "uni:red");
    CHECK(face.slots.at("border").winner.tint == "#111111");
}

TEST_CASE("dedupe_diagnostics_keeps_the_first_of_each_triple") {
    std::vector<Diagnostic> diagnostics = {
        {"kinds/a.json", "/kind/extends", "one", ""},
        {"kinds/a.json", "/kind/slot_order", "one", ""},
        {"kinds/a.json", "/kind/extends", "one", ""},
        {"kinds/a.json", "/kind/extends", "two", ""},
    };

    match::cardschema::DedupeDiagnostics(diagnostics);

    REQUIRE(diagnostics.size() == 3);
    CHECK(diagnostics[0].pointer == "/kind/extends");
    CHECK(diagnostics[0].message == "one");
    CHECK(diagnostics[1].pointer == "/kind/slot_order");
    CHECK(diagnostics[2].message == "two");
}

TEST_CASE("slot_order_inherited_from_parent") {
    KindTemplate parent;
    parent.identifier = "uni:card";
    parent.slot_order = {"base", "border"};

    KindTemplate child;
    child.identifier = "uni:draw2";

    const std::vector<const KindTemplate*> chain = {&child, &parent};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    CHECK(face.slot_order == std::vector<std::string>{"base", "border"});
}

TEST_CASE("child_slot_order_wins_over_parent") {
    KindTemplate parent;
    parent.identifier = "uni:card";
    parent.slot_order = {"base"};

    KindTemplate child;
    child.identifier = "uni:draw2";
    child.slot_order = {"border"};

    const std::vector<const KindTemplate*> chain = {&child, &parent};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    CHECK(face.slot_order == std::vector<std::string>{"border"});
}

TEST_CASE("back_slot_is_not_a_slot_order_error") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.file = "kinds/card.json";
    kind.slot_order = {"base"};
    Contribution back;
    back.sprite = "cards/back";
    back.pointer = "/kind/contribute/back";
    kind.contribute["back"] = back;

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    CHECK(face.diagnostics.empty());
    CHECK(face.slots.empty());
}

TEST_CASE("slot_specific_key_replaces_all") {
    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.slot_order = {"base", "border"};
    Contribution all;
    all.sprite = "cards/all";
    kind.contribute["all"] = all;
    Contribution border;
    border.sprite = "cards/border";
    kind.contribute["border"] = border;

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/card.json");

    CHECK(face.diagnostics.empty());
    REQUIRE(face.slots.count("base") == 1);
    REQUIRE(face.slots.count("border") == 1);
    CHECK(face.slots.at("base").winner.sprite == "cards/all");
    CHECK(face.slots.at("border").winner.sprite == "cards/border");
}

TEST_CASE("kind_and_member_conflict_in_base_band") {
    KindTemplate kind;
    kind.identifier = "uni:draw2";
    kind.file = "kinds/draw2.json";
    kind.slot_order = {"base"};
    kind.contribute["base"] = Tint("#111111", Band::kBase);
    kind.contribute["base"].pointer = "/kind/contribute/base";

    MemberDef member;
    member.identifier = "uni:red";
    member.file = "attributes/color/red.json";
    member.contribute["base"] = Tint("#e5383b", Band::kBase);
    member.contribute["base"].pointer = "/attribute_member/contribute/base";

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members = {&member};
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/draw2.json");

    REQUIRE(face.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(face.diagnostics[0]) ==
          "kinds/draw2.json#/kind/contribute/base: "
          "slot \"base\" has two contributions in band \"base\" (uni:draw2, uni:red); "
          "set \"explicit\": true on one");
    CHECK(face.slots.count("base") == 0);
}

TEST_CASE("extends_chain_at_the_limit_has_no_diagnostic") {
    std::vector<KindTemplate> kinds(16);
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        kinds[index].identifier = "uni:k" + std::to_string(index);
        kinds[index].file = "kinds/k" + std::to_string(index) + ".json";
    }
    for (std::size_t index = 0; index + 1 < kinds.size(); ++index) {
        kinds[index].extends = kinds[index + 1].identifier;
    }
    std::map<std::string, const KindTemplate*> by_id;
    for (const KindTemplate& kind : kinds) {
        by_id.emplace(kind.identifier, &kind);
    }

    std::vector<Diagnostic> diagnostics;
    const std::vector<const KindTemplate*> chain = BuildExtendsChain(kinds[0], by_id, diagnostics);

    CHECK(diagnostics.empty());
    CHECK(chain.size() == 16);
}

TEST_CASE("extends_chain_over_the_limit_fails") {
    std::vector<KindTemplate> kinds(17);
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        kinds[index].identifier = "uni:k" + std::to_string(index);
        kinds[index].file = "kinds/k" + std::to_string(index) + ".json";
    }
    for (std::size_t index = 0; index + 1 < kinds.size(); ++index) {
        kinds[index].extends = kinds[index + 1].identifier;
    }
    std::map<std::string, const KindTemplate*> by_id;
    for (const KindTemplate& kind : kinds) {
        by_id.emplace(kind.identifier, &kind);
    }

    std::vector<Diagnostic> diagnostics;
    const std::vector<const KindTemplate*> chain = BuildExtendsChain(kinds[0], by_id, diagnostics);

    CHECK(chain.empty());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].message == "extends chain is longer than 16 kinds");
}

TEST_CASE("self_extends_is_a_cycle") {
    KindTemplate kind;
    kind.identifier = "uni:a";
    kind.extends = "uni:a";
    kind.file = "kinds/a.json";

    const std::map<std::string, const KindTemplate*> by_id = {{"uni:a", &kind}};
    std::vector<Diagnostic> diagnostics;
    const std::vector<const KindTemplate*> chain = BuildExtendsChain(kind, by_id, diagnostics);

    CHECK(chain.empty());
    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "kinds/a.json#/kind/extends: extends cycle: uni:a -> uni:a");
}

TEST_CASE("back_from_deck") {
    DeckSpec deck;
    deck.identifier = "uni:mini";
    deck.file = "decks/mini.json";
    Contribution back;
    back.sprite = "cards/back";
    back.band = Band::kBase;
    back.pointer = "/deck/contribute/back";
    deck.contribute["back"] = back;

    const std::vector<const KindTemplate*> chain;
    const std::vector<const MemberDef*> members;
    const ResolvedBack resolved = ResolveBack(&deck, chain, members);

    CHECK(resolved.diagnostics.empty());
    REQUIRE(resolved.slot.has_value());
    CHECK(resolved.slot->winner.sprite == "cards/back");
    CHECK(resolved.slot->source == "uni:mini");
}

TEST_CASE("member_back_beats_deck") {
    DeckSpec deck;
    deck.identifier = "uni:mini";
    deck.file = "decks/mini.json";
    Contribution deck_back;
    deck_back.sprite = "cards/back";
    deck_back.band = Band::kBase;
    deck.contribute["back"] = deck_back;

    MemberDef member;
    member.identifier = "uni:red";
    member.file = "attributes/color/red.json";
    Contribution member_back;
    member_back.tint = "#e5383b";
    member_back.band = Band::kOverride;
    member.contribute["back"] = member_back;

    const std::vector<const KindTemplate*> chain;
    const std::vector<const MemberDef*> members = {&member};
    const ResolvedBack resolved = ResolveBack(&deck, chain, members);

    CHECK(resolved.diagnostics.empty());
    REQUIRE(resolved.slot.has_value());
    CHECK(resolved.slot->winner.tint == "#e5383b");
    CHECK(resolved.slot->source == "uni:red");
}

TEST_CASE("kind_back_beats_deck_same_band") {
    DeckSpec deck;
    deck.identifier = "uni:mini";
    deck.file = "decks/mini.json";
    Contribution deck_back;
    deck_back.sprite = "cards/back";
    deck_back.band = Band::kBase;
    deck.contribute["back"] = deck_back;

    KindTemplate kind;
    kind.identifier = "uni:card";
    kind.file = "kinds/card.json";
    Contribution kind_back;
    kind_back.sprite = "cards/kind_back";
    kind_back.band = Band::kBase;
    kind.contribute["back"] = kind_back;

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members;
    const ResolvedBack resolved = ResolveBack(&deck, chain, members);

    CHECK(resolved.diagnostics.empty());
    REQUIRE(resolved.slot.has_value());
    CHECK(resolved.slot->winner.sprite == "cards/kind_back");
    CHECK(resolved.slot->source == "uni:card");
}

TEST_CASE("no_back_no_error") {
    DeckSpec deck;
    deck.identifier = "uni:mini";
    deck.file = "decks/mini.json";

    const std::vector<const KindTemplate*> chain;
    const std::vector<const MemberDef*> members;
    const ResolvedBack resolved = ResolveBack(&deck, chain, members);

    CHECK(resolved.diagnostics.empty());
    CHECK_FALSE(resolved.slot.has_value());
}

TEST_CASE("surface_channel_rejected") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"surfaces":{"left":{"mask":"masks/split",)"
              R"("channel":"alpha"}}}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/surfaces/left/channel: "
          "key \"channel\" must be one of \"red\", \"green\", \"blue\"");
}

TEST_CASE("surface_undeclared") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.file = "kinds/split.json";
    kind.slot_order = {"base"};
    Contribution base;
    base.pointer = "/kind/contribute/base";
    base.surfaces["left"] = SurfaceDef{"masks/split", "red"};
    kind.contribute["base"] = base;
    SlotBinding binding;
    binding.attribute = "uni:color";
    binding.name = "left";
    binding.surface = "lefft";
    kind.slots = {binding};

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/split.json");
    const std::vector<Diagnostic> diagnostics = CheckSurfaces(kind, face);

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].message == "slot \"left\" names surface \"lefft\"");
    CHECK(diagnostics[0].suggestion == "left");
}

TEST_CASE("surface_duplicate_pair") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.file = "kinds/split.json";
    kind.slot_order = {"base"};
    Contribution base;
    base.pointer = "/kind/contribute/base";
    base.surfaces["left"] = SurfaceDef{"masks/split", "red"};
    base.surfaces["right"] = SurfaceDef{"masks/split", "red"};
    kind.contribute["base"] = base;

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/split.json");
    const std::vector<Diagnostic> diagnostics = CheckSurfaces(kind, face);

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "kinds/split.json#/kind/contribute/base: slot \"base\" surfaces \"left\" and \"right\" "
          "use the same mask \"masks/split\" and channel \"red\"");
}

TEST_CASE("surface_ok") {
    KindTemplate kind;
    kind.identifier = "uni:split";
    kind.file = "kinds/split.json";
    kind.slot_order = {"base"};
    Contribution base;
    base.pointer = "/kind/contribute/base";
    base.surfaces["left"] = SurfaceDef{"masks/split", "red"};
    base.surfaces["right"] = SurfaceDef{"masks/split", "green"};
    kind.contribute["base"] = base;
    SlotBinding left;
    left.attribute = "uni:color";
    left.name = "left";
    left.surface = "left";
    SlotBinding right;
    right.attribute = "uni:color";
    right.name = "right";
    right.surface = "right";
    kind.slots = {left, right};

    const std::vector<const KindTemplate*> chain = {&kind};
    const std::vector<const MemberDef*> members;
    const ResolvedFace face = ResolveTemplateSlots(chain, members, "kinds/split.json");
    const std::vector<Diagnostic> diagnostics = CheckSurfaces(kind, face);

    CHECK(diagnostics.empty());
}

}  // TEST_SUITE("cardschema resolve")

TEST_SUITE("cardschema contribute") {

TEST_CASE("kind_contribution_parsed") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"sprite":"cards/base","tint":"#e5383b",)"
              R"("band":"override","explicit":true,"parameters":{"shade":1}},)"
              R"("all":{"sprite":"cards/all"}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.ok());
    REQUIRE(outcome.schema.kinds.size() == 1);
    const match::cardschema::ContributionMap& contribute = outcome.schema.kinds[0].contribute;
    REQUIRE(contribute.size() == 2);
    CHECK(contribute.at("base").sprite == "cards/base");
    CHECK(contribute.at("base").tint == "#e5383b");
    CHECK(contribute.at("base").band == Band::kOverride);
    CHECK(contribute.at("base").explicit_override);
    CHECK(contribute.at("base").parameters ==
          match::cardschema::json::parse(R"({"shade":1})"));
    CHECK(contribute.at("base").pointer == "/kind/contribute/base");
    CHECK(contribute.at("all").sprite == "cards/all");
    CHECK(contribute.at("all").band == Band::kBase);
}

TEST_CASE("member_contribution_default_band") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","contribute":{"base":{"tint":"#e5383b"}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.ok());
    REQUIRE(outcome.schema.members.size() == 1);
    const match::cardschema::ContributionMap& contribute = outcome.schema.members[0].contribute;
    REQUIRE(contribute.count("base") == 1);
    CHECK(contribute.at("base").tint == "#e5383b");
    CHECK(contribute.at("base").band == Band::kAttribute);
    CHECK(contribute.at("base").pointer == "/attribute_member/contribute/base");
}

TEST_CASE("deck_back_default_band") {
    CardSchemaTempDir dir;
    dir.Write("uni/decks/mini.json",
              R"({"format_version":"1.0.0","deck":{"identifier":"uni:mini",)"
              R"("contribute":{"back":{"sprite":"cards/back"}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.ok());
    REQUIRE(outcome.schema.decks.size() == 1);
    const match::cardschema::ContributionMap& contribute = outcome.schema.decks[0].contribute;
    REQUIRE(contribute.count("back") == 1);
    CHECK(contribute.at("back").sprite == "cards/back");
    CHECK(contribute.at("back").band == Band::kBase);
    CHECK(contribute.at("back").pointer == "/deck/contribute/back");
}

TEST_CASE("contribution_unknown_key_suggested") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"sprit":"x"}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/sprit: unknown key \"sprit\" "
          "(did you mean \"sprite\")");
}

TEST_CASE("contribution_bad_tint") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"tint":"red"}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/tint: "
          "key \"tint\" must be a \"#rrggbb\" color");
}

TEST_CASE("contribution_bad_band_suggested") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"band":"overide"}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/band: "
          "key \"band\" must be one of \"base\", \"attribute\", \"override\", \"runtime\" "
          "(did you mean \"override\")");
}

TEST_CASE("contribution_bad_when") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"sprite":"s","when":"1 +"}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/when: "
          "expected an expression after '+' (at offset 3)");
}

TEST_CASE("when_diagnostics_are_capped") {
    CardSchemaTempDir dir;
    std::string contribute = "\"contribute\":{";
    for (int index = 0; index < 500; ++index) {
        if (index != 0) {
            contribute += ",";
        }
        contribute += "\"s" + std::to_string(index) + "\":{\"when\":\"1 +\"}";
    }
    contribute += "}";
    const std::string document =
        "{\"format_version\":\"1.0.0\",\"kind\":{\"identifier\":\"uni:draw2\"," + contribute +
        "}}";
    dir.Write("uni/kinds/draw2.json", document);

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 101);
    CHECK(outcome.diagnostics.back().message == "more errors not shown");
}

TEST_CASE("contribution_surfaces_parsed") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"surfaces":{"left":{"mask":"masks/split",)"
              R"("channel":"red"}}}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.ok());
    REQUIRE(outcome.schema.kinds.size() == 1);
    const auto& surfaces = outcome.schema.kinds[0].contribute.at("base").surfaces;
    REQUIRE(surfaces.count("left") == 1);
    CHECK(surfaces.at("left").mask == "masks/split");
    CHECK(surfaces.at("left").channel == "red");
}

TEST_CASE("member_contribution_must_be_object") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","contribute":[]}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/contribute: "
          "key \"contribute\" must be an object");
}

TEST_CASE("contribution_must_be_object") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":[]}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base: "
          "contribution must be an object");
}

TEST_CASE("contribution_explicit_must_be_boolean") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"explicit":1}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/explicit: "
          "key \"explicit\" must be a boolean");
}

TEST_CASE("contribution_parameters_must_be_object") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"parameters":3}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/parameters: "
          "key \"parameters\" must be an object");
}

TEST_CASE("contribution_surfaces_must_be_object") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"surfaces":[]}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/surfaces: "
          "key \"surfaces\" must be an object");
}

TEST_CASE("contribution_surface_must_be_object") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"surfaces":{"left":[]}}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/surfaces/left: "
          "surface must be an object");
}

TEST_CASE("contribution_when_over_node_budget") {
    CardSchemaTempDir dir;
    std::string source = "1";
    for (int index = 0; index < 128; ++index) {
        source += " + 1";
    }
    dir.Write("uni/kinds/draw2.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:draw2",)"
              R"("contribute":{"base":{"sprite":"s","when":")" + source + R"("}}}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/draw2.json#/kind/contribute/base/when: "
          "expression has 257 nodes; the limit is 256");
}

}  // TEST_SUITE("cardschema contribute")
