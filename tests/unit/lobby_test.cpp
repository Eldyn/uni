#include <doctest/doctest.h>
#include <common/lobby.hpp>
#include <common/contract.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/server/match_session.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("LobbyMember defaults to not ready") {
    LobbyMember member("alice", nullptr, true, false, 0);
    CHECK_FALSE(member.is_ready);
}

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

TEST_CASE("lobby: Sanitize clamps starting_cards down when it would exceed the deck size") {
    LobbySettings settings;
    settings.max_players = contract::kMaxLobbyMembers;
    settings.starting_cards = contract::kStartingCardsMax;
    settings.count_zeros = 1;
    settings.count_numbered = 1;
    settings.count_skips = 0;
    settings.count_reverses = 0;
    settings.count_draw_two = 0;
    settings.count_wild = 0;
    settings.count_wild_draw_four = 0;
    REQUIRE_EQ(settings.DeckSize(), 40);

    settings.Sanitize();

    CHECK_EQ(settings.starting_cards, settings.DeckSize() / settings.max_players);
}

TEST_CASE("lobby: Sanitize floors the deck-size clamp at one card") {
    LobbySettings settings;
    settings.max_players = contract::kMaxLobbyMembers;
    settings.starting_cards = 7;
    settings.count_zeros = 0;
    settings.count_numbered = 0;
    settings.count_skips = 1;
    settings.count_reverses = 0;
    settings.count_draw_two = 0;
    settings.count_wild = 0;
    settings.count_wild_draw_four = 0;
    REQUIRE_EQ(settings.DeckSize(), 4);

    settings.Sanitize();

    CHECK_EQ(settings.starting_cards, 1);
}

TEST_CASE("lobby: Sanitize clamps out-of-range bot_mode") {
    LobbySettings settings;
    settings.bot_mode = static_cast<BotTakeoverMode>(contract::kBotModeMax + 1);

    settings.Sanitize();

    CHECK(static_cast<int>(settings.bot_mode) == contract::kBotModeMax);
}

TEST_CASE("lobby: Sanitize deduplicates mod names preserving order") {
    LobbySettings settings;
    settings.active_mods = {"seven_zero", "not_a_real_mod", "force_play"};

    settings.Sanitize();

    // INFO: unknown mod names are no longer stripped here; the mod loader
    //       ignores ids it cannot resolve, so Sanitize only dedupes.
    CHECK(settings.active_mods ==
          std::vector<std::string>{"seven_zero", "not_a_real_mod", "force_play"});
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

namespace fs = std::filesystem;

/** @brief Locate the repo root so mods load cwd-independently. */
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

struct SessionContent {
    std::vector<match::modload::LoadedMod> mods;
    match::modload::DeckDef classic;
};

bool LoadSessionContent(SessionContent& out) {
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

const SessionContent& Content() {
    static SessionContent content;
    static const bool loaded = LoadSessionContent(content);
    REQUIRE_MESSAGE(loaded, "failed to load mods/classic deck");
    return content;
}

/**
 * @brief Attach a started new-engine session for `players` to `lobby`.
 *
 * Mirrors `LobbyController::HandleStartGame`: the vanilla classic deck plus
 * any rule mods named by `lobby.settings.active_mods`.
 */
void AttachSession(Lobby& lobby,
                   const std::vector<std::pair<std::string, bool>>& players) {
    const SessionContent& content = Content();
    match::modload::DeckDef deck = content.classic;
    for (const std::string& mod : lobby.settings.active_mods) {
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
    options.starting_cards = lobby.settings.starting_cards;
    options.seed = 12345;
    for (const auto& [username, is_bot] : players) {
        options.players.push_back({username, is_bot, true, true});
    }

    match::engine::AssemblyResult result =
        match::engine::MatchAssembler::Assemble(active_mods, deck, options);
    std::string assembly_error = result.error.has_value()
                                     ? result.error->message
                                     : std::string("assembly failed");
    REQUIRE_MESSAGE(result.ok(), assembly_error);
    auto engine = std::make_unique<match::engine::MatchInstance>(
        std::move(result.assembly));

    match::server::MatchSession::SocketMap sockets;
    for (const auto& [username, is_bot] : players) {
        (void)is_bot;
        sockets[username] = nullptr;
    }
    lobby.session = std::make_unique<match::server::MatchSession>(
        std::move(engine), std::move(active_mods), std::move(sockets));
}

/** @brief The engine `PlayerInfo` for `username`, or nullptr. */
const match::ecs::PlayerInfo* EnginePlayer(Lobby& lobby,
                                           const std::string& username) {
    const auto entity = lobby.session->Engine().FindPlayer(username);
    if (!entity.has_value()) return nullptr;
    return lobby.session->Engine().Store().Get<match::ecs::PlayerInfo>(*entity);
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

    AttachSession(lobby, {{"Host", false}, {"BotX", true}});

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

    AttachSession(lobby, {{"Alice", false}, {"Bob", false}});

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Alice", rng);

    CHECK_EQ(result.match_outcome, MemberRemovalOutcome::kMatchAborted);
    // session is deliberately left intact so the caller can persist its state
    // before tearing it down itself.
    CHECK(lobby.session);
    CHECK_EQ(lobby.members.size(), 1);
}

TEST_CASE("lobby: RemoveMember replaces the departing player with a bot") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_replacement = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    AttachSession(lobby, {{"Alice", false}, {"Bob", false}});

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Alice", rng);

    CHECK_EQ(result.match_outcome, MemberRemovalOutcome::kPlayerReplacedByBot);
    CHECK_FALSE(result.new_bot_name.empty());
    REQUIRE(lobby.session);
    CHECK_EQ(lobby.members.size(), 2);
    bool bot_present = false;
    for (const auto& m : lobby.members)
        if (m.username == result.new_bot_name && m.is_bot) bot_present = true;
    CHECK(bot_present);
}

TEST_CASE("lobby: a bot replacement renames the seat and unbinds the leaver") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_replacement = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    AttachSession(lobby, {{"Alice", false}, {"Bob", false}});
    auto* leaver_socket = reinterpret_cast<AppWebSocket*>(0x1);
    REQUIRE(lobby.session->BindSocket("Alice", leaver_socket));

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Alice", rng);

    REQUIRE_EQ(result.match_outcome, MemberRemovalOutcome::kPlayerReplacedByBot);
    CHECK_NE(result.new_bot_name, "Alice");
    CHECK(lobby.FindMember("Alice") == nullptr);
    CHECK_FALSE(lobby.session->Engine().FindPlayer("Alice").has_value());
    CHECK(lobby.session->Engine().FindPlayer(result.new_bot_name).has_value());

    const auto& sockets = lobby.session->Sockets();
    CHECK_FALSE(sockets.contains("Alice"));
    REQUIRE(sockets.contains(result.new_bot_name));
    CHECK(sockets.at(result.new_bot_name) == nullptr);
}

TEST_CASE("lobby: RemoveMember replacing the current turn-holder reports was_their_turn") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_replacement = true;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    AttachSession(lobby, {{"Alice", false}, {"Bob", false}});
    std::string turn_holder =
        lobby.session->Engine().GetCurrentPlayerUsername();

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

    AttachSession(lobby, {{"Alice", false}, {"Bob", false}});
    std::string turn_holder =
        lobby.session->Engine().GetCurrentPlayerUsername();
    std::string other = (turn_holder == "Alice") ? "Bob" : "Alice";

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember(other, rng);

    CHECK_FALSE(result.was_their_turn);
}

TEST_CASE("lobby: RemoveMember erases the member but keeps the engine seat") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.quit_deletes_match = false;
    lobby.settings.allow_bot_replacement = false;
    lobby.members.emplace_back("Alice", nullptr, true, false);
    lobby.members.emplace_back("Bob", nullptr, true, false);

    AttachSession(lobby, {{"Alice", false}, {"Bob", false}});

    std::mt19937 rng(42);
    auto result = lobby.RemoveMember("Alice", rng);

    CHECK_EQ(result.match_outcome, MemberRemovalOutcome::kPlayerDroppedFromEngine);
    REQUIRE(lobby.session);
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

    AttachSession(lobby, {{"Alice", false}, {"Bot1", true}});

    auto result = lobby.AddOrHijack("Charlie", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kHijackedBot);
    const match::ecs::PlayerInfo* engine_player =
        EnginePlayer(lobby, "Charlie");
    REQUIRE(engine_player != nullptr);
    CHECK_FALSE(engine_player->is_bot);
    CHECK(EnginePlayer(lobby, "Bot1") == nullptr);
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

TEST_CASE("lobby: AddOrHijack admits mid-game joiner as spectator when no bots are hijackable") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_takeover = false;
    lobby.members.emplace_back("Alice", nullptr, true, false);

    AttachSession(lobby, {{"Alice", false}, {"Bob", false}});

    auto result = lobby.AddOrHijack("Charlie", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kJoinedAsSpectator);
    LobbyMember* m = lobby.FindMember("Charlie");
    REQUIRE(m);
    CHECK(m->is_spectator);
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

TEST_CASE("lobby: a sanitized 8-player lobby fills with humans then bots up to max_players") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.max_players = 8;
    lobby.settings.bot_count = 5;
    lobby.settings.allow_bot_takeover = false;
    lobby.settings.Sanitize();
    CHECK_EQ(lobby.settings.max_players, 8);

    for (int i = 0; i < 6; ++i) {
        auto result = lobby.AddOrHijack("Human" + std::to_string(i), nullptr);
        CHECK_EQ(result.outcome, JoinOutcome::kJoinedEmptySlot);
    }
    REQUIRE_EQ(lobby.members.size(), 6);

    std::mt19937 rng(42);
    lobby.SyncBots(rng);

    CHECK_EQ(lobby.members.size(), 8);
    CHECK_EQ(CountBots(lobby), 2);

    auto overflow = lobby.AddOrHijack("Overflow", nullptr);
    CHECK_EQ(overflow.outcome, JoinOutcome::kLobbyFull);
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

TEST_CASE("lobby: AddOrHijack decrements settings.bot_count when hijacking a bot") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_takeover = true;
    lobby.settings.bot_count = 2;
    lobby.members.emplace_back("Alice", nullptr, true, false, 0);
    lobby.members.emplace_back("Bot1", nullptr, true, true, 1);

    auto result = lobby.AddOrHijack("Charlie", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kHijackedBot);
    CHECK_EQ(lobby.settings.bot_count, 1);
}

TEST_CASE("lobby: AddOrHijack never drives settings.bot_count below zero") {
    Lobby lobby;
    lobby.id = 1;
    lobby.settings.allow_bot_takeover = true;
    lobby.settings.bot_count = 0;
    lobby.members.emplace_back("Alice", nullptr, true, false, 0);
    lobby.members.emplace_back("Bot1", nullptr, true, true, 1);

    auto result = lobby.AddOrHijack("Charlie", nullptr);

    CHECK_EQ(result.outcome, JoinOutcome::kHijackedBot);
    CHECK_EQ(lobby.settings.bot_count, 0);
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

TEST_CASE("lobby: LobbySettings ranked defaults to true and roundtrips through json") {
    LobbySettings settings;
    CHECK(settings.ranked == true);

    nlohmann::json j = settings;
    CHECK(j["ranked"] == true);

    j["ranked"] = false;
    LobbySettings unranked = j.get<LobbySettings>();
    CHECK(unranked.ranked == false);
}

TEST_CASE("lobby: 120s grace period is respected") {
    Lobby lobby;
    lobby.id = 1;
    lobby.members.emplace_back("Alice", nullptr, false, false);
    lobby.members.back().disconnected_at = std::chrono::steady_clock::now() - std::chrono::seconds(60);

    lobby.members.emplace_back("Bob", nullptr, false, false);
    lobby.members.back().disconnected_at = std::chrono::steady_clock::now() - std::chrono::seconds(130);

    auto expired = lobby.CollectExpiredDisconnects(std::chrono::steady_clock::now(), 120'000);
    REQUIRE_EQ(expired.size(), 1);
    CHECK_EQ(expired[0], "Bob");
}
