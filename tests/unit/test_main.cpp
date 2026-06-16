// Single translation unit that provides doctest's main(). All other test files
// in tests/unit/ just include <doctest/doctest.h> and add TEST_CASEs.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("doctest harness is wired up") {
    CHECK(1 + 1 == 2);
}
