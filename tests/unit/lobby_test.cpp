#include <doctest/doctest.h>
#include <common/lobby.hpp>
#include <common/contract.hpp>
#include <match/match_instance.hpp>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

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

namespace {

int CountBots(const Lobby& lobby) {
    return static_cast<int>(std::ranges::count_if(lobby.members, [](const LobbyMember& m) {
        return m.is_bot;
    }));
}

}  // namespace

TEST_CASE("lobby: SyncBots tops up bots to settings.bot_count") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Host", nullptr, true, false);
    lobby.settings.bot_count = 3;

    std::mt19937 rng(42);
    lobby.SyncBots(rng);

    CHECK_EQ(lobby.members.size(), 4);
    CHECK_EQ(CountBots(lobby), 3);
}

TEST_CASE("lobby: SyncBots removes bots when bot_count is lowered") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Host", nullptr, true, false);
    lobby.settings.bot_count = 3;

    std::mt19937 rng(42);
    lobby.SyncBots(rng);
    REQUIRE(CountBots(lobby) == 3);

    lobby.settings.bot_count = 1;
    lobby.SyncBots(rng);

    CHECK_EQ(lobby.members.size(), 2);
    CHECK_EQ(CountBots(lobby), 1);
}

TEST_CASE("lobby: SyncBots clamps desired bots to the lobby capacity") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);
    lobby.members.emplace_back("Carol", nullptr, true, false);
    lobby.settings.bot_count = 3;

    std::mt19937 rng(42);
    lobby.SyncBots(rng);

    CHECK_EQ(lobby.members.size(), static_cast<std::size_t>(contract::kMaxLobbyMembers));
    CHECK_EQ(CountBots(lobby), 1);
}

TEST_CASE("lobby: SyncBots is a no-op when a match is in progress") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Host", nullptr, true, false);
    lobby.settings.bot_count = 2;

    std::vector<std::pair<std::string, bool>> players_info{{"Host", false}};
    lobby.match = std::make_unique<match::MatchInstance>(players_info, lobby.settings);

    std::mt19937 rng(42);
    lobby.SyncBots(rng);

    CHECK_EQ(lobby.members.size(), 1);
    CHECK_EQ(CountBots(lobby), 0);
}

TEST_CASE("lobby: SyncBots picks bot names unique against existing members") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.settings.bot_count = 3;

    std::mt19937 rng(1337);
    lobby.SyncBots(rng);

    REQUIRE(lobby.members.size() == 4);
    std::vector<std::string> names;
    for (const auto& m : lobby.members) names.push_back(m.username);
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
}
