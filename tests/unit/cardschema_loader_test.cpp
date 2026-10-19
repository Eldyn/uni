#include <doctest/doctest.h>

#include <filesystem>
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

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/atribute: unknown key \"atribute\" "
          "(did you mean \"attribute\")");
}

TEST_CASE("root_key_in_wrong_folder") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/color.json#/attribute: "
          "root key 'attribute' is not allowed in kinds/");
}

TEST_CASE("foreign_namespace_rejected") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"other:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/attribute/identifier: "
          "attribute identifier \"other:color\" must use the namespace \"uni\"");
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

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/shade/red.json#/attribute_member/identifier: "
          "duplicate member identifier \"uni:red\" in mods/uni/attributes/shade/red.json; "
          "first declared in mods/uni/attributes/color/red.json");
}

TEST_CASE("bad_local_name") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:Red"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/identifier: "
          "member identifier \"uni:Red\" is not a valid identifier; a local name uses "
          "[a-z0-9_] (1 to 32 characters)");
}

TEST_CASE("invalid_json") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json", R"({"format_version":)");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/: invalid JSON");
}

TEST_CASE("non_object_file") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json", "[]");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/: file must be a JSON object");
}

TEST_CASE("wrong_format_version") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"2.0.0","attribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/format_version: format_version must be \"1.0.0\"");
}

TEST_CASE("multiple_roots") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"},)"
              R"("attribute_member":{"identifier":"uni:red","attribute":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/: "
          "exactly one root key is allowed besides \"format_version\"");
}

TEST_CASE("unknown_attribute_key") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color",)"
              R"("visibilty":"public"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/attribute/visibilty: unknown key \"visibilty\" "
          "(did you mean \"visibility\")");
}

TEST_CASE("unknown_member_key") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","ordr":1}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/ordr: "
          "unknown key \"ordr\" (did you mean \"order\")");
}

TEST_CASE("order_must_be_integer") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","order":"0"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/order: "
          "key \"order\" must be an integer");
}

TEST_CASE("tags_must_be_array") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","tags":"colored"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/tags: "
          "key \"tags\" must be an array");
}

TEST_CASE("extensible_must_be_boolean") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color",)"
              R"("extensible":"yes"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/attribute/extensible: "
          "key \"extensible\" must be a boolean");
}

TEST_CASE("order_above_maximum") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","order":2147483648}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/order: "
          "key \"order\" is out of range");
}

TEST_CASE("order_below_minimum") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","order":-2147483649}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/order: "
          "key \"order\" is out of range");
}

TEST_CASE("empty_tag_rejected") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/red.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red","tags":[""]}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/red.json#/attribute_member/tags/0: "
          "tag must be a non-empty string");
}

TEST_CASE("bad_visibility") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color",)"
              R"("visibility":"everyone"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/attribute/visibility: "
          "key \"visibility\" must be one of \"public\", \"owner\", \"hidden\"");
}

TEST_CASE("duplicate_attribute") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");
    dir.Write("uni/attributes/shade/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/shade/color.json#/attribute/identifier: "
          "duplicate attribute identifier \"uni:color\" in "
          "mods/uni/attributes/shade/color.json; "
          "first declared in mods/uni/attributes/color.json");
}

TEST_CASE("duplicate_inline_member") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/value.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:value",)"
              R"("members":[{"identifier":"uni:0"},{"identifier":"uni:0"}]}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/value.json#/attribute/members/1: "
          "duplicate member identifier \"uni:0\" in mods/uni/attributes/value.json; "
          "first declared in mods/uni/attributes/value.json");
}

TEST_CASE("inline_member_with_attribute_key") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/value.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:value",)"
              R"("members":[{"attribute":"uni:value","identifier":"uni:0"}]}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/value.json#/attribute/members/0/attribute: "
          "unknown key \"attribute\" (did you mean \"contribute\")");
}

TEST_CASE("bad_inline_member_keeps_attribute") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/value.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:value",)"
              R"("members":[{"identifier":"uni:Red"}]}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    CHECK_FALSE(outcome.ok());
    CHECK(outcome.schema.attributes.size() == 1);
    CHECK(outcome.schema.members.empty());
}

TEST_CASE("pointer_escapes_slash_and_tilde") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color",)"
              R"("a/b":1,"a~b":2}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 2);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/attribute/a~1b: unknown key \"a/b\"");
    CHECK(FormatDiagnostic(outcome.diagnostics[1]) ==
          "mods/uni/attributes/color.json#/attribute/a~0b: unknown key \"a~b\"");
}

TEST_CASE("file_too_large") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json", std::string(1048577, 'a'));

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/: "
          "file is too large (more than 1048576 bytes)");
}

TEST_CASE("nested_too_deep") {
    CardSchemaTempDir dir;
    std::string text = R"({"format_version":"1.0.0","attribute":)";
    for (int level = 0; level < 70; ++level) {
        text += "[";
    }
    text += "0";
    for (int level = 0; level < 70; ++level) {
        text += "]";
    }
    text += "}";
    dir.Write("uni/attributes/color.json", text);

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/: "
          "schema is nested too deeply (more than 64 levels)");
}

TEST_CASE("duplicate_key") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color",)"
              R"("identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/attribute: duplicate key \"identifier\"");
}

TEST_CASE("symbolic_link_rejected") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/real.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:real"}})");
    std::error_code error;
    std::filesystem::create_symlink(dir.root / "uni/attributes/real.json",
                                    dir.root / "uni/attributes/color.json", error);
    if (error) {
        MESSAGE("skipped: cannot create a symbolic link");
        return;
    }

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.json#/: symbolic links are not allowed");
}

TEST_CASE("uppercase_extension_rejected") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.JSON",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color.JSON#/: only .json files are allowed in attributes/");
}

TEST_CASE("filename_must_match_attribute") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/colour.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/colour.json#/attribute/identifier: "
          "identifier \"uni:color\" does not match the file name \"colour.json\"");
}

TEST_CASE("filename_must_match_member") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color/rede.json",
              R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
              R"("identifier":"uni:red"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/color/rede.json#/attribute_member/identifier: "
          "identifier \"uni:red\" does not match the file name \"rede.json\"");
}

TEST_CASE("missing_format_version_and_wrong_root") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/color.json",
              R"({"attribute":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 2);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/color.json#/format_version: "
          "missing required key \"format_version\"");
    CHECK(FormatDiagnostic(outcome.diagnostics[1]) ==
          "mods/uni/kinds/color.json#/attribute: "
          "root key 'attribute' is not allowed in kinds/");
}

TEST_CASE("invalid_mod_id") {
    CardSchemaTempDir dir;

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "BAD ID");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(outcome.diagnostics[0].message == "invalid mod id \"BAD ID\"");
}

TEST_CASE("unreadable_directory_reported") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");
    const std::filesystem::path attributes = dir.root / "uni" / "attributes";

    std::error_code error;
    std::filesystem::permissions(attributes, std::filesystem::perms::none,
                                 std::filesystem::perm_options::replace, error);
    error.clear();
    const std::filesystem::directory_iterator probe(attributes, error);
    if (!error) {
        std::filesystem::permissions(attributes, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, error);
        MESSAGE("skipped: the directory stays readable (running as root?)");
        return;
    }

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    std::filesystem::permissions(attributes, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(outcome.diagnostics[0].message.find("cannot read directory") != std::string::npos);
}

TEST_CASE("symbolic_link_folder_rejected") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes_real/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color"}})");
    std::error_code error;
    std::filesystem::create_directory_symlink(dir.root / "uni/attributes_real",
                                              dir.root / "uni/attributes", error);
    if (error) {
        MESSAGE("skipped: cannot create a directory symbolic link");
        return;
    }

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes#/: symbolic links are not allowed");
}

TEST_CASE("subdirectory_in_flat_folder_rejected") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/nested/color.json",
              R"({"format_version":"1.0.0","kind":{"identifier":"uni:color"}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/nested#/: subdirectories are not allowed in kinds/");
}

TEST_CASE("root_key_suggestion_is_folder_scoped") {
    CardSchemaTempDir dir;
    dir.Write("uni/kinds/color.json", R"({"format_version":"1.0.0","attribut":{}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/kinds/color.json#/attribut: unknown key \"attribut\"");
}

TEST_CASE("diagnostics_are_capped") {
    CardSchemaTempDir dir;
    std::string text = R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color")";
    for (int index = 0; index < 500; ++index) {
        text += ",\"field" + std::to_string(index) + "\":0";
    }
    text += "}}";
    dir.Write("uni/attributes/color.json", text);

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 101);
    CHECK(outcome.diagnostics[100].message == "more errors not shown");
}

TEST_CASE("rejected_duplicate_attribute_drops_its_members") {
    CardSchemaTempDir dir;
    dir.Write("uni/attributes/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color",)"
              R"("members":[{"identifier":"uni:0"}]}})");
    dir.Write("uni/attributes/shade/color.json",
              R"({"format_version":"1.0.0","attribute":{"identifier":"uni:color",)"
              R"("members":[{"identifier":"uni:1"}]}})");

    const match::cardschema::LoadOutcome outcome = LoadModSchema(dir.root / "uni", "uni");

    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(outcome.diagnostics[0]) ==
          "mods/uni/attributes/shade/color.json#/attribute/identifier: "
          "duplicate attribute identifier \"uni:color\" in "
          "mods/uni/attributes/shade/color.json; "
          "first declared in mods/uni/attributes/color.json");
    REQUIRE(outcome.schema.members.size() == 1);
    CHECK(outcome.schema.members[0].identifier == "uni:0");
}

}  // TEST_SUITE("cardschema loader")
