#include <doctest/doctest.h>
#include <common/lobby.hpp>
#include <common/contract.hpp>

TEST_CASE("lobby: Sanitize clamps out-of-range numeric fields") {
    LobbySettings settings;
    settings.turn_time_limit_ms = contract::kTurnTimeMinMs - 1;
    settings.starting_cards     = contract::kStartingCardsMax + 1;
    settings.bot_count          = contract::kBotCountMax + 1;

    settings.Sanitize();

    CHECK(settings.turn_time_limit_ms == contract::kTurnTimeMinMs);
    CHECK(settings.starting_cards == contract::kStartingCardsMax);
    CHECK(settings.bot_count == contract::kBotCountMax);
}

TEST_CASE("lobby: Sanitize clamps out-of-range bot_mode") {
    LobbySettings settings;
    settings.bot_mode = static_cast<BotTakeoverMode>(contract::kBotModeMax + 1);

    settings.Sanitize();

    CHECK(static_cast<int>(settings.bot_mode) == contract::kBotModeMax);
}

TEST_CASE("lobby: Sanitize strips unknown mod names") {
    LobbySettings settings;
    settings.active_mods = {"seven_zero", "not_a_real_mod", "force_play"};

    settings.Sanitize();

    CHECK(settings.active_mods == std::vector<std::string>{"seven_zero", "force_play"});
}

TEST_CASE("lobby: Sanitize deduplicates repeated valid mods preserving order") {
    LobbySettings settings;
    settings.active_mods = {"jump_in", "draw_stacking", "jump_in", "progressive", "draw_stacking"};

    settings.Sanitize();

    CHECK(settings.active_mods ==
          std::vector<std::string>{"jump_in", "draw_stacking", "progressive"});
}

TEST_CASE("lobby: Sanitize leaves a full set of valid mods unchanged") {
    LobbySettings settings;
    settings.active_mods = {"seven_zero", "draw_stacking", "force_play", "jump_in", "progressive"};

    settings.Sanitize();

    CHECK(settings.active_mods ==
          std::vector<std::string>{"seven_zero", "draw_stacking", "force_play", "jump_in",
                                    "progressive"});
}
