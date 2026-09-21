#include <doctest/doctest.h>
#include <action_router.hpp>
#include <controllers/match_controller.hpp>
#include <controllers/ilobby_store.hpp>
#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/match_session.hpp>
#include <common/lobby.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "support/fake_broadcaster.hpp"
#include "support/fake_timer_service.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

// Single-lobby test double for ILobbyStore. Gives tests full control over the
// Lobby/MatchSession being driven, without the overhead of LobbyController's
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
// New-engine content: the mods folder + the vanilla classic deck, loaded once.
// ---------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

// INFO: locate the project root from this file so the test is cwd-independent
//       (mirrors bot_policy_test.cpp).
fs::path ProjectRoot() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (fs::is_directory(p / "contract" / "schemas", ec)) return p;
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

struct Content {
    std::vector<match::modload::LoadedMod> mods;
    match::modload::DeckDef classic;
};

bool LoadContent(Content& out) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    match::modload::LoadResult load =
        match::modload::ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const match::modload::LoadedMod& mod : out.mods) {
        for (const match::modload::DeckDef& deck : mod.decks) {
            if (deck.deck_id == "vanilla:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

const Content& ContentCache() {
    static Content content;
    static const bool loaded = LoadContent(content);
    REQUIRE_MESSAGE(loaded, "failed to load mods/classic deck");
    return content;
}

}  // namespace

// ---------------------------------------------------------------------------
// Fixture: fresh router/bus/timers/lobby-store/controller per test case.
// ---------------------------------------------------------------------------
struct MatchFixture {
    ActionRouter     router;
    FakeBroadcaster  bus;
    RecordingTimerService timers;
    FakeLobbyStore   store;
    MatchController  match_ctrl{router, bus, timers, store};

    // INFO: an opaque per-seat socket key; the FakeBroadcaster stores but
    //       never dereferences it, so EmitEvents/BroadcastSnapshot reach it.
    static AppWebSocket* SeatSocket(int index) {
        return reinterpret_cast<AppWebSocket*>(
            static_cast<std::uintptr_t>(0x200 + index));
    }

    // Builds and starts a new-engine match for the given players, wiring it
    // into the fake lobby store, then fires the OnGameStarted hook (as
    // LobbyController would after a real match start).
    void SetupMatch(const std::vector<std::pair<std::string, bool>>& players_info,
                     const LobbySettings& settings) {
        store.lobby.id = 1;
        store.lobby.invite_code = "TEST01";
        store.lobby.host = players_info.front().first;
        store.lobby.settings = settings;
        store.lobby.members.clear();
        store.lobby.session.reset();

        int seat = 0;
        for (const auto& [username, is_bot] : players_info) {
            store.lobby.members.emplace_back(username, SeatSocket(seat),
                                             !is_bot, is_bot, seat);
            ++seat;
        }

        const Content& content = ContentCache();
        match::modload::DeckDef deck = content.classic;
        for (const std::string& mod : settings.active_mods) {
            if (std::find(deck.mods.begin(), deck.mods.end(), mod)
                == deck.mods.end()) {
                deck.mods.push_back(mod);
            }
        }

        std::vector<match::modload::LoadedMod> active_mods;
        for (const auto& mod : content.mods) {
            if (std::find(deck.mods.begin(), deck.mods.end(), mod.manifest.id)
                != deck.mods.end()) {
                active_mods.push_back(mod);
            }
        }

        match::engine::MatchAssemblyOptions options;
        options.starting_cards = settings.starting_cards;
        options.seed = 12345;
        for (const auto& member : store.lobby.members) {
            options.players.push_back({member.username, member.is_bot,
                                       member.is_connected, member.is_ready});
        }

        match::engine::AssemblyResult result =
            match::engine::MatchAssembler::Assemble(active_mods, deck, options);
        std::string assembly_error =
            result.error.has_value() ? result.error->message
                                     : std::string("assembly failed");
        REQUIRE_MESSAGE(result.ok(), assembly_error);
        auto engine = std::make_unique<match::engine::MatchInstance>(
            std::move(result.assembly));

        match::server::MatchSession::SocketMap sockets;
        for (const auto& member : store.lobby.members) {
            sockets[member.username] = member.socket;
        }
        store.lobby.session = std::make_unique<match::server::MatchSession>(
            std::move(engine), std::move(active_mods), std::move(sockets));

        store.FireGameStarted();
    }

    match::engine::MatchInstance& Engine() {
        return store.lobby.session->Engine();
    }

    // Drains the single-shot "turn_1" timer chain until the match ends or the
    // fake timer service no longer has a pending callback for the lobby.
    // FakeTimerService::Fire() does not consume the callback entry (it mimics
    // a re-armable slot keyed by lobby id), so IsMatchOver() is the real
    // termination signal; Has() is only used to detect that nothing new was
    // armed (e.g. the engine is waiting on a human who never responds).
    int DrainTurnTimer(int max_fires) {
        int fired = 0;
        while (!Engine().IsMatchOver() && fired < max_fires) {
            if (!timers.Has("turn_1")) break;
            timers.Fire("turn_1");
            ++fired;
        }
        return fired;
    }
};

// INFO: force a seat's hand to exactly `cards` (test setup for a scripted
//       human win), mirroring match_session_test.cpp.
static void ForceHand(match::engine::MatchInstance& engine,
                      match::ecs::Entity player,
                      const std::vector<match::ecs::Entity>& cards) {
    match::ecs::Hand* hand = engine.Store().Get<match::ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<match::ecs::Entity> existing = hand->cards;
    for (match::ecs::Entity card : existing) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            match::ecs::ZoneRef{match::ecs::ZoneKind::kDrawPile,
                                match::ecs::Entity{}});
    }
    for (match::ecs::Entity card : cards) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            match::ecs::ZoneRef{match::ecs::ZoneKind::kHand, player});
    }
}

static std::vector<match::ecs::Entity> CardsByKind(
    const match::engine::MatchInstance& engine, const std::string& kind) {
    std::vector<match::ecs::Entity> out;
    for (match::ecs::Entity card : engine.Registries().cards) {
        const match::ecs::CardIdentity* identity =
            engine.Store().Get<match::ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind) {
            out.push_back(card);
        }
    }
    return out;
}

static uint32_t BitsOf(const match::engine::MatchInstance& engine,
                       match::ecs::Entity card) {
    const std::optional<match::ecs::CompactCardV2> id =
        engine.Registries().CardId(card);
    REQUIRE(id.has_value());
    return id->bits;
}

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
    CHECK(f.Engine().IsMatchOver());
}

TEST_CASE("Bot-autoplay chain: kPlayInstantly mode still arms one turn timer per bot move "
          "and terminates once drained") {
    MatchFixture f;
    f.SetupMatch(all_bots(4), settings_with_mode(BotTakeoverMode::kPlayInstantly));

    // Real bot players (regardless of bot_mode) always go through the
    // timer-armed branch of OnTurnStarted; kPlayInstantly only shortens the
    // bot-thinking delay, it does not bypass the timer chain.
    REQUIRE(f.timers.Has("turn_1"));

    constexpr int kMaxFires = 5000;
    int fired = f.DrainTurnTimer(kMaxFires);

    CHECK(fired < kMaxFires);
    CHECK(f.Engine().IsMatchOver());
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
    int seat = 0;
    for (const auto& [username, is_bot] : players) {
        store.lobby.members.emplace_back(username, nullptr, !is_bot,
                                         is_bot, seat++);
    }
    const Content& content = ContentCache();
    match::engine::MatchAssemblyOptions options;
    options.starting_cards = settings.starting_cards;
    options.seed = 12345;
    for (const auto& member : store.lobby.members) {
        options.players.push_back({member.username, member.is_bot,
                                   member.is_connected, member.is_ready});
    }
    match::engine::AssemblyResult result =
        match::engine::MatchAssembler::Assemble(content.mods, content.classic,
                                                options);
    REQUIRE_MESSAGE(result.ok(), "assembly failed");
    auto engine = std::make_unique<match::engine::MatchInstance>(
        std::move(result.assembly));
    match::server::MatchSession::SocketMap sockets;
    for (const auto& member : store.lobby.members) {
        sockets[member.username] = nullptr;
    }
    store.lobby.session = std::make_unique<match::server::MatchSession>(
        std::move(engine), content.mods, std::move(sockets));
    store.FireGameStarted();

    REQUIRE(store.lobby.session->Engine().GetCurrentPlayerUsername()
            == "BotBob");
    REQUIRE(store.lobby.FindMember("BotBob")->is_bot);
    REQUIRE(timers.last_timeout_ms.count("turn_1") == 1);

    // The default bot "thinking" jitter is bounded well below the 15s human
    // turn-time-limit (defaults: instant=1000ms, wait spread=500-3500ms).
    CHECK(timers.last_timeout_ms["turn_1"] < settings.turn_time_limit_ms);
}

TEST_CASE("SetTurnTimer: human turn uses the full turn-time-limit as the AFK timeout") {
    MatchFixture f;
    LobbySettings settings = settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd, 15'000);
    f.SetupMatch(human_vs_bot(), settings);  // Alice goes first (players[0]).

    REQUIRE(f.Engine().GetCurrentPlayerUsername() == "Alice");
    REQUIRE_FALSE(f.store.lobby.FindMember("Alice")->is_bot);
    REQUIRE(f.timers.last_timeout_ms.count("turn_1") == 1);

    CHECK(f.timers.last_timeout_ms["turn_1"] == settings.turn_time_limit_ms);
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
    CHECK(f.Engine().IsMatchOver());
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
    REQUIRE(f.Engine().IsMatchOver());

    std::string winner = f.Engine().GetWinner();
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
    CHECK(f.Engine().IsMatchOver());
    CHECK_FALSE(f.Engine().GetWinner().empty());
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
        CHECK(f.Engine().IsMatchOver());
    }
}
}  // TEST_SUITE("MatchController::FullGameSimulation")

TEST_SUITE("MatchController::Spectator") {
TEST_CASE("Spectator cannot play card, draw card, or provide input") {
    MatchFixture f;
    LobbySettings settings;
    f.SetupMatch({{"Alice", false}, {"Bob", false}}, settings);

    f.store.lobby.members.emplace_back("Charlie", nullptr, true, false, -1, /*is_spectator=*/true);

    PerSocketData sd;
    sd.username = "Charlie";
    sd.lobby_id = 1;
    auto* sock = reinterpret_cast<AppWebSocket*>(0x1234);
    WsContext ctx{sock, &sd};

    // Play card rejected
    f.router.Dispatch(ctx, json{
        {"action", ws::ClientAction::kMatchPlayCard},
        {"card_id", 1}
    });
    REQUIRE_FALSE(f.bus.sent.empty());
    auto err1 = json::parse(f.bus.sent.back().payload);
    CHECK_EQ(err1["action"], "error");
    CHECK_EQ(err1["code"], "spectator_cannot_act");

    // Draw card rejected
    f.bus.Clear();
    f.router.Dispatch(ctx, json{
        {"action", ws::ClientAction::kMatchDrawCard}
    });
    REQUIRE_FALSE(f.bus.sent.empty());
    auto err2 = json::parse(f.bus.sent.back().payload);
    CHECK_EQ(err2["action"], "error");
    CHECK_EQ(err2["code"], "spectator_cannot_act");
}
}

// ---------------------------------------------------------------------------
// a human action that ends the match must notify the lobby store
// (teardown + rematch allowed), and an already-finished engine must be
// notified on turn start instead of silently returning.
// ---------------------------------------------------------------------------
TEST_SUITE("MatchController::MatchOver") {
TEST_CASE("A human play that wins the match notifies the lobby store") {
    MatchFixture f;
    // INFO: Alice is human and seated first, so she holds the opening turn.
    f.SetupMatch(human_vs_bot(), settings_with_mode(
        BotTakeoverMode::kWaitUntilTurnEnd));

    REQUIRE(f.Engine().GetCurrentPlayerUsername() == "Alice");
    const std::vector<match::ecs::Entity> wilds =
        CardsByKind(f.Engine(), "vanilla:wild");
    REQUIRE(wilds.size() >= 2);
    const match::ecs::Entity alice = *f.Engine().FindPlayer("Alice");
    ForceHand(f.Engine(), alice, {wilds[0]});
    const uint32_t wild_bits = BitsOf(f.Engine(), wilds[0]);

    PerSocketData sd;
    sd.username = "Alice";
    sd.lobby_id = 1;
    WsContext ctx{f.store.lobby.FindMember("Alice")->socket, &sd,
                  uWS::OpCode::TEXT};

    REQUIRE(f.store.match_over_notifications == 0);
    f.router.Dispatch(ctx, json{
        {"action", ws::ClientAction::kMatchPlayCard},
        {"card_id", wild_bits}
    });
    // INFO: the wild parks a colour prompt; answering it ends the match.
    f.router.Dispatch(ctx, json{
        {"action", ws::ClientAction::kMatchPromptResponse},
        {"prompt_id", "choose_color"},
        {"value", "red"}
    });

    CHECK(f.Engine().IsMatchOver());
    CHECK(f.Engine().GetWinner() == "Alice");
    // INFO: Without routing the post-input broadcast through
    //       BroadcastMatchState this stays 0 and a rematch is rejected.
    CHECK_GE(f.store.match_over_notifications, 1);
}

TEST_CASE("OnTurnStarted notifies the lobby store when the engine is already over") {
    MatchFixture f;
    f.SetupMatch(human_vs_bot(), settings_with_mode(
        BotTakeoverMode::kWaitUntilTurnEnd));

    const std::vector<match::ecs::Entity> wilds =
        CardsByKind(f.Engine(), "vanilla:wild");
    REQUIRE(wilds.size() >= 2);
    const match::ecs::Entity alice = *f.Engine().FindPlayer("Alice");
    ForceHand(f.Engine(), alice, {wilds[0]});

    REQUIRE(f.store.match_over_notifications == 0);
    const uint32_t before = f.store.match_over_notifications;

    PerSocketData sd;
    sd.username = "Alice";
    sd.lobby_id = 1;
    WsContext ctx{f.store.lobby.FindMember("Alice")->socket, &sd,
                  uWS::OpCode::TEXT};
    f.router.Dispatch(ctx, json{
        {"action", ws::ClientAction::kMatchPlayCard},
        {"card_id", BitsOf(f.Engine(), wilds[0])}
    });
    f.router.Dispatch(ctx, json{
        {"action", ws::ClientAction::kMatchPromptResponse},
        {"prompt_id", "choose_color"},
        {"value", "red"}
    });
    REQUIRE(f.Engine().IsMatchOver());

    // INFO: The winning human action routed through
    //       BroadcastMatchState must notify the lobby store. Reverting to a
    //       raw BroadcastSnapshot leaves this at `before` (0).
    CHECK_GT(f.store.match_over_notifications, before);
}
}

// ---------------------------------------------------------------------------
// bot / AFK timer steps must emit `match_event` frames, not just
// snapshots (an all-bot match previously emitted no events at all).
// ---------------------------------------------------------------------------
TEST_SUITE("MatchController::BotEvents") {
TEST_CASE("Bot timer steps emit match_event frames to the seated sockets") {
    MatchFixture f;
    f.SetupMatch(all_bots(2), settings_with_mode(
        BotTakeoverMode::kWaitUntilTurnEnd));

    REQUIRE(f.timers.Has("turn_1"));
    const std::uintptr_t socket0 =
        reinterpret_cast<std::uintptr_t>(MatchFixture::SeatSocket(0));
    const std::uintptr_t socket1 =
        reinterpret_cast<std::uintptr_t>(MatchFixture::SeatSocket(1));

    constexpr int kMaxFires = 20'000;
    const int fired = f.DrainTurnTimer(kMaxFires);
    REQUIRE(fired < kMaxFires);
    REQUIRE(f.Engine().IsMatchOver());

    std::size_t match_events = 0;
    for (const SentFrame& frame : f.bus.sent) {
        const std::uintptr_t to = reinterpret_cast<std::uintptr_t>(frame.to);
        if (to != socket0 && to != socket1) continue;
        const json packet = json::parse(frame.payload);
        if (packet.value("action", std::string()) == "match_event") {
            ++match_events;
        }
    }
    // INFO: The bot steps must have flushed real events, not
    //       only state snapshots, even without any human input.
    CHECK_GT(match_events, 0u);
}

TEST_CASE("AFK takeover timer emits match_event frames") {
    MatchFixture f;
    f.SetupMatch(human_vs_bot(), settings_with_mode(
        BotTakeoverMode::kWaitUntilTurnEnd));
    REQUIRE(f.Engine().GetCurrentPlayerUsername() == "Alice");

    const std::uintptr_t alice_socket =
        reinterpret_cast<std::uintptr_t>(MatchFixture::SeatSocket(0));

    constexpr int kMaxFires = 20'000;
    const int fired = f.DrainTurnTimer(kMaxFires);
    REQUIRE(fired < kMaxFires);

    std::size_t match_events = 0;
    for (const SentFrame& frame : f.bus.sent) {
        if (reinterpret_cast<std::uintptr_t>(frame.to) != alice_socket) {
            continue;
        }
        const json packet = json::parse(frame.payload);
        if (packet.value("action", std::string()) == "match_event") {
            ++match_events;
        }
    }
    CHECK_GT(match_events, 0u);
}
}

// ---------------------------------------------------------------------------
// The client B2a: the Pass button's wire route. `match_window_response`
// carries `pass` (decline) or `card_id` (respond by play) into the session's
// window methods; the action was previously unregistered (ground truth 3).
// ---------------------------------------------------------------------------
TEST_SUITE("MatchController::WindowResponse") {
// INFO: open a real draw_stacking window on a 3-human match: the current
//       player plays a +2, which leaves the other two seats as responders.
TEST_CASE("match_window_response: pass reaches PassWindow and keeps the window open") {
    MatchFixture f;
    LobbySettings settings;
    settings.active_mods = {"draw_stacking"};
    f.SetupMatch({{"Alice", false}, {"Bob", false}, {"Carol", false}}, settings);

    match::engine::MatchInstance& engine = f.Engine();
    const std::string current = engine.GetCurrentPlayerUsername();
    REQUIRE_FALSE(current.empty());

    std::map<std::string, PerSocketData> data;
    auto context_for = [&](const std::string& username) {
        PerSocketData& sd = data[username];
        sd.username = username;
        sd.lobby_id = f.store.lobby.id;
        return WsContext{f.store.lobby.FindMember(username)->socket, &sd,
                         uWS::OpCode::TEXT};
    };

    // INFO: make the +2 legal and leave the acting seat a spare card so the
    //       play does not win outright.
    engine.Store().Get<match::ecs::ActiveTypeReq>(engine.Registries().match)->type = "red";
    const std::vector<match::ecs::Entity> red2 = CardsByKind(engine, "vanilla:red_draw2");
    REQUIRE(red2.size() >= 2);
    ForceHand(engine, *engine.FindPlayer(current), {red2[0], red2[1]});

    CHECK(f.router.Dispatch(context_for(current), json{
        {"action", ws::ClientAction::kMatchPlayCard},
        {"card_id", BitsOf(engine, red2[0])}
    }));
    REQUIRE(engine.WindowOpen());

    std::string responder;
    for (const json& name : engine.ExportWindow()["responders"]) {
        const std::string candidate = name.get<std::string>();
        if (candidate != current) {
            responder = candidate;
            break;
        }
    }
    REQUIRE_FALSE(responder.empty());

    const bool handled = f.router.Dispatch(
        context_for(responder),
        json{{"action", ws::ClientAction::kMatchWindowResponse}, {"pass", true}});
    CHECK(handled);

    bool responder_passed = false;
    for (const json& response : engine.ExportWindow()["responses"]) {
        if (response.value("player", std::string()) == responder
            && response.value("pass", false)) {
            responder_passed = true;
        }
    }
    // INFO: PassWindow recorded the pass; another responder is still pending.
    CHECK(responder_passed);
    CHECK(engine.WindowOpen());
}

TEST_CASE("match_window_response: card_id reaches RespondWindow") {
    MatchFixture f;
    LobbySettings settings;
    settings.active_mods = {"draw_stacking"};
    f.SetupMatch({{"Alice", false}, {"Bob", false}, {"Carol", false}}, settings);

    match::engine::MatchInstance& engine = f.Engine();
    const std::string current = engine.GetCurrentPlayerUsername();
    REQUIRE_FALSE(current.empty());

    std::vector<std::string> others;
    for (const auto& member : f.store.lobby.members) {
        if (member.username != current) others.push_back(member.username);
    }
    REQUIRE(others.size() == 2);

    std::map<std::string, PerSocketData> data;
    auto context_for = [&](const std::string& username) {
        PerSocketData& sd = data[username];
        sd.username = username;
        sd.lobby_id = f.store.lobby.id;
        return WsContext{f.store.lobby.FindMember(username)->socket, &sd,
                         uWS::OpCode::TEXT};
    };

    engine.Store().Get<match::ecs::ActiveTypeReq>(engine.Registries().match)->type = "red";
    const std::vector<match::ecs::Entity> red2 = CardsByKind(engine, "vanilla:red_draw2");
    const std::vector<match::ecs::Entity> green2 = CardsByKind(engine, "vanilla:green_draw2");
    REQUIRE(red2.size() >= 2);
    REQUIRE(green2.size() >= 2);
    ForceHand(engine, *engine.FindPlayer(current), {red2[0], red2[1]});
    ForceHand(engine, *engine.FindPlayer(others[1]), {green2[0], green2[1]});

    CHECK(f.router.Dispatch(context_for(current), json{
        {"action", ws::ClientAction::kMatchPlayCard},
        {"card_id", BitsOf(engine, red2[0])}
    }));
    REQUIRE(engine.WindowOpen());

    const bool handled = f.router.Dispatch(
        context_for(others[1]),
        json{{"action", ws::ClientAction::kMatchWindowResponse},
             {"card_id", BitsOf(engine, green2[0])}});
    CHECK(handled);

    bool responded = false;
    for (const json& response : engine.ExportWindow()["responses"]) {
        if (response.value("player", std::string()) == others[1]
            && !response.value("pass", true)
            && response.value("kind", std::string()) == "vanilla:green_draw2") {
            responded = true;
        }
    }
    // INFO: RespondWindow recorded the card response; the other responder is
    //       still pending, so the window has not closed yet.
    CHECK(responded);
    CHECK(engine.WindowOpen());
}

TEST_CASE("match_window_response: neither pass nor card_id is an invalid payload") {
    MatchFixture f;
    f.SetupMatch(human_vs_bot(), LobbySettings{});

    PerSocketData sd;
    sd.username = "Alice";
    sd.lobby_id = f.store.lobby.id;
    WsContext ctx{f.store.lobby.FindMember("Alice")->socket, &sd, uWS::OpCode::TEXT};

    f.bus.Clear();
    CHECK(f.router.Dispatch(
        ctx, json{{"action", ws::ClientAction::kMatchWindowResponse}}));
    REQUIRE_FALSE(f.bus.sent.empty());
    const json err = json::parse(f.bus.sent.back().payload);
    CHECK_EQ(err["action"], "error");
    CHECK_EQ(err["code"], "invalid_payload");
}
}

// ---------------------------------------------------------------------------
// Final-review fix wave: the engine turn deadline is armed into the
// snapshot on every turn start.
// ---------------------------------------------------------------------------
TEST_SUITE("MatchController::TurnDeadline") {
TEST_CASE("OnTurnStartedSession arms the current player's engine turn deadline") {
    MatchFixture f;
    f.SetupMatch(human_vs_bot(),
                 settings_with_mode(BotTakeoverMode::kWaitUntilTurnEnd, 15'000));

    const std::optional<match::ecs::Entity> current =
        f.Engine().GetCurrentPlayer();
    REQUIRE(current.has_value());
    const match::ecs::TurnState* turn =
        f.Engine().Store().Get<match::ecs::TurnState>(*current);
    REQUIRE(turn != nullptr);
    CHECK(turn->turn_deadline_ms > 0);

    // INFO: C2 - the snapshot is the reconnect-safe deadline source.
    f.bus.Clear();
    f.store.lobby.session->BroadcastSnapshot(f.bus);
    int64_t deadline = 0;
    for (const SentFrame& frame : f.bus.sent) {
        const json packet = json::parse(frame.payload);
        if (packet.value("action", std::string()) != "match_state_updated") {
            continue;
        }
        deadline = packet["match_state"].value("turn_deadline_ms", int64_t{0});
        break;
    }
    CHECK(deadline > 0);
}
}


