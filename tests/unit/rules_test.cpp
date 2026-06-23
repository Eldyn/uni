#include <doctest/doctest.h>
#include <game/match_instance.hpp>

using namespace game;

static LobbySettings settings_with_mod(const std::string& mod) {
    LobbySettings s;
    s.starting_cards = 7;
    s.turn_time_limit_ms = 15000;
    s.active_mods.push_back(mod);
    return s;
}

static std::vector<std::pair<std::string, bool>> two_humans() {
    return {{"Alice", false}, {"Bob", false}};
}

TEST_CASE("rules: draw_stacking mod loads without crash") {
    MatchInstance m(two_humans(), settings_with_mod("draw_stacking"));
    CHECK_NOTHROW(m.Start());
    CHECK_FALSE(m.IsGameOver());
}

TEST_CASE("rules: progressive mod loads without crash") {
    MatchInstance m(two_humans(), settings_with_mod("progressive"));
    CHECK_NOTHROW(m.Start());
    CHECK_FALSE(m.IsGameOver());
}

TEST_CASE("rules: no_bluffing mod loads without crash") {
    MatchInstance m(two_humans(), settings_with_mod("no_bluffing"));
    CHECK_NOTHROW(m.Start());
    CHECK_FALSE(m.IsGameOver());
}

TEST_CASE("rules: force_play mod loads without crash") {
    MatchInstance m(two_humans(), settings_with_mod("force_play"));
    CHECK_NOTHROW(m.Start());
    CHECK_FALSE(m.IsGameOver());
}

TEST_CASE("rules: jump_in mod loads without crash") {
    MatchInstance m(two_humans(), settings_with_mod("jump_in"));
    CHECK_NOTHROW(m.Start());
    CHECK_FALSE(m.IsGameOver());
}

TEST_CASE("rules: seven_zero mod loads without crash") {
    MatchInstance m(two_humans(), settings_with_mod("seven_zero"));
    CHECK_NOTHROW(m.Start());
    CHECK_FALSE(m.IsGameOver());
}
