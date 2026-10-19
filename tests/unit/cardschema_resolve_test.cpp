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
using match::cardschema::Contribution;
using match::cardschema::Diagnostic;
using match::cardschema::KindTemplate;
using match::cardschema::LoadModSchema;
using match::cardschema::MemberDef;
using match::cardschema::ResolvedFace;
using match::cardschema::ResolveTemplateSlots;
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
    CHECK(diagnostics[0].message == "extends cycle: uni:a -> uni:b -> uni:a");
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
    CHECK(diagnostics[0].message == "unknown parent \"uni:cardd\"");
    CHECK(diagnostics[0].suggestion == "uni:card");
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
    CHECK(outcome.diagnostics[0].file == "mods/uni/kinds/draw2.json");
    CHECK(outcome.diagnostics[0].pointer == "/kind/contribute/base/when");
    CHECK_FALSE(outcome.diagnostics[0].message.empty());
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

}  // TEST_SUITE("cardschema contribute")
