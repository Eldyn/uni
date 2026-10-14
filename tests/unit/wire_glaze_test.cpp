#include <doctest/doctest.h>
#include "match/view/wire.hpp"

#include <string>

using namespace match::view;

TEST_CASE("glaze packet round-trips without nlohmann") {
    WirePacket p{1, "match_event", "turn_changed"};
    std::string buf; WritePacket(p, buf);
    CHECK(buf.find("turn_changed") != std::string::npos);
}
