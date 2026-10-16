#include <doctest/doctest.h>
#include "common/crypto_pool.hpp"
TEST_CASE("crypto pool verifies a fresh hash") {
    auto h = CryptoPool::Get().HashPasswordAsync("pw-loadtest-1").get();
    REQUIRE(h.has_value());
    CHECK(CryptoPool::Get().VerifyPasswordAsync("pw-loadtest-1", *h).get() == true);
    CHECK(CryptoPool::Get().VerifyPasswordAsync("wrong", *h).get() == false);
}
