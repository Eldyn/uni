#include <doctest/doctest.h>
#include <common/lobby.hpp>
#include <common/contract.hpp>
#include <match/match_instance.hpp>
#include <algorithm>
#include <cctype>
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

TEST_CASE("lobby: Sanitize clamps max_players to [2, contract::kMaxLobbyMembers] by default") {
    LobbySettings low;
    low.max_players = 1;
    low.Sanitize();
    CHECK_EQ(low.max_players, 2);

    LobbySettings high;
    high.max_players = contract::kMaxLobbyMembers + 5;
    high.Sanitize();
    CHECK_EQ(high.max_players, contract::kMaxLobbyMembers);
}

TEST_CASE("lobby: Sanitize clamps max_players against a caller-supplied ceiling") {
    LobbySettings settings;
    settings.max_players = 10;

    settings.Sanitize(6);

    CHECK_EQ(settings.max_players, 6);
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

TEST_CASE("lobby: SyncBots clamps desired bots to the lobby's max_players") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.max_players = 10;
    int human_count = lobby.settings.max_players - 1;
    for (int i = 0; i < human_count; ++i)
        lobby.members.emplace_back("Human" + std::to_string(i), nullptr, true, false);
    lobby.settings.bot_count = human_count;

    std::mt19937 rng(42);
    lobby.SyncBots(rng);

    CHECK_EQ(lobby.members.size(), static_cast<std::size_t>(lobby.settings.max_players));
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

TEST_CASE("lobby: GenerateInviteCode produces a 6-character alphanumeric code") {
    std::string code = Lobby::GenerateInviteCode();

    CHECK_EQ(code.size(), 6);
    CHECK(std::ranges::all_of(code, [](char c) { return std::isalnum(static_cast<unsigned char>(c)); }));
}

TEST_CASE("lobby: RemoveMember erases a member with no match in progress") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Alice", rng);

    CHECK(result.found);
    CHECK_EQ(result.match_outcome, MemberRemovalOutcome::kMatchUnaffected);
    CHECK_EQ(lobby.members.size(), 1);
    CHECK_EQ(lobby.members[0].username, "Bob");
}

TEST_CASE("lobby: RemoveMember reports not found for an unknown username") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Alice", nullptr, true, false);

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Ghost", rng);

    CHECK_FALSE(result.found);
    CHECK_EQ(lobby.members.size(), 1);
}

TEST_CASE("lobby: RemoveMember aborts the match when quit_deletes_match is set") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.quit_deletes_match = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    std::vector<std::pair<std::string, bool>> players_info{{"Alice", false}, {"Bob", false}};
    lobby.match = std::make_unique<match::MatchInstance>(players_info, lobby.settings);

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Alice", rng);

    CHECK_EQ(result.match_outcome, MemberRemovalOutcome::kMatchAborted);
    // match is deliberately left intact so the caller can persist its state
    // before tearing it down itself.
    CHECK(lobby.match);
    CHECK_EQ(lobby.members.size(), 1);
}

TEST_CASE("lobby: RemoveMember replaces the departing player with a bot") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_replacement = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    std::vector<std::pair<std::string, bool>> players_info{{"Alice", false}, {"Bob", false}};
    lobby.match = std::make_unique<match::MatchInstance>(players_info, lobby.settings);

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Alice", rng);

    CHECK_EQ(result.match_outcome, MemberRemovalOutcome::kPlayerReplacedByBot);
    CHECK_FALSE(result.new_bot_name.empty());
    REQUIRE(lobby.match);
    CHECK_EQ(lobby.members.size(), 2);
    bool bot_present = false;
    for (const auto& m : lobby.members)
        if (m.username == result.new_bot_name && m.is_bot) bot_present = true;
    CHECK(bot_present);
}

TEST_CASE("lobby: RemoveMember replacing the current turn-holder reports was_their_turn") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_replacement = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    std::vector<std::pair<std::string, bool>> players_info{{"Alice", false}, {"Bob", false}};
    lobby.match = std::make_unique<match::MatchInstance>(players_info, lobby.settings);
    std::string turn_holder = lobby.match->GetCurrentPlayerUsername();

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember(turn_holder, rng);

    CHECK(result.was_their_turn);
}

TEST_CASE("lobby: RemoveMember replacing a non-turn-holder reports was_their_turn false") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_replacement = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    std::vector<std::pair<std::string, bool>> players_info{{"Alice", false}, {"Bob", false}};
    lobby.match = std::make_unique<match::MatchInstance>(players_info, lobby.settings);
    std::string turn_holder = lobby.match->GetCurrentPlayerUsername();
    std::string other = (turn_holder == "Alice") ? "Bob" : "Alice";

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember(other, rng);

    CHECK_FALSE(result.was_their_turn);
}

TEST_CASE("lobby: RemoveMember drops the player from the engine when neither policy applies") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.quit_deletes_match = false;
    lobby.settings.allow_bot_replacement = false;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    std::vector<std::pair<std::string, bool>> players_info{{"Alice", false}, {"Bob", false}};
    lobby.match = std::make_unique<match::MatchInstance>(players_info, lobby.settings);

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Alice", rng);

    CHECK_EQ(result.match_outcome, MemberRemovalOutcome::kPlayerDroppedFromEngine);
    REQUIRE(lobby.match);
    CHECK_EQ(lobby.members.size(), 1);
}

TEST_CASE("lobby: PromoteNextHost promotes the first connected non-bot member") {
    Lobby lobby;
    lobby.id = 1;
    lobby.host = "Alice";
    lobby.members.emplace_back("Bot1", nullptr, true, true);
    lobby.members.emplace_back("Bob", nullptr, false, false);
    lobby.members.emplace_back("Carol", nullptr, true, false);

    bool promoted = lobby.PromoteNextHost();

    CHECK(promoted);
    CHECK_EQ(lobby.host, "Carol");
}

TEST_CASE("lobby: PromoteNextHost is a no-op when no eligible member exists") {
    Lobby lobby;
    lobby.id = 1;
    lobby.host = "Alice";
    lobby.members.emplace_back("Bot1", nullptr, true, true);
    lobby.members.emplace_back("Bob", nullptr, false, false);

    bool promoted = lobby.PromoteNextHost();

    CHECK_FALSE(promoted);
    CHECK_EQ(lobby.host, "Alice");
}

TEST_CASE("lobby: AddOrHijack hijacks an existing bot slot") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_takeover = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bot1", nullptr, true, true);

    auto result = lobby.AddOrHijack("Charlie", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kHijackedBot);
    CHECK_EQ(result.old_bot_name, "Bot1");
    CHECK_EQ(lobby.members.size(), 2);
    bool charlie_present = false;
    for (const auto& m : lobby.members)
        if (m.username == "Charlie" && !m.is_bot) charlie_present = true;
    CHECK(charlie_present);
}

TEST_CASE("lobby: AddOrHijack renames the engine-side player when hijacking mid-match") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_takeover = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bot1", nullptr, true, true);

    std::vector<std::pair<std::string, bool>> players_info{{"Alice", false}, {"Bot1", true}};
    lobby.match = std::make_unique<match::MatchInstance>(players_info, lobby.settings);

    auto result = lobby.AddOrHijack("Charlie", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kHijackedBot);
    match::Player* engine_player = lobby.match->GetPlayer("Charlie");
    REQUIRE(engine_player);
    CHECK_FALSE(engine_player->is_bot);
    CHECK_FALSE(lobby.match->GetPlayer("Bot1"));
}

TEST_CASE("lobby: AddOrHijack fills an empty slot when no bots are hijackable") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Alice", nullptr, true, false);

    auto result = lobby.AddOrHijack("Bob", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kJoinedEmptySlot);
    CHECK_EQ(lobby.members.size(), 2);
}

TEST_CASE("lobby: AddOrHijack reports full when at the lobby's max_players capacity") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.max_players = 8;
    for (int i = 0; i < lobby.settings.max_players; ++i)
        lobby.members.emplace_back("Player" + std::to_string(i), nullptr, true, false);

    auto result = lobby.AddOrHijack("Overflow", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kLobbyFull);
    CHECK_EQ(lobby.members.size(), static_cast<std::size_t>(lobby.settings.max_players));
}

TEST_CASE("lobby: AddOrHijack allows more than 4 members up to max_players") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.max_players = 6;
    for (int i = 0; i < 5; ++i)
        lobby.members.emplace_back("Player" + std::to_string(i), nullptr, true, false);

    auto result = lobby.AddOrHijack("Sixth", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kJoinedEmptySlot);
    CHECK_EQ(lobby.members.size(), 6);
}

TEST_CASE("lobby: seat_index survives leave/join, lowest free seat is reused first") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_takeover = false;

    lobby.AddOrHijack("Alice", nullptr);
    lobby.AddOrHijack("Bob", nullptr);
    lobby.AddOrHijack("Carol", nullptr);
    lobby.AddOrHijack("Dave", nullptr);
    REQUIRE_EQ(lobby.members.size(), 4);

    auto seat_of = [&](const std::string& name) {
        auto it = std::ranges::find(lobby.members, name, &LobbyMember::username);
        REQUIRE(it != lobby.members.end());
        return it->seat_index;
    };
    CHECK_EQ(seat_of("Alice"), 0);
    CHECK_EQ(seat_of("Bob"), 1);
    CHECK_EQ(seat_of("Carol"), 2);
    CHECK_EQ(seat_of("Dave"), 3);

    std::mt19937 rng(42);
    lobby.RemoveMember("Bob", rng);
    lobby.RemoveMember("Dave", rng);
    REQUIRE_EQ(lobby.members.size(), 2);
    CHECK_EQ(seat_of("Alice"), 0);
    CHECK_EQ(seat_of("Carol"), 2);

    lobby.AddOrHijack("Eve", nullptr);
    lobby.AddOrHijack("Frank", nullptr);
    REQUIRE_EQ(lobby.members.size(), 4);

    CHECK_EQ(seat_of("Eve"), 1);
    CHECK_EQ(seat_of("Frank"), 3);
    CHECK_EQ(seat_of("Alice"), 0);
    CHECK_EQ(seat_of("Carol"), 2);
}

TEST_CASE("lobby: AddOrHijack preserves seat_index when hijacking a bot slot") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_takeover = true;
    lobby.members.emplace_back("Alice", nullptr, true, false, 0);
    lobby.members.emplace_back("Bot1", nullptr, true, true, 1);
    lobby.members.emplace_back("Carol", nullptr, true, false, 2);

    auto result = lobby.AddOrHijack("Charlie", nullptr);
    CHECK_EQ(result.outcome, JoinOutcome::kHijackedBot);

    auto it = std::ranges::find(lobby.members, "Charlie", &LobbyMember::username);
    REQUIRE(it != lobby.members.end());
    CHECK_EQ(it->seat_index, 1);
}

TEST_CASE("lobby: Create builds a sanitized lobby with the host as first member") {
    Lobby lobby = Lobby::Create(7, "Alice", nullptr, true, "Alice's Room",
                                 contract::kTurnTimeMinMs - 1, contract::kStartingCardsMax + 1,
                                 [](const std::string&) { return false; });

    CHECK_EQ(lobby.id, 7);
    CHECK_EQ(lobby.host, "Alice");
    CHECK_EQ(lobby.name, "Alice's Room");
    CHECK(lobby.settings.is_public);
    CHECK_EQ(lobby.settings.turn_time_limit_ms, contract::kTurnTimeMinMs);
    CHECK_EQ(lobby.settings.starting_cards, contract::kStartingCardsMax);
    REQUIRE_EQ(lobby.members.size(), 1);
    CHECK_EQ(lobby.members[0].username, "Alice");
    CHECK_FALSE(lobby.invite_code.empty());
}

TEST_CASE("lobby: Create retries on invite code collision") {
    int attempts = 0;
    Lobby lobby = Lobby::Create(1, "Alice", nullptr, false, "Room", 15000, 7,
                                 [&](const std::string&) { return ++attempts < 3; });

    CHECK_GE(attempts, 3);
    CHECK_FALSE(lobby.invite_code.empty());
}

TEST_CASE("lobby: Create throws after exhausting collision retries") {
    CHECK_THROWS_AS(
        Lobby::Create(1, "Alice", nullptr, false, "Room", 15000, 7,
                      [](const std::string&) { return true; }),
        std::runtime_error);
}

TEST_CASE("lobby: CollectExpiredDisconnects returns members past the grace window") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Alice", nullptr, false, false);
    lobby.members.back().disconnected_at = std::chrono::steady_clock::now() - std::chrono::hours(1);
    lobby.members.emplace_back("Bob", nullptr, true, false);
    lobby.members.emplace_back("Bot1", nullptr, false, true);

    auto expired = lobby.CollectExpiredDisconnects(std::chrono::steady_clock::now(), 30'000);

    REQUIRE_EQ(expired.size(), 1);
    CHECK_EQ(expired[0], "Alice");
}

TEST_CASE("lobby: CollectExpiredDisconnects excludes members within grace") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Alice", nullptr, false, false);
    lobby.members.back().disconnected_at = std::chrono::steady_clock::now();

    auto expired = lobby.CollectExpiredDisconnects(std::chrono::steady_clock::now(), 30'000);

    CHECK(expired.empty());
}
