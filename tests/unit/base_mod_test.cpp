#include <doctest/doctest.h>

#include <match/modload/base_mod.hpp>

#include <string>

/**
 * @file base_mod_test.cpp
 * @brief Base mod id pair tests: `uni` is accepted beside `vanilla`.
 */

using match::modload::IsBaseModId;
using match::modload::IsSameModId;
using match::modload::ToLegacyBaseKindId;

TEST_SUITE("BaseMod") {

TEST_CASE("IsBaseModId: accepts the uni and the vanilla id") {
    CHECK(IsBaseModId("uni"));
    CHECK(IsBaseModId("vanilla"));
}

TEST_CASE("IsBaseModId: rejects every other id") {
    CHECK_FALSE(IsBaseModId(""));
    CHECK_FALSE(IsBaseModId("seven_zero"));
    CHECK_FALSE(IsBaseModId("unity"));
}

TEST_CASE("IsSameModId: the two base ids name the same mod") {
    CHECK(IsSameModId("uni", "vanilla"));
    CHECK(IsSameModId("vanilla", "uni"));
    CHECK(IsSameModId("uni", "uni"));
    CHECK(IsSameModId("seven_zero", "seven_zero"));
}

TEST_CASE("IsSameModId: a base id differs from every other mod id") {
    CHECK_FALSE(IsSameModId("uni", "seven_zero"));
    CHECK_FALSE(IsSameModId("seven_zero", "vanilla"));
    CHECK_FALSE(IsSameModId("seven_zero", "test"));
}

TEST_CASE("ToLegacyBaseKindId: maps the uni namespace to vanilla") {
    CHECK(ToLegacyBaseKindId("uni:red_0") == "vanilla:red_0");
    CHECK(ToLegacyBaseKindId("vanilla:red_0") == "vanilla:red_0");
}

TEST_CASE("ToLegacyBaseKindId: leaves other namespaces untouched") {
    CHECK(ToLegacyBaseKindId("seven_zero:swap") == "seven_zero:swap");
    CHECK(ToLegacyBaseKindId("unity:red_0") == "unity:red_0");
    CHECK(ToLegacyBaseKindId("uni") == "uni");
}

}  // TEST_SUITE
