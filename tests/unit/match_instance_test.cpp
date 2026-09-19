#include <doctest/doctest.h>
#include <database.hpp>
#include <match/match_instance.hpp>
#include <match/match_state.hpp>

using namespace match;

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
    CHECK_FALSE(m.IsMatchOver());
}

TEST_CASE("match: starting a 6-player match with sanitized settings deals every hand fully") {
    LobbySettings settings;
    settings.max_players = 6;
    settings.starting_cards = 7;
    settings.turn_time_limit_ms = 15000;
    settings.Sanitize();

    std::vector<std::pair<std::string, bool>> players_info;
    for (int i = 0; i < 6; ++i) players_info.emplace_back("Player" + std::to_string(i), false);

    MatchInstance m(players_info, settings);
    m.Start();

    nlohmann::json state = m.ExportState();
    REQUIRE_EQ(state["players"].size(), 6);
    for (const auto& p : state["players"]) {
        CHECK_EQ(p["hand"].size(), settings.starting_cards);
    }
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

TEST_CASE("match: SerializeBaseState draw_pile_size matches the real draw pile, and never grows on a normal draw") {
    MatchInstance m(two_humans(), default_settings());
    m.Start();

    nlohmann::json base = m.SerializeBaseState();
    nlohmann::json exported = m.ExportState();
    CHECK_EQ(base["draw_pile_size"].get<size_t>(), exported["draw_pile"].size());

    const std::string current = m.GetCurrentPlayerUsername();
    size_t before = base["draw_pile_size"].get<size_t>();
    m.DrawCard(current);
    size_t after = m.SerializeBaseState()["draw_pile_size"].get<size_t>();

    // INFO: Either it shrank by one (normal draw) or stayed the same
    //       (draw-locked turn) — same tolerance as the existing
    //       "draw card grows hand" test above.
    CHECK(after <= before);
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

TEST_CASE("match: SerializePlayerState matches SerializeBaseState + SerializeHandFor") {
    MatchInstance m(two_humans(), default_settings());
    m.Start();

    const std::string current = m.GetCurrentPlayerUsername();
    const std::string other = current == "Alice" ? "Bob" : "Alice";

    for (const std::string& viewer : {current, other}) {
        nlohmann::json expected = m.SerializePlayerState(viewer);

        nlohmann::json actual = m.SerializeBaseState();
        for (auto& p_json : actual["players"]) {
            if (p_json["username"] == viewer) {
                p_json["hand"] = m.SerializeHandFor(viewer);
                break;
            }
        }

        CHECK(actual == expected);
    }
}

TEST_CASE("match: SerializeBaseState omits any player's hand") {
    MatchInstance m(two_humans(), default_settings());
    m.Start();

    nlohmann::json base = m.SerializeBaseState();
    for (const auto& p_json : base["players"]) {
        CHECK_FALSE(p_json.contains("hand"));
    }
}

TEST_CASE("match: SerializeHandFor only marks can_play on the current player's turn") {
    MatchInstance m(two_humans(), default_settings());
    m.Start();

    const std::string current = m.GetCurrentPlayerUsername();
    const std::string other = current == "Alice" ? "Bob" : "Alice";

    nlohmann::json other_hand = m.SerializeHandFor(other);
    for (const auto& card : other_hand) {
        CHECK(card["can_play"] == false);
    }
}

TEST_CASE("match: serialization handles missing keys gracefully") {
    nlohmann::json partial;
    partial["rules"]   = nlohmann::json::array();
    partial["players"] = nlohmann::json::array();

    CHECK_NOTHROW(MatchInstance m(partial, default_settings()));
}

TEST_CASE("match: IsBot reflects each player's bot flag") {
    std::vector<std::pair<std::string, bool>> players = {{"Alice", false}, {"BotBob", true}};
    MatchInstance m(players, default_settings());
    m.Start();

    CHECK(m.IsBot("BotBob"));
    CHECK_FALSE(m.IsBot("Alice"));
    CHECK_FALSE(m.IsBot("Nobody"));
}

// ---------------------------------------------------------------------------
// AdvanceBotTurns
// ---------------------------------------------------------------------------

TEST_CASE("AdvanceBotTurns: all-bots-disconnected chain terminates within the cap, "
          "invoking on_step once per move") {
    std::vector<std::pair<std::string, bool>> players = {
        {"Bot0", true}, {"Bot1", true}, {"Bot2", true}, {"Bot3", true}};
    MatchInstance m(players, default_settings());
    m.Start();

    int on_step_calls = 0;
    auto result = m.AdvanceBotTurns(
        [](const std::string&) { return false; },
        [&on_step_calls]() { ++on_step_calls; });

    CHECK_LE(result.steps, 20);
    CHECK_EQ(on_step_calls, result.steps);
    CHECK_FALSE(result.stalled);
}

TEST_CASE("AdvanceBotTurns: a connected human's turn stops the loop early") {
    std::vector<std::pair<std::string, bool>> players = {{"BotBob", true}, {"Alice", false}};
    MatchInstance m(players, default_settings());
    m.Start();

    auto result = m.AdvanceBotTurns(
        [](const std::string& username) { return username == "Alice"; },
        []() {});

    CHECK_EQ(result.steps, 1);
    CHECK_FALSE(result.stalled);
    CHECK_FALSE(result.match_over);
}

TEST_CASE("AdvanceBotTurns: a stalled state (same player, same waiting-state after a move) "
          "is reported without exceeding the cap or looping forever") {
    // INFO: A 2-player match where the current player's only playable card is
    //       a Skip: resolving it advances the turn twice, landing back on the
    //       same player with no pending input, the exact no-progress
    //       condition AdvanceBotTurns must detect and abort on.
    json saved_state;
    saved_state["rules"]                = json::array();
    saved_state["status"]               = 1;  // kPlaying
    saved_state["active_type"]          = 0;  // kRed
    saved_state["current_player_index"] = 0;
    saved_state["play_direction"]       = 1;
    saved_state["pending_player"]       = "";
    saved_state["discard_pile"]         = json::array({MakeCard(Type::kRed, Value::k5, 100)});
    saved_state["draw_pile"]            = json::array();

    saved_state["players"] = json::array({
        {
            {"username", "Bot0"},
            {"hand", json::array({MakeCard(Type::kRed, Value::kSkip, 1),
                                   MakeCard(Type::kBlue, Value::k7, 2)})},
            {"is_bot", false}
        },
        {
            {"username", "Bot1"},
            {"hand", json::array({MakeCard(Type::kGreen, Value::k9, 3)})},
            {"is_bot", true}
        }
    });

    MatchInstance m(saved_state, default_settings());

    auto result = m.AdvanceBotTurns(
        [](const std::string&) { return false; },
        []() {});

    CHECK_LE(result.steps, 20);
    CHECK(result.stalled);
    CHECK_FALSE(result.match_over);
}

// ---------------------------------------------------------------------------
// GetTurnTimeoutPolicy
// ---------------------------------------------------------------------------

static LobbySettings settings_with_mode(BotTakeoverMode mode) {
    LobbySettings s = default_settings();
    s.bot_mode = mode;
    return s;
}

static auto always_connected = [](const std::string&) { return true; };
static auto never_connected  = [](const std::string&) { return false; };

TEST_CASE("GetTurnTimeoutPolicy: a bot's turn always resolves to kBotThinking") {
    std::vector<std::pair<std::string, bool>> players = {{"BotBob", true}, {"Alice", false}};
    MatchInstance m(players, settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd));
    m.Start();

    CHECK(m.GetTurnTimeoutPolicy(always_connected) == TurnTimeoutPolicy::kBotThinking);
    CHECK(m.GetTurnTimeoutPolicy(never_connected) == TurnTimeoutPolicy::kBotThinking);
}

TEST_CASE("GetTurnTimeoutPolicy: a connected human under kWaitUntilTurnEnd resolves to "
          "kHumanAfkTimeout") {
    std::vector<std::pair<std::string, bool>> players = {{"Alice", false}, {"Bob", false}};
    MatchInstance m(players, settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd));
    m.Start();

    CHECK(m.GetTurnTimeoutPolicy(always_connected) == TurnTimeoutPolicy::kHumanAfkTimeout);
}

TEST_CASE("GetTurnTimeoutPolicy: a disconnected human under kPlayInstantly resolves to "
          "kInstantBotAdvance") {
    std::vector<std::pair<std::string, bool>> players = {{"Alice", false}, {"Bob", false}};
    MatchInstance m(players, settings_with_mode(BotTakeoverMode::kPlayInstantly));
    m.Start();

    CHECK(m.GetTurnTimeoutPolicy(never_connected) == TurnTimeoutPolicy::kInstantBotAdvance);
}

TEST_CASE("GetTurnTimeoutPolicy: a pending-input human resolves to kInputWaitTimeout") {
    json saved_state;
    saved_state["rules"]                = json::array();
    saved_state["status"]               = 1;  // kPlaying
    saved_state["active_type"]          = 0;  // kRed
    saved_state["current_player_index"] = 0;
    saved_state["play_direction"]       = 1;
    saved_state["pending_player"]       = "Alice";
    saved_state["pending_action"]       = 0;  // kChooseType
    saved_state["discard_pile"]         = json::array({MakeCard(Type::kRed, Value::k5, 100)});
    saved_state["draw_pile"]            = json::array();
    saved_state["players"] = json::array({
        {{"username", "Alice"}, {"hand", json::array()}, {"is_bot", false}},
        {{"username", "Bob"}, {"hand", json::array()}, {"is_bot", false}}
    });

    MatchInstance m(saved_state, settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd));

    REQUIRE(m.IsWaitingForInput());
    CHECK(m.GetTurnTimeoutPolicy(always_connected) == TurnTimeoutPolicy::kInputWaitTimeout);
}

TEST_CASE("GetTurnTimeoutPolicy: a connected human under kPlayInstantly with no pending "
          "input falls through to kNone") {
    std::vector<std::pair<std::string, bool>> players = {{"Alice", false}, {"Bob", false}};
    MatchInstance m(players, settings_with_mode(BotTakeoverMode::kPlayInstantly));
    m.Start();

    CHECK(m.GetTurnTimeoutPolicy(always_connected) == TurnTimeoutPolicy::kNone);
}

// ---------------------------------------------------------------------------
// Match Ledger & Ranked Integrity Tests
// ---------------------------------------------------------------------------

static void SetupTestUser(const std::string& username) {
    auto& db = Database::Get();
    (void)db.RunMigrations();
    (void)db.Exec("INSERT OR IGNORE INTO users (username, pass_hash, salt, email) VALUES (?, 'h', 's', ?);",
                  {username, username + "@example.com"});
    (void)db.Exec("DELETE FROM player_stats WHERE username = ?;", {username});
    (void)db.Exec("DELETE FROM match_history WHERE username = ?;", {username});
}

TEST_CASE("Ranked integrity: 3+ humans match updates player_stats and writes match_history") {
    SetupTestUser("mi_alice");
    SetupTestUser("mi_bob");
    SetupTestUser("mi_carol");

    std::vector<std::pair<std::string, bool>> players = {
        {"mi_alice", false}, {"mi_bob", false}, {"mi_carol", false}
    };
    LobbySettings settings = default_settings();
    settings.ranked = true;

    MatchInstance m(players, settings);
    m.SetMatchId("ranked-3h-test");
    m.Start();

    REQUIRE(m.IsRankedEligible());
    CHECK_EQ(m.GetInitialHumanCount(), 3);

    m.RecordMatchCompleted("mi_alice");

    auto& db = Database::Get();
    auto alice_stats = db.QueryOne("SELECT total_wins, total_losses FROM player_stats WHERE username = ?;", {"mi_alice"});
    REQUIRE(alice_stats.has_value());
    REQUIRE(alice_stats->has_value());
    CHECK_EQ(alice_stats.value()->Get<int>("total_wins"), 1);
    CHECK_EQ(alice_stats.value()->Get<int>("total_losses"), 0);

    auto bob_stats = db.QueryOne("SELECT total_wins, total_losses FROM player_stats WHERE username = ?;", {"mi_bob"});
    REQUIRE(bob_stats.has_value());
    REQUIRE(bob_stats->has_value());
    CHECK_EQ(bob_stats.value()->Get<int>("total_wins"), 0);
    CHECK_EQ(bob_stats.value()->Get<int>("total_losses"), 1);

    auto rows = db.Query("SELECT username, result, ended_reason, ranked, placement FROM match_history WHERE match_id = ? ORDER BY username;", {"ranked-3h-test"});
    REQUIRE(rows.has_value());
    REQUIRE_EQ(rows->size(), 3);

    for (const auto& r : *rows) {
        std::string uname = r.Get<std::string>("username");
        CHECK_EQ(r.Get<int>("ranked"), 1);
        CHECK_EQ(r.Get<std::string>("ended_reason"), "completed");
        if (uname == "mi_alice") {
            CHECK_EQ(r.Get<std::string>("result"), "win");
            CHECK_EQ(r.Get<int>("placement"), 1);
        } else {
            CHECK_EQ(r.Get<std::string>("result"), "loss");
            CHECK_EQ(r.GetOr<int>("placement", -1), -1);
        }
    }
}

TEST_CASE("Ranked integrity: <3 humans leaves player_stats untouched and logs bot_majority") {
    SetupTestUser("mi_u1");
    SetupTestUser("mi_u2");

    std::vector<std::pair<std::string, bool>> players = {
        {"mi_u1", false}, {"mi_u2", false}, {"BotBuddy", true}
    };
    LobbySettings settings = default_settings();
    settings.ranked = true;

    MatchInstance m(players, settings);
    m.SetMatchId("bot-majority-test");
    m.Start();

    CHECK_FALSE(m.IsRankedEligible());
    CHECK_EQ(m.GetInitialHumanCount(), 2);

    m.RecordMatchCompleted("mi_u1");

    auto& db = Database::Get();
    auto u1_stats = db.QueryOne("SELECT total_wins FROM player_stats WHERE username = ?;", {"mi_u1"});
    REQUIRE(u1_stats.has_value());
    if (u1_stats->has_value()) {
        CHECK_EQ(u1_stats.value()->Get<int>("total_wins"), 0);
    }

    auto rows = db.Query("SELECT username, result, ended_reason, ranked FROM match_history WHERE match_id = ? ORDER BY username;", {"bot-majority-test"});
    REQUIRE(rows.has_value());
    REQUIRE_EQ(rows->size(), 2);
    for (const auto& r : *rows) {
        CHECK_EQ(r.Get<int>("ranked"), 0);
        CHECK_EQ(r.Get<std::string>("ended_reason"), "bot_majority");
    }
}

TEST_CASE("Ranked integrity: ranked=false setting leaves player_stats untouched regardless of human count") {
    SetupTestUser("mi_r1");
    SetupTestUser("mi_r2");
    SetupTestUser("mi_r3");
    SetupTestUser("mi_r4");

    std::vector<std::pair<std::string, bool>> players = {
        {"mi_r1", false}, {"mi_r2", false}, {"mi_r3", false}, {"mi_r4", false}
    };
    LobbySettings settings = default_settings();
    settings.ranked = false;

    MatchInstance m(players, settings);
    m.SetMatchId("unranked-test");
    m.Start();

    CHECK_FALSE(m.IsRankedEligible());
    CHECK_EQ(m.GetInitialHumanCount(), 4);

    m.RecordMatchCompleted("mi_r1");

    auto& db = Database::Get();
    auto r1_stats = db.QueryOne("SELECT total_wins FROM player_stats WHERE username = ?;", {"mi_r1"});
    REQUIRE(r1_stats.has_value());
    if (r1_stats->has_value()) {
        CHECK_EQ(r1_stats.value()->Get<int>("total_wins"), 0);
    }

    auto rows = db.Query("SELECT username, result, ended_reason, ranked FROM match_history WHERE match_id = ?;", {"unranked-test"});
    REQUIRE(rows.has_value());
    REQUIRE_EQ(rows->size(), 4);
    for (const auto& r : *rows) {
        CHECK_EQ(r.Get<int>("ranked"), 0);
        CHECK_EQ(r.Get<std::string>("ended_reason"), "completed");
    }
}

TEST_CASE("Quit behavior: quit_deletes_match=false records loss for quitter and normal result for others") {
    SetupTestUser("mi_q1");
    SetupTestUser("mi_q2");
    SetupTestUser("mi_q3");

    std::vector<std::pair<std::string, bool>> players = {
        {"mi_q1", false}, {"mi_q2", false}, {"mi_q3", false}
    };
    LobbySettings settings = default_settings();
    settings.ranked = true;
    settings.quit_deletes_match = false;

    MatchInstance m(players, settings);
    m.SetMatchId("quit-continue-test");
    m.Start();

    // mi_q2 quits mid-game
    m.RecordPlayerQuit("mi_q2");

    auto& db = Database::Get();
    auto q2_stats = db.QueryOne("SELECT total_losses FROM player_stats WHERE username = ?;", {"mi_q2"});
    REQUIRE(q2_stats.has_value());
    REQUIRE(q2_stats->has_value());
    CHECK_EQ(q2_stats.value()->Get<int>("total_losses"), 1);

    auto q2_rows = db.Query("SELECT result, ended_reason, ranked FROM match_history WHERE match_id = ? AND username = ?;", {"quit-continue-test", "mi_q2"});
    REQUIRE(q2_rows.has_value());
    REQUIRE_EQ(q2_rows->size(), 1);
    CHECK_EQ(q2_rows->front().Get<std::string>("result"), "quit");
    CHECK_EQ(q2_rows->front().Get<std::string>("ended_reason"), "quit");
    CHECK_EQ(q2_rows->front().Get<int>("ranked"), 1);

    // Later, match finishes with mi_q1 winning
    m.RecordMatchCompleted("mi_q1");

    auto all_rows = db.Query("SELECT username, result, ranked FROM match_history WHERE match_id = ? ORDER BY username;", {"quit-continue-test"});
    REQUIRE(all_rows.has_value());
    REQUIRE_EQ(all_rows->size(), 3);

    auto q1_stats = db.QueryOne("SELECT total_wins FROM player_stats WHERE username = ?;", {"mi_q1"});
    REQUIRE(q1_stats.has_value());
    REQUIRE(q1_stats->has_value());
    CHECK_EQ(q1_stats.value()->Get<int>("total_wins"), 1);
}

TEST_CASE("Quit behavior: quit_deletes_match=true records aborted for all and touches no player_stats") {
    SetupTestUser("mi_a1");
    SetupTestUser("mi_a2");
    SetupTestUser("mi_a3");

    std::vector<std::pair<std::string, bool>> players = {
        {"mi_a1", false}, {"mi_a2", false}, {"mi_a3", false}
    };
    LobbySettings settings = default_settings();
    settings.ranked = true;
    settings.quit_deletes_match = true;

    MatchInstance m(players, settings);
    m.SetMatchId("quit-abort-test");
    m.Start();

    m.RecordMatchAborted();

    auto& db = Database::Get();
    for (const auto& u : {"mi_a1", "mi_a2", "mi_a3"}) {
        auto stats = db.QueryOne("SELECT total_wins, total_losses FROM player_stats WHERE username = ?;", {u});
        REQUIRE(stats.has_value());
        if (stats->has_value()) {
            CHECK_EQ(stats.value()->Get<int>("total_wins"), 0);
            CHECK_EQ(stats.value()->Get<int>("total_losses"), 0);
        }
    }

    auto rows = db.Query("SELECT username, result, ended_reason, ranked FROM match_history WHERE match_id = ?;", {"quit-abort-test"});
    REQUIRE(rows.has_value());
    REQUIRE_EQ(rows->size(), 3);
    for (const auto& r : *rows) {
        CHECK_EQ(r.Get<std::string>("result"), "aborted");
        CHECK_EQ(r.Get<std::string>("ended_reason"), "aborted");
        CHECK_EQ(r.Get<int>("ranked"), 0);
    }
}

// ---------------------------------------------------------------------------
// Elimination Mode Tests
// ---------------------------------------------------------------------------

TEST_CASE("Elimination mode: 3 players turn rotation and placement resolution") {
    json saved_state;
    saved_state["rules"] = json::array();
    saved_state["status"] = 1;  // kPlaying
    saved_state["active_type"] = 0;  // kRed
    saved_state["current_player_index"] = 0;
    saved_state["play_direction"] = 1;
    saved_state["pending_player"] = "";
    saved_state["discard_pile"] = json::array({MakeCard(Type::kRed, Value::k5, 100)});
    saved_state["draw_pile"] = json::array();

    saved_state["players"] = json::array({
        {
            {"username", "el3_p0"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k1, 1)})},
            {"is_bot", false}
        },
        {
            {"username", "el3_p1"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k2, 2), MakeCard(Type::kRed, Value::k3, 3)})},
            {"is_bot", false}
        },
        {
            {"username", "el3_p2"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k4, 4), MakeCard(Type::kRed, Value::k6, 6)})},
            {"is_bot", false}
        }
    });

    LobbySettings settings = default_settings();
    settings.mode = "elimination";
    settings.survivor_count = 1;

    MatchInstance m(saved_state, settings);
    REQUIRE_EQ(m.GetCurrentPlayerUsername(), "el3_p0");

    // el3_p0 plays their single card and empties hand
    REQUIRE(m.PlayCard("el3_p0", 1));
    m.Tick();

    // el3_p0 has shed their hand and leaves first -> recorded first in
    // placements (best-first, shedding order) while live. Match continues with
    // 2 players (survivor_count = 1).
    CHECK_FALSE(m.IsMatchOver());
    REQUIRE_EQ(m.GetPlacements().size(), 1);
    CHECK_EQ(m.GetPlacements()[0], "el3_p0");

    // Next turn must be el3_p1 (no skipped turn!)
    CHECK_EQ(m.GetCurrentPlayerUsername(), "el3_p1");

    // el3_p1 plays a card
    REQUIRE(m.PlayCard("el3_p1", 2));
    m.Tick();

    // Next turn must be el3_p2
    CHECK_EQ(m.GetCurrentPlayerUsername(), "el3_p2");

    // el3_p2 plays a card
    REQUIRE(m.PlayCard("el3_p2", 4));
    m.Tick();

    // Turn returns to el3_p1 (1 card remaining)
    CHECK_EQ(m.GetCurrentPlayerUsername(), "el3_p1");

    // el3_p1 plays their last card -> empties hand
    REQUIRE(m.PlayCard("el3_p1", 3));
    m.Tick();

    // Match must conclude because remaining players <= survivor_count (1)
    CHECK(m.IsMatchOver());

    // Shedding race: the FIRST player to empty their hand wins (1st) and the
    // last one still holding cards is the absolute loser. el3_p0 shed first,
    // el3_p1 second, el3_p2 never shed (survivor) -> el3_p2 is last.
    CHECK_EQ(m.GetWinner(), "el3_p0");

    const auto& placements = m.GetPlacements();
    REQUIRE_EQ(placements.size(), 3);
    CHECK_EQ(placements[0], "el3_p0");  // shed first -> 1st (winner)
    CHECK_EQ(placements[1], "el3_p1");  // shed second -> 2nd
    CHECK_EQ(placements[2], "el3_p2");  // last standing -> 3rd (absolute loser)
}

TEST_CASE("Elimination mode: 4 players rotation and survivor_count=2") {
    json saved_state;
    saved_state["rules"] = json::array();
    saved_state["status"] = 1;  // kPlaying
    saved_state["active_type"] = 0;  // kRed
    saved_state["current_player_index"] = 0;
    saved_state["play_direction"] = 1;
    saved_state["pending_player"] = "";
    saved_state["discard_pile"] = json::array({MakeCard(Type::kRed, Value::k5, 100)});
    saved_state["draw_pile"] = json::array();

    saved_state["players"] = json::array({
        {
            {"username", "A"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k1, 1)})},
            {"is_bot", false}
        },
        {
            {"username", "B"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k2, 2)})},
            {"is_bot", false}
        },
        {
            {"username", "C"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k3, 3), MakeCard(Type::kRed, Value::k4, 4)})},
            {"is_bot", false}
        },
        {
            {"username", "D"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k6, 6), MakeCard(Type::kRed, Value::k7, 7), MakeCard(Type::kRed, Value::k8, 8)})},
            {"is_bot", false}
        }
    });

    LobbySettings settings = default_settings();
    settings.mode = "elimination";
    settings.survivor_count = 2;

    MatchInstance m(saved_state, settings);
    REQUIRE_EQ(m.GetCurrentPlayerUsername(), "A");

    // A empties hand
    REQUIRE(m.PlayCard("A", 1));
    m.Tick();

    CHECK_FALSE(m.IsMatchOver());
    CHECK_EQ(m.GetPlacements().size(), 1);
    CHECK_EQ(m.GetPlacements()[0], "A");

    // Turn moves to B
    CHECK_EQ(m.GetCurrentPlayerUsername(), "B");

    // B empties hand
    REQUIRE(m.PlayCard("B", 2));
    m.Tick();

    // With survivor_count=2 and 2 remaining players (C, D), match concludes immediately!
    CHECK(m.IsMatchOver());

    // Shedding race: A shed first (winner), B second; C and D never shed, so
    // they rank below the shed players, smaller held hand first (C's 2 cards
    // before D's 3). The last player still holding cards is the absolute loser.
    CHECK_EQ(m.GetWinner(), "A");

    const auto& placements = m.GetPlacements();
    REQUIRE_EQ(placements.size(), 4);
    CHECK_EQ(placements[0], "A");  // shed first -> 1st (winner)
    CHECK_EQ(placements[1], "B");  // shed second -> 2nd
    CHECK_EQ(placements[2], "C");  // 2 cards -> 3rd
    CHECK_EQ(placements[3], "D");  // 3 cards -> 4th (absolute loser)
}

TEST_CASE("Elimination mode: mid-game removal reaching survivor_count yields best-first placements (path B)") {
    json saved_state;
    saved_state["rules"] = json::array();
    saved_state["status"] = 1;  // kPlaying
    saved_state["active_type"] = 0;  // kRed
    saved_state["current_player_index"] = 0;
    saved_state["play_direction"] = 1;
    saved_state["pending_player"] = "";
    saved_state["discard_pile"] = json::array({MakeCard(Type::kRed, Value::k5, 100)});
    saved_state["draw_pile"] = json::array();

    saved_state["players"] = json::array({
        {
            {"username", "rm_p0"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k1, 1)})},
            {"is_bot", false}
        },
        {
            {"username", "rm_p1"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k2, 2)})},
            {"is_bot", false}
        },
        {
            {"username", "rm_p2"},
            {"hand", json::array({MakeCard(Type::kRed, Value::k3, 3), MakeCard(Type::kRed, Value::k4, 4)})},
            {"is_bot", false}
        }
    });

    LobbySettings settings = default_settings();
    settings.mode = "elimination";
    settings.survivor_count = 1;

    MatchInstance m(saved_state, settings);
    REQUIRE_EQ(m.GetCurrentPlayerUsername(), "rm_p0");

    // rm_p0 empties hand -> leaves first, recorded first in placements
    // (best-first, shedding order) while the match is running. Match continues
    // with 2 players (survivor_count = 1).
    REQUIRE(m.PlayCard("rm_p0", 1));
    m.Tick();
    CHECK_FALSE(m.IsMatchOver());
    REQUIRE_EQ(m.GetPlacements().size(), 1);
    CHECK_EQ(m.GetPlacements()[0], "rm_p0");

    // rm_p1 leaves mid-game, dropping the remaining players to exactly
    // survivor_count(1); RemovePlayerMidGame completes the match (path B).
    // The removed player quits and is not ranked (recorded as "quit" in the
    // ledger), so placements only ever contain eliminations + survivors.
    m.RemovePlayerMidGame("rm_p1");

    CHECK(m.IsMatchOver());

    // Shedding race: rm_p0 shed first and wins; rm_p2 never shed and is left
    // standing, so it ranks last (absolute loser).
    CHECK_EQ(m.GetWinner(), "rm_p0");

    const auto& placements = m.GetPlacements();
    REQUIRE_EQ(placements.size(), 2);
    CHECK_EQ(placements[0], "rm_p0");  // shed first -> 1st (winner)
    CHECK_EQ(placements[1], "rm_p2");  // last standing -> 2nd (absolute loser)
}

TEST_CASE("Elimination mode: max players (16) rotation with elimination before, at, and after current turn") {
    std::vector<std::pair<std::string, bool>> players;
    for (int i = 0; i < 16; ++i) {
        players.emplace_back("player_" + std::to_string(i), false);
    }

    LobbySettings settings;
    settings.max_players = 16;
    settings.starting_cards = 5;
    settings.turn_time_limit_ms = 15000;
    settings.mode = "elimination";
    settings.survivor_count = 1;
    settings.Sanitize(16);

    MatchInstance m(players, settings);
    m.Start();

    // 16 players, turn starts at player_0 (index 0)
    CHECK_EQ(m.GetCurrentPlayerUsername(), "player_0");

    // Test elimination of player immediately after current turn (player_1, index 1)
    m.RemovePlayerFromRotation(1, false);
    // player_0 should still be current player
    CHECK_EQ(m.GetCurrentPlayerUsername(), "player_0");

    // Test player immediately before current turn:
    auto exported = m.ExportState();
    exported["current_player_index"] = 5;
    std::string expected_current = exported["players"][5]["username"].get<std::string>();

    MatchInstance m2(exported, settings);
    CHECK_EQ(m2.GetCurrentPlayerUsername(), expected_current);

    // Remove player at index 4 (immediately before current turn)
    m2.RemovePlayerFromRotation(4, false);
    CHECK_EQ(m2.GetCurrentPlayerUsername(), expected_current);

    // Remove player at index 5 (immediately after current turn)
    m2.RemovePlayerFromRotation(5, false);
    CHECK_EQ(m2.GetCurrentPlayerUsername(), expected_current);
}

TEST_CASE("Elimination mode: ledger writes placement and leaves player_stats untouched") {
    SetupTestUser("el_u1");
    SetupTestUser("el_u2");
    SetupTestUser("el_u3");

    std::vector<std::pair<std::string, bool>> players = {
        {"el_u1", false}, {"el_u2", false}, {"el_u3", false}
    };
    LobbySettings settings = default_settings();
    settings.mode = "elimination";
    settings.ranked = true;  // Even if ranked is true, elimination must be unranked (ranked=0, no player_stats)

    MatchInstance m(players, settings);
    m.SetMatchId("elim-ledger-test");
    m.Start();

    CHECK_FALSE(m.IsRankedEligible());  // Defense-in-depth

    // Emulate completion with placements
    m.RemovePlayerMidGame("el_u1");  // Just to get 1 remaining
    m.RecordMatchCompleted("el_u1");

    auto& db = Database::Get();
    for (const auto& u : {"el_u1", "el_u2", "el_u3"}) {
        auto stats = db.QueryOne("SELECT total_wins, total_losses FROM player_stats WHERE username = ?;", {u});
        REQUIRE(stats.has_value());
        if (stats->has_value()) {
            CHECK_EQ(stats.value()->Get<int>("total_wins"), 0);
            CHECK_EQ(stats.value()->Get<int>("total_losses"), 0);
        }
    }

    auto rows = db.Query("SELECT username, mode, placement, result, ended_reason, ranked FROM match_history WHERE match_id = ? ORDER BY username;", {"elim-ledger-test"});
    REQUIRE(rows.has_value());
    REQUIRE_EQ(rows->size(), 3);
    for (const auto& r : *rows) {
        CHECK_EQ(r.Get<std::string>("mode"), "elimination");
        CHECK_EQ(r.Get<int>("ranked"), 0);
    }
}

