// Single translation unit that provides doctest's main(). All other test files
// in tests/unit/ just include <doctest/doctest.h> and add TEST_CASEs.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <database.hpp>

// INFO: Open an in-memory DB once so lobby/auth controller tests can exercise
//       DB-backed paths without a real file.
namespace {
struct TestDBInit {
    TestDBInit() {
        (void)Database::Get().Open(":memory:");
        // 32 zero bytes, base64-encoded — a fixed key is fine for tests, it
        // never needs to match anything outside this process.
        setenv("CHAT_DM_KEY", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", 1);
    }
} g_db_init;
}
