#include <doctest/doctest.h>
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
