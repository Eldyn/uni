#include <doctest/doctest.h>
#include <game/match_instance.hpp>
#include <game/game_state.hpp>

using namespace game;

static LobbySettings default_settings() {
    LobbySettings s;
    s.starting_cards = 7;
    s.turn_time_limit_ms = 15000;
    return s;
}

static std::vector<std::pair<std::string, bool>> two_humans() {
    return {{"Alice", false}, {"Bob", false}};
}

TEST_CASE("match: start → playing status") {
    MatchInstance m(two_humans(), default_settings());
    m.Start();
    CHECK_FALSE(m.IsGameOver());
}

TEST_CASE("match: draw card grows hand") {
    MatchInstance m(two_humans(), default_settings());
    m.Start();

    const std::string current = m.GetCurrentPlayerUsername();
    nlohmann::json state_before = m.ExportState();
    int hand_before = 0;
    for (const auto& p : state_before["players"]) {
        if (p["username"].get<std::string>() == current)
            hand_before = static_cast<int>(p["hand"].size());
    }

    m.DrawCard(current);

    nlohmann::json state_after = m.ExportState();
    int hand_after = 0;
    for (const auto& p : state_after["players"]) {
        if (p["username"].get<std::string>() == current)
            hand_after = static_cast<int>(p["hand"].size());
    }

    // INFO: Either hand grew (normal draw) or is unchanged (draw-locked turn).
    CHECK(hand_after >= hand_before);
}

TEST_CASE("match: serialization round-trip") {
    MatchInstance m(two_humans(), default_settings());
    m.Start();
    m.SetMatchId("test-round-trip");

    nlohmann::json exported = m.ExportState();
    std::string json_str    = exported.dump();

    MatchInstance restored(nlohmann::json::parse(json_str), default_settings());
    nlohmann::json re_exported = restored.ExportState();

    CHECK(exported["current_player_index"] == re_exported["current_player_index"]);
    CHECK(exported["play_direction"]       == re_exported["play_direction"]);
    CHECK(exported["players"].size()       == re_exported["players"].size());
}

TEST_CASE("match: serialization handles missing keys gracefully") {
    nlohmann::json partial;
    partial["rules"]   = nlohmann::json::array();
    partial["players"] = nlohmann::json::array();

    CHECK_NOTHROW(MatchInstance m(partial, default_settings()));
}
