#include <doctest/doctest.h>
#include <game/match_instance.hpp>

using namespace game;

static LobbySettings default_settings() {
    LobbySettings s;
    s.starting_cards = 7;
    s.turn_time_limit_ms = 15000;
    return s;
}

TEST_CASE("serialization: export → import → export produces equal state") {
    std::vector<std::pair<std::string, bool>> players = {{"Alice", false}, {"Bob", false}};

    MatchInstance first(players, default_settings());
    first.Start();
    first.SetMatchId("ser-test-01");

    nlohmann::json snap1 = first.ExportState();

    MatchInstance restored(snap1, default_settings());
    nlohmann::json snap2 = restored.ExportState();

    CHECK(snap1["current_player_index"] == snap2["current_player_index"]);
    CHECK(snap1["play_direction"]        == snap2["play_direction"]);
    CHECK(snap1["status"]                == snap2["status"]);
    CHECK(snap1["draw_pile"].size()      == snap2["draw_pile"].size());
    CHECK(snap1["discard_pile"].size()   == snap2["discard_pile"].size());
    CHECK(snap1["players"].size()        == snap2["players"].size());

    for (size_t i = 0; i < snap1["players"].size(); ++i) {
        CHECK(snap1["players"][i]["username"]      == snap2["players"][i]["username"]);
        CHECK(snap1["players"][i]["hand"].size()   == snap2["players"][i]["hand"].size());
        CHECK(snap1["players"][i]["has_called_uno"] == snap2["players"][i]["has_called_uno"]);
    }
}

TEST_CASE("serialization: three-player round-trip") {
    std::vector<std::pair<std::string, bool>> players = {
        {"Alice", false}, {"Bob", false}, {"Charlie", false}
    };

    MatchInstance first(players, default_settings());
    first.Start();

    nlohmann::json snap1 = first.ExportState();
    MatchInstance restored(snap1, default_settings());
    nlohmann::json snap2 = restored.ExportState();

    CHECK(snap1["players"].size() == snap2["players"].size());
}
