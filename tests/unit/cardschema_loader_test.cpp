#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <match/cardschema/loader.hpp>
#include <match/expr/diagnostic.hpp>

#include "cardschema_test_support.hpp"

using match::cardschema::LoadModSchema;
using match::expr::FormatDiagnostic;

TEST_SUITE("cardschema loader") {

TEST_CASE("loads_attribute_and_members") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color",)"
              R"("visibility":"public"}})");
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","tags":["colored","red"],"order":0}})");
    dir.Write("uni/attributes/color/blue.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:blue","tags":["colored","blue"],"order":1}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    CHECK(outcome.ok());
    REQUIRE(outcome.schema.attributes.size() == 1);
    CHECK(outcome.schema.attributes[0].identifier == "uni:color");
    REQUIRE(outcome.schema.members.size() == 2);
    CHECK(outcome.schema.members[0].identifier == "uni:red");
    CHECK(outcome.schema.members[0].order == 0);
    CHECK(outcome.schema.members[1].identifier == "uni:blue");
    CHECK(outcome.schema.members[1].order == 1);
    CHECK(outcome.schema.members[0].tags == std::vector<std::string>{"colored", "red"});
}

TEST_CASE("inline_members") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/value.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:value",)"
              R"("members":[{"identifier":"uni:0","order":0},{"identifier":"uni:1","order":1}]}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    CHECK(outcome.ok());
    REQUIRE(outcome.schema.members.size() == 2);
    CHECK(outcome.schema.members[0].attribute == "uni:value");
    CHECK(outcome.schema.members[1].attribute == "uni:value");
    CHECK(outcome.schema.members[0].identifier == "uni:0");
    CHECK(outcome.schema.members[1].identifier == "uni:1");
}

TEST_CASE("missing_format_version") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"attribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/format_version: "
          "missing required key \"format_version\"");
}

TEST_CASE("unknown_root_key") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","atribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    CHECK_FALSE(outcome.ok());
    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]).find("did you mean \"attribute\"") !=
          std::string::npos);
}

TEST_CASE("root_key_in_wrong_folder") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    CHECK_FALSE(outcome.ok());
    REQUIRE(!outcome.diagnostics.empty());
    CHECK(FormatDiagnostic(outcome.diagnostics[0]).find("not allowed in kinds/") !=
          std::string::npos);
}

TEST_CASE("foreign_namespace_rejected") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"other:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    CHECK_FALSE(outcome.ok());
    REQUIRE(!outcome.diagnostics.empty());
    CHECK(FormatDiagnostic(outcome.diagnostics[0]).find("namespace") != std::string::npos);
}

TEST_CASE("duplicate_member") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red"}})");
    dir.Write("uni/attributes/shade/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    CHECK_FALSE(outcome.ok());
    REQUIRE(outcome.diagnostics.size() == 1);
    const std::string formatted = FormatDiagnostic(outcome.diagnostics[0]);
    CHECK(formatted.find("mods/uni/attributes/color/red.json") != std::string::npos);
    CHECK(formatted.find("mods/uni/attributes/shade/red.json") != std::string::npos);
}

TEST_CASE("bad_local_name") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:Red"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    CHECK_FALSE(outcome.ok());
    REQUIRE(!outcome.diagnostics.empty());
    CHECK(FormatDiagnostic(outcome.diagnostics[0]).find("[a-z0-9_]") != std::string::npos);
}

}  // TEST_SUITE("cardschema loader")
