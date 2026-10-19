#include <doctest/doctest.h>
#include "match/view/wire.hpp"

#include <string>

using namespace match::view;

TEST_CASE("glaze packet round-trips without nlohmann") {
    MatchOverWire p{"match_over", "player0", {"player0", "player1"}};
    std::string buf; WritePacket(p, buf);
    CHECK(buf.find("match_over") != std::string::npos);
    CHECK(buf.find("player0") != std::string::npos);
}

TEST_CASE("match_over packet writes payouts as an object") {
    MatchOverWire p{"match_over", "player0", {"player0", "player1"},
                    {{"player0", 100}, {"player1", 20}}};
    std::string buf;
    REQUIRE(WritePacket(p, buf));
    CHECK(buf.find(R"("payouts":{"player0":100,"player1":20})")
          != std::string::npos);
}
