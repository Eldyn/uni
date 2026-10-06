#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/view/view_util.hpp>

#include "support/fake_broadcaster.hpp"
#include "support/leak_scanner.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file view_leak_test.cpp
 * @brief Plays real bot matches and searches every byte sent to each seat.
 *
 * After every action the test lists the cards that a seat must not know: the
 * cards in the hands of other seats and the cards of the draw pile. It then
 * parses each frame that the seat received during that action, and the
 * snapshot of the seat, and looks for those cards by identity bits and by
 * entity handle.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using leak_scanner::HiddenCard;
using leak_scanner::Leak;
using match::engine::AssemblyResult;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadedMod;
using match::modload::LoadResult;
using match::modload::ScanModsDirectory;
using match::server::BotStep;
using match::server::HeuristicBotPolicy;
using match::server::MatchSession;
using nlohmann::json;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

bool LoadContent(Content& out) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    LoadResult load = ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (int index = 0; index < players; ++index) {
        MatchPlayerSpec spec;
        spec.username = "bot" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

AppWebSocket* SeatSocket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x300 + index));
}

HiddenCard HiddenCardOf(const MatchInstance& engine, ecs::Entity card) {
    HiddenCard hidden;
    hidden.entity = card;
    const std::optional<ecs::CompactCardV2> id =
        engine.Registries().CardId(card);
    hidden.bits = id.has_value() ? id->bits : 0u;
    return hidden;
}

/** @brief True when `viewer` holds any grant on `target`. */
bool HasGrant(const MatchInstance& engine, ecs::Entity target,
              ecs::Entity viewer) {
    const ecs::VisibilityGrant* grant =
        engine.Store().Get<ecs::VisibilityGrant>(target);
    if (grant == nullptr) return false;
    for (const ecs::VisibilityGrant::Entry& entry : grant->entries) {
        if (entry.viewer == viewer) return true;
    }
    return false;
}

/** @brief Cards in a window response are public by the act of responding. */
std::vector<ecs::Entity> PubliclyOfferedCards(const MatchInstance& engine) {
    std::vector<ecs::Entity> offered;
    if (!engine.WindowOpen()) return offered;
    const json window = engine.ExportWindow();
    for (const json& response : window.value("responses", json::array())) {
        ecs::Entity card{};
        if (response.contains("card")
            && match::view::EntityFromJson(response["card"], card)) {
            offered.push_back(card);
        }
    }
    return offered;
}

/**
 * @brief The cards `username` must not learn about right now.
 *
 * Every card in another seat's hand, and every card of the draw pile, unless
 * a visibility grant lets the seat see it.
 */
std::vector<HiddenCard> HiddenFrom(const MatchInstance& engine,
                                   const std::string& username) {
    const ecs::Entity viewer = *engine.FindPlayer(username);
    const std::vector<ecs::Entity> offered = PubliclyOfferedCards(engine);
    std::vector<HiddenCard> hidden;
    const auto add = [&](ecs::Entity holder,
                         const std::vector<ecs::Entity>& cards) {
        if (HasGrant(engine, holder, viewer)) return;
        for (ecs::Entity card : cards) {
            if (HasGrant(engine, card, viewer)) continue;
            bool is_offered = false;
            for (ecs::Entity entry : offered) is_offered |= entry == card;
            if (!is_offered) hidden.push_back(HiddenCardOf(engine, card));
        }
    };
    for (ecs::Entity player : engine.Registries().players) {
        if (player == viewer) continue;
        const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
        if (hand != nullptr) add(player, hand->cards);
    }
    const ecs::PileContents* draw_pile = engine.Store().Get<ecs::PileContents>(
        engine.Registries().draw_pile);
    if (draw_pile != nullptr) {
        add(engine.Registries().draw_pile, draw_pile->cards);
    }
    return hidden;
}

/**
 * @brief The cards a spectator must not learn about right now.
 *
 * Every card of the draw pile, and every card in the hand of a seat whose
 * owner has Streamer/Privacy Mode on.
 */
std::vector<HiddenCard> HiddenFromSpectator(
    const MatchInstance& engine,
    const std::vector<std::string>& private_seats) {
    const std::vector<ecs::Entity> offered = PubliclyOfferedCards(engine);
    std::vector<HiddenCard> hidden;
    const auto add = [&](const std::vector<ecs::Entity>& cards) {
        for (ecs::Entity card : cards) {
            bool is_offered = false;
            for (ecs::Entity entry : offered) is_offered |= entry == card;
            if (!is_offered) hidden.push_back(HiddenCardOf(engine, card));
        }
    };
    for (const std::string& username : private_seats) {
        const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(
            *engine.FindPlayer(username));
        if (hand != nullptr) add(hand->cards);
    }
    const ecs::PileContents* draw_pile = engine.Store().Get<ecs::PileContents>(
        engine.Registries().draw_pile);
    if (draw_pile != nullptr) add(draw_pile->cards);
    return hidden;
}

struct LeakReport {
    std::size_t frames_scanned = 0;
    std::vector<std::string> findings;
};

/** @brief The spectator of a scan and the privacy setting of each seat. */
struct SpectatorPlan {
    std::vector<std::string> private_seats;
    /** Seat whose setting flips at `flip_step` (empty: no flip). */
    std::string flip_seat;
    int flip_step = 0;
};

AppWebSocket* SpectatorSocket() {
    return reinterpret_cast<AppWebSocket*>(static_cast<std::uintptr_t>(0x400));
}

bool ContainsSeat(const std::vector<std::string>& seats,
                  const std::string& seat) {
    return std::ranges::find(seats, seat) != seats.end();
}

/** @brief Scan every frame sent to `socket` since the last call. */
void ScanSentFrames(const FakeBroadcaster& fake, AppWebSocket* socket,
                    std::size_t& cursor, const std::string& username,
                    const std::vector<HiddenCard>& hidden,
                    LeakReport& report) {
    for (; cursor < fake.sent.size(); ++cursor) {
        const SentFrame& frame = fake.sent[cursor];
        if (frame.to != socket) continue;
        ++report.frames_scanned;
        const json packet = json::parse(frame.payload);
        for (const Leak& leak : leak_scanner::FindLeaks(packet, hidden)) {
            report.findings.push_back(username + " " + leak.kind + " at "
                                      + leak.path + " in "
                                      + frame.payload.substr(0, 200));
        }
    }
}

/**
 * @brief Drive four bots to the end and scan every frame of every seat.
 *
 * Frames are scanned right after each action against the hidden set that
 * the engine state implies at that moment.
 */
LeakReport PlayAndScan(uint64_t seed, const SpectatorPlan& plan = {}) {
    Content content;
    REQUIRE(LoadContent(content));
    const std::vector<std::string> bots = {"bot0", "bot1", "bot2", "bot3"};
    std::map<std::string, AppWebSocket*> sockets;
    MatchSession::SocketMap socket_map;
    for (int index = 0; index < 4; ++index) {
        sockets[bots[index]] = SeatSocket(index);
        socket_map[bots[index]] = SeatSocket(index);
    }
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, seed);
    MatchSession session(std::move(engine), std::move(content.mods),
                         socket_map);
    HeuristicBotPolicy policy(12345, {});
    FakeBroadcaster fake;
    LeakReport report;
    std::map<std::string, std::size_t> cursors;

    std::vector<std::string> private_seats = plan.private_seats;
    for (const std::string& seat : private_seats) {
        session.SetSeatPrivacy(seat, true);
    }
    const bool has_spectator = !plan.private_seats.empty()
                               || !plan.flip_seat.empty();
    if (has_spectator) session.BindViewer("watcher", SpectatorSocket());
    std::size_t spectator_cursor = 0;

    const auto scan_all = [&]() {
        for (const std::string& bot : bots) {
            ScanSentFrames(fake, sockets[bot], cursors[bot], bot,
                           HiddenFrom(session.Engine(), bot), report);
        }
        if (!has_spectator) return;
        const std::vector<HiddenCard> hidden =
            HiddenFromSpectator(session.Engine(), private_seats);
        ScanSentFrames(fake, SpectatorSocket(), spectator_cursor, "watcher",
                       hidden, report);
    };

    session.EmitMatchStart(fake);
    session.BroadcastSnapshot(fake);
    scan_all();

    int steps = 0;
    int idle = 0;
    while (!session.Engine().IsMatchOver() && steps < 20000 && idle <= 200) {
        bool acted = false;
        const std::optional<json> pending = session.Engine().PendingInput();
        if (pending.has_value()) {
            const std::string target = match::view::ResolvePlayer(
                session.Engine(), pending->value("target", json()));
            if (!target.empty()) acted = BotStep(session, policy, target);
        } else if (session.Engine().WindowOpen()) {
            const json window = session.Engine().ExportWindow();
            for (const json& responder :
                 window.value("responders", json::array())) {
                if (!responder.is_string()) continue;
                acted = BotStep(session, policy, responder.get<std::string>())
                        || acted;
            }
        } else {
            const std::string current =
                session.Engine().GetCurrentPlayerUsername();
            if (!current.empty()) acted = BotStep(session, policy, current);
        }
        if (!plan.flip_seat.empty() && steps == plan.flip_step) {
            const bool now_private =
                !ContainsSeat(private_seats, plan.flip_seat);
            session.SetSeatPrivacy(plan.flip_seat, now_private);
            if (now_private) {
                private_seats.push_back(plan.flip_seat);
            } else {
                std::erase(private_seats, plan.flip_seat);
            }
        }
        session.Tick();
        session.EmitEvents(fake);
        session.BroadcastSnapshot(fake);
        scan_all();
        ++steps;
        idle = acted ? 0 : idle + 1;
    }
    REQUIRE(session.Engine().IsMatchOver());
    return report;
}

}  // namespace

TEST_CASE("leak: no byte sent to a seat names a card of another hand") {
    for (const uint64_t seed : {7u, 11u, 23u}) {
        const LeakReport report = PlayAndScan(seed);
        CHECK(report.frames_scanned > 100);
        for (const std::string& finding : report.findings) {
            FAIL_CHECK("seed " << seed << ": " << finding);
        }
    }
}

TEST_CASE("leak: no byte sent to a spectator names a private card") {
    SpectatorPlan plan;
    plan.private_seats = {"bot1", "bot3"};
    for (const uint64_t seed : {7u, 11u, 23u}) {
        const LeakReport report = PlayAndScan(seed, plan);
        CHECK(report.frames_scanned > 200);
        for (const std::string& finding : report.findings) {
            FAIL_CHECK("seed " << seed << ": " << finding);
        }
    }
}

TEST_CASE("leak: a spectator of an all-open table never gets the draw pile") {
    SpectatorPlan plan;
    plan.flip_seat = "bot0";
    plan.flip_step = 1000000;
    for (const uint64_t seed : {7u, 11u}) {
        const LeakReport report = PlayAndScan(seed, plan);
        CHECK(report.frames_scanned > 200);
        for (const std::string& finding : report.findings) {
            FAIL_CHECK("seed " << seed << ": " << finding);
        }
    }
}

TEST_CASE("leak: a setting that changes mid match leaks nothing afterward") {
    SpectatorPlan plan;
    plan.private_seats = {"bot2"};
    plan.flip_seat = "bot0";
    plan.flip_step = 40;
    for (const uint64_t seed : {7u, 11u}) {
        const LeakReport report = PlayAndScan(seed, plan);
        for (const std::string& finding : report.findings) {
            FAIL_CHECK("seed " << seed << ": " << finding);
        }
    }
}

TEST_CASE("leak: the spectator scan finds an injected leak") {
    Content content;
    REQUIRE(LoadContent(content));
    MatchSession::SocketMap socket_map;
    for (int index = 0; index < 4; ++index) {
        socket_map["bot" + std::to_string(index)] = SeatSocket(index);
    }
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7);
    MatchSession session(std::move(engine), std::move(content.mods),
                         socket_map);
    session.SetSeatPrivacy("bot1", true);
    session.BindViewer("watcher", SpectatorSocket());
    FakeBroadcaster fake;
    session.EmitMatchStart(fake);
    session.SendSnapshot(fake, SpectatorSocket(), "watcher", true);

    const std::vector<HiddenCard> hidden =
        HiddenFromSpectator(session.Engine(), {"bot1"});
    REQUIRE_FALSE(hidden.empty());
    LeakReport clean_report;
    std::size_t cursor = 0;
    ScanSentFrames(fake, SpectatorSocket(), cursor, "watcher", hidden,
                   clean_report);
    REQUIRE(clean_report.frames_scanned > 0);
    CHECK(clean_report.findings.empty());

    // INFO: the first pile card stands for an omniscient snapshot of the old
    //       kind that listed the draw pile in order.
    const ecs::PileContents* draw_pile = session.Engine().Store()
        .Get<ecs::PileContents>(session.Engine().Registries().draw_pile);
    const HiddenCard pile_card =
        HiddenCardOf(session.Engine(), draw_pile->cards.front());
    fake.Send(SpectatorSocket(),
              json{{"action", "match_state_updated"},
                   {"match_state",
                    json{{"draw_pile",
                          json{{"cards", json::array({pile_card.bits})}}}}}}
                  .dump());
    LeakReport leak_report;
    ScanSentFrames(fake, SpectatorSocket(), cursor, "watcher", hidden,
                   leak_report);
    CHECK(leak_report.findings.size() == 1);
}

TEST_CASE("leak: a spectator of an open seat does see its cards") {
    Content content;
    REQUIRE(LoadContent(content));
    MatchSession::SocketMap socket_map;
    for (int index = 0; index < 4; ++index) {
        socket_map["bot" + std::to_string(index)] = SeatSocket(index);
    }
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7);
    const ecs::Entity open_card =
        engine->Store().Get<ecs::Hand>(*engine->FindPlayer("bot0"))
            ->cards.front();
    const HiddenCard open = HiddenCardOf(*engine, open_card);
    MatchSession session(std::move(engine), std::move(content.mods),
                         socket_map);
    session.SetSeatPrivacy("bot1", true);
    session.BindViewer("watcher", SpectatorSocket());
    FakeBroadcaster fake;
    session.SendSnapshot(fake, SpectatorSocket(), "watcher", true);

    LeakReport report;
    std::size_t cursor = 0;
    ScanSentFrames(fake, SpectatorSocket(), cursor, "watcher", {open}, report);
    CHECK(report.findings.size() == 1);
}

TEST_CASE("leak: the scanner finds an injected leak") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7);
    const ecs::Entity other_hand_card =
        engine->Store().Get<ecs::Hand>(*engine->FindPlayer("bot1"))
            ->cards.front();
    const HiddenCard hidden = HiddenCardOf(*engine, other_hand_card);
    const std::vector<HiddenCard> hidden_cards = {hidden};

    const json clean = json{{"id", "uni:cards_drawn"},
                            {"payload", json{{"count", 2}}}};
    CHECK(leak_scanner::FindLeaks(clean, hidden_cards).empty());

    const json bits_leak =
        json{{"payload", json{{"cards", json::array({hidden.bits})}}}};
    const std::vector<Leak> bits_found =
        leak_scanner::FindLeaks(bits_leak, hidden_cards);
    REQUIRE(bits_found.size() == 1);
    CHECK(bits_found[0].kind == "card_bits");
    CHECK(bits_found[0].path == "/payload/cards/0");

    const json handle_leak = json{
        {"payload",
         json{{"target", json{{"index", other_hand_card.index},
                              {"generation", other_hand_card.generation}}}}}};
    const std::vector<Leak> handle_found =
        leak_scanner::FindLeaks(handle_leak, hidden_cards);
    REQUIRE(handle_found.size() == 1);
    CHECK(handle_found[0].kind == "entity_handle");

    const json small_hidden = json{{"payload", json{{"drawn_card", 3}}}};
    CHECK(leak_scanner::FindLeaks(small_hidden, {HiddenCard{{}, 3}}).size()
          == 1);
    const json card_counter =
        json{{"payload", json{{"card_count", 3}}}};
    CHECK(leak_scanner::FindLeaks(card_counter, {HiddenCard{{}, 3}}).empty());
    const json small_counter = json{{"payload", json{{"count", 3}}}};
    CHECK(leak_scanner::FindLeaks(small_counter, {HiddenCard{{}, 3}}).empty());

    const json other_generation = json{
        {"index", other_hand_card.index},
        {"generation", other_hand_card.generation + 1}};
    CHECK(leak_scanner::FindLeaks(other_generation, hidden_cards).empty());
}

TEST_CASE("leak: a leak injected into a real stream is caught") {
    Content content;
    REQUIRE(LoadContent(content));
    MatchSession::SocketMap socket_map;
    for (int index = 0; index < 4; ++index) {
        socket_map["bot" + std::to_string(index)] = SeatSocket(index);
    }
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7);
    MatchSession session(std::move(engine), std::move(content.mods),
                         socket_map);
    FakeBroadcaster fake;
    session.EmitMatchStart(fake);
    session.BroadcastSnapshot(fake);

    const std::vector<HiddenCard> hidden = HiddenFrom(session.Engine(), "bot0");
    REQUIRE_FALSE(hidden.empty());
    LeakReport clean_report;
    std::size_t cursor = 0;
    ScanSentFrames(fake, SeatSocket(0), cursor, "bot0", hidden, clean_report);
    REQUIRE(clean_report.frames_scanned > 0);
    CHECK(clean_report.findings.empty());

    // INFO: the injected frame mimics a server that forgot to redact a hand.
    const ecs::Entity leaked_entity = hidden[1].entity;
    json leaked = json{
        {"action", "match_event"},
        {"payload",
         json{{"cards", json::array({hidden[0].bits})},
              {"target", json{{"index", leaked_entity.index},
                              {"generation", leaked_entity.generation}}}}}};
    fake.Send(SeatSocket(0), leaked.dump());
    LeakReport leak_report;
    ScanSentFrames(fake, SeatSocket(0), cursor, "bot0", hidden, leak_report);
    CHECK(leak_report.findings.size() == 2);
}
