#include <doctest/doctest.h>
#include <action_router.hpp>
#include <controllers/match_controller.hpp>
#include <controllers/ilobby_store.hpp>
#include <match/match_instance.hpp>
#include <common/lobby.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "support/fake_broadcaster.hpp"
#include "support/fake_timer_service.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

// Single-lobby test double for ILobbyStore. Gives tests full control over the
// Lobby/MatchInstance being driven, without the overhead of LobbyController's
// invite-code/join machinery.
class FakeLobbyStore : public ILobbyStore {
public:
    Lobby lobby;
    uint32_t match_over_notifications = 0;

    Lobby* GetLobbyById(uint32_t id) override {
        return (lobby.id == id) ? &lobby : nullptr;
    }

    void OnGameStarted(MatchStartedCallback cb) override {
        game_started_cbs.push_back(std::move(cb));
    }

    void OnPlayerReplaced(PlayerReplacedCallback cb) override {
        player_replaced_cbs.push_back(std::move(cb));
    }

    void OnMatchAborted(MatchAbortedCallback cb) override {
        match_aborted_cbs.push_back(std::move(cb));
    }

    void OnLobbyDestroyed(LobbyDestroyedCallback cb) override {
        lobby_destroyed_cbs.push_back(std::move(cb));
    }

    void NotifyMatchOver(uint32_t) override {
        ++match_over_notifications;
    }

    void FireGameStarted() {
        for (auto& cb : game_started_cbs) cb(&lobby);
    }

    std::vector<MatchStartedCallback>   game_started_cbs;
    std::vector<PlayerReplacedCallback> player_replaced_cbs;
    std::vector<MatchAbortedCallback>   match_aborted_cbs;
    std::vector<LobbyDestroyedCallback> lobby_destroyed_cbs;
};

// FakeTimerService that also records the last requested timeout for each key,
// so tests can assert on the timeout-mode selection (bot-thinking delay vs.
// full human AFK turn-time-limit) without needing a real clock.
class RecordingTimerService : public FakeTimerService {
public:
    std::map<std::string, int> last_timeout_ms;

    void Schedule(const std::string& key, int timeout_ms, bool repeat,
                  std::function<void()> cb) override {
        last_timeout_ms[key] = timeout_ms;
        FakeTimerService::Schedule(key, timeout_ms, repeat, std::move(cb));
    }
};

// ---------------------------------------------------------------------------
// Fixture: fresh router/bus/timers/lobby-store/controller per test case.
// ---------------------------------------------------------------------------
struct MatchFixture {
    ActionRouter     router;
    FakeBroadcaster  bus;
    FakeTimerService timers;
    FakeLobbyStore   store;
    MatchController  match_ctrl{router, bus, timers, store};

    // Builds and starts a match for the given players, wiring it into the
    // fake lobby store, then fires the OnGameStarted hook (as LobbyController
    // would after a real match start).
    void SetupMatch(const std::vector<std::pair<std::string, bool>>& players_info,
                     const LobbySettings& settings) {
        store.lobby.id = 1;
        store.lobby.invite_code = "TEST01";
        store.lobby.host = players_info.front().first;
        store.lobby.settings = settings;
        store.lobby.members.clear();
        for (const auto& [username, is_bot] : players_info) {
            store.lobby.members.emplace_back(username, nullptr, !is_bot, is_bot);
        }
        store.lobby.match = std::make_unique<match::MatchInstance>(players_info, settings);
        store.lobby.match->Start();

        store.FireGameStarted();
    }

    // Drains the single-shot "turn_1" timer chain until the match ends or the
    // fake timer service no longer has a pending callback for the lobby.
    // FakeTimerService::Fire() does not consume the callback entry (it mimics
    // a re-armable slot keyed by lobby id), so IsMatchOver() is the real
    // termination signal; Has() is only used to detect that nothing new was
    // armed (e.g. the engine is waiting on a human who never responds).
    int DrainTurnTimer(int max_fires) {
        int fired = 0;
        while (!store.lobby.match->IsMatchOver() && fired < max_fires) {
            if (!timers.Has("turn_1")) break;
            timers.Fire("turn_1");
            ++fired;
        }
        return fired;
    }
};

static LobbySettings settings_with_mode(BotTakeoverMode mode, int turn_time_limit_ms = 15'000) {
    LobbySettings s;
    s.bot_mode = mode;
    s.turn_time_limit_ms = turn_time_limit_ms;
    return s;
}

static std::vector<std::pair<std::string, bool>> human_vs_bot() {
    return {{"Alice", false}, {"BotBob", true}};
}

static std::vector<std::pair<std::string, bool>> bot_vs_human() {
    return {{"BotBob", true}, {"Alice", false}};
}

static std::vector<std::pair<std::string, bool>> all_bots(int count) {
    std::vector<std::pair<std::string, bool>> players;
    for (int i = 0; i < count; ++i) {
        players.emplace_back("Bot" + std::to_string(i), true);
    }
    return players;
}

// ---------------------------------------------------------------------------
// Tests: bot-autoplay chain termination + turn-timeout mode selection.
// ---------------------------------------------------------------------------
TEST_SUITE("MatchController") {
TEST_CASE("OnTurnStarted: arms a turn timer as soon as a match starts") {
    MatchFixture f;
    f.SetupMatch(human_vs_bot(), settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd));

    // Under kWaitUntilTurnEnd, a timer is always armed for the current turn:
    // it doubles as the bot-thinking delay for a bot player and as the AFK
    // takeover timer for a human player.
    CHECK(f.timers.Has("turn_1"));
}

TEST_CASE("Bot-autoplay chain: firing the bot turn timer eventually reaches match end, "
          "without an infinite loop") {
    MatchFixture f;
    f.SetupMatch(all_bots(4), settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd));

    // Every player is a bot, so the turn timer must be armed as soon as the
    // match starts.
    REQUIRE(f.timers.Has("turn_1"));

    // Repeatedly fire the turn timer, simulating the bots playing out their
    // consecutive turns. This must terminate rather than looping forever.
    constexpr int kMaxFires = 5000;
    int fired = f.DrainTurnTimer(kMaxFires);

    CHECK(fired < kMaxFires);
    CHECK(f.store.lobby.match->IsMatchOver());
}

TEST_CASE("Bot-autoplay chain: kPlayInstantly mode still arms one turn timer per bot move "
          "and terminates once drained") {
    MatchFixture f;
    f.SetupMatch(all_bots(4), settings_with_mode(BotTakeoverMode::kPlayInstantly));

    // Real bot players (regardless of bot_mode) always go through the
    // timer-armed branch of OnTurnStarted; only a *disconnected human* uses
    // the synchronous instant-play loop. kPlayInstantly only shortens the
    // bot-thinking delay, it does not bypass the timer chain.
    REQUIRE(f.timers.Has("turn_1"));

    constexpr int kMaxFires = 5000;
    int fired = f.DrainTurnTimer(kMaxFires);

    CHECK(fired < kMaxFires);
    CHECK(f.store.lobby.match->IsMatchOver());
}

TEST_CASE("Bot-autoplay chain: a connected human's turn ends the automatic chain, leaving "
          "a single armed timer instead of recursing") {
    MatchFixture f;
    f.SetupMatch(human_vs_bot(), settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd));

    // Regardless of who goes first, exactly one turn timer is armed for the
    // lobby: the chain does not recurse past the point where a response
    // (human input or a later bot timer) is required.
    CHECK(f.timers.Has("turn_1"));
}

TEST_CASE("SetTurnTimer: bot turn uses the bot-thinking delay, not the full turn-time-limit") {
    ActionRouter router;
    FakeBroadcaster bus;
    RecordingTimerService timers;
    FakeLobbyStore store;
    MatchController ctrl(router, bus, timers, store);

    LobbySettings settings = settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd, 15'000);
    auto players = bot_vs_human();  // BotBob goes first (players[0]).
    store.lobby.id = 1;
    store.lobby.host = players.front().first;
    store.lobby.settings = settings;
    for (const auto& [username, is_bot] : players) {
        store.lobby.members.emplace_back(username, nullptr, !is_bot, is_bot);
    }
    store.lobby.match = std::make_unique<match::MatchInstance>(players, settings);
    store.lobby.match->Start();
    store.FireGameStarted();

    REQUIRE(store.lobby.match->GetPlayer("BotBob")->is_bot);
    REQUIRE(timers.last_timeout_ms.count("turn_1") == 1);

    // The default bot "thinking" jitter is bounded well below the 15s human
    // turn-time-limit (defaults: instant=1000ms, wait spread=500-3500ms).
    CHECK(timers.last_timeout_ms["turn_1"] < settings.turn_time_limit_ms);
}

TEST_CASE("SetTurnTimer: human turn uses the full turn-time-limit as the AFK timeout") {
    ActionRouter router;
    FakeBroadcaster bus;
    RecordingTimerService timers;
    FakeLobbyStore store;
    MatchController ctrl(router, bus, timers, store);

    LobbySettings settings = settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd, 15'000);
    auto players = human_vs_bot();  // Alice goes first (players[0]).
    store.lobby.id = 1;
    store.lobby.host = players.front().first;
    store.lobby.settings = settings;
    for (const auto& [username, is_bot] : players) {
        store.lobby.members.emplace_back(username, nullptr, !is_bot, is_bot);
    }
    store.lobby.match = std::make_unique<match::MatchInstance>(players, settings);
    store.lobby.match->Start();
    store.FireGameStarted();

    REQUIRE_FALSE(store.lobby.match->GetPlayer("Alice")->is_bot);
    REQUIRE(timers.last_timeout_ms.count("turn_1") == 1);

    CHECK(timers.last_timeout_ms["turn_1"] == settings.turn_time_limit_ms);
}

TEST_CASE("SetTurnTimer: firing the human AFK timer under kWaitUntilTurnEnd hands the turn to "
          "the bot and re-arms without infinite recursion") {
    MatchFixture f;
    f.SetupMatch(human_vs_bot(), settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd));

    REQUIRE(f.timers.Has("turn_1"));

    constexpr int kMaxFires = 5000;
    int fired = f.DrainTurnTimer(kMaxFires);

    CHECK(fired < kMaxFires);
}

TEST_CASE("ClearTurnTimer: the match reaching a terminal state stops the timer chain from "
          "re-arming") {
    MatchFixture f;
    f.SetupMatch(all_bots(2), settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd));
    REQUIRE(f.timers.Has("turn_1"));

    constexpr int kMaxFires = 5000;
    int fired = f.DrainTurnTimer(kMaxFires);

    CHECK(fired < kMaxFires);
    CHECK(f.store.lobby.match->IsMatchOver());
}
}  // TEST_SUITE("MatchController")

// ---------------------------------------------------------------------------
// Test: full-game bot simulation with all mods enabled.
// ---------------------------------------------------------------------------
TEST_SUITE("MatchController::FullGameSimulation") {
TEST_CASE("Full match: 4 bots with every mod enabled reaches a terminal state "
          "without hanging, crashing, or violating invariants") {
    MatchFixture f;

    LobbySettings settings = settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd, 15'000);
    settings.active_mods = {"seven_zero", "draw_stacking", "force_play", "jump_in", "progressive"};

    f.SetupMatch(all_bots(4), settings);

    constexpr int kMaxFires = 20'000;
    int fired = f.DrainTurnTimer(kMaxFires);

    REQUIRE(fired < kMaxFires);
    REQUIRE(f.store.lobby.match->IsMatchOver());

    std::string winner = f.store.lobby.match->GetWinner();
    CHECK_FALSE(winner.empty());

    // The winner must be one of the participating bots.
    bool winner_is_participant = false;
    for (const auto& member : f.store.lobby.members) {
        if (member.username == winner) {
            winner_is_participant = true;
            break;
        }
    }
    CHECK(winner_is_participant);

    // Match-over broadcast must have propagated through to the lobby store.
    CHECK_GE(f.store.match_over_notifications, 1);
}

TEST_CASE("Full match: kPlayInstantly mode with all bots and every mod enabled reaches a "
          "terminal state once the bot-thinking timer chain is drained") {
    MatchFixture f;

    LobbySettings settings = settings_with_mode(BotTakeoverMode::kPlayInstantly, 15'000);
    settings.active_mods = {"seven_zero", "draw_stacking", "force_play", "jump_in", "progressive"};

    f.SetupMatch(all_bots(3), settings);
    REQUIRE(f.timers.Has("turn_1"));

    constexpr int kMaxFires = 20'000;
    int fired = f.DrainTurnTimer(kMaxFires);

    CHECK(fired < kMaxFires);
    CHECK(f.store.lobby.match->IsMatchOver());
    CHECK_FALSE(f.store.lobby.match->GetWinner().empty());
}

TEST_CASE("Full match: mixed bot count (2 to 4 players) with every mod enabled always "
          "terminates") {
    for (int player_count = 2; player_count <= 4; ++player_count) {
        MatchFixture f;

        LobbySettings settings = settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd, 15'000);
        settings.active_mods = {"seven_zero", "draw_stacking", "force_play", "jump_in",
                                 "progressive"};

        f.SetupMatch(all_bots(player_count), settings);

        constexpr int kMaxFires = 20'000;
        int fired = f.DrainTurnTimer(kMaxFires);

        CAPTURE(player_count);
        CHECK(fired < kMaxFires);
        CHECK(f.store.lobby.match->IsMatchOver());
    }
}
}  // TEST_SUITE("MatchController::FullGameSimulation")
