#include <doctest/doctest.h>

#include <string>

#include <match/cardschema/model.hpp>

TEST_SUITE("cardschema model") {

TEST_CASE("namespace_and_local") {
    CHECK(match::cardschema::NamespaceOf("uni:red") == "uni");
    CHECK(match::cardschema::LocalNameOf("uni:red") == "red");
    CHECK(match::cardschema::NamespaceOf("red") == "");
}

TEST_CASE("valid_local_names") {
    CHECK(match::cardschema::IsValidLocalName("red_5"));
    CHECK(match::cardschema::IsValidLocalName("0"));
    CHECK_FALSE(match::cardschema::IsValidLocalName("Red"));
    CHECK_FALSE(match::cardschema::IsValidLocalName(""));
    CHECK_FALSE(match::cardschema::IsValidLocalName("a-b"));
    CHECK_FALSE(match::cardschema::IsValidLocalName(std::string(33, 'a')));
}

TEST_CASE("valid_identifier") {
    CHECK(match::cardschema::IsValidIdentifier("uni:red"));
    CHECK_FALSE(match::cardschema::IsValidIdentifier("red"));
    CHECK_FALSE(match::cardschema::IsValidIdentifier("uni:"));
    CHECK_FALSE(match::cardschema::IsValidIdentifier(":red"));
    CHECK_FALSE(match::cardschema::IsValidIdentifier("a:b:c"));
}

}  // TEST_SUITE("cardschema model")
