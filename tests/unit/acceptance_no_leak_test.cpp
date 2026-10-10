#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/timers.hpp>
#include <match/view/view_util.hpp>

#include "support/fake_broadcaster.hpp"
#include "support/hidden_cards.hpp"
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
 * @file acceptance_no_leak_test.cpp
 * @brief The acceptance test "No leak" as one combined run.
 *
 * Three bot seats and two spectators play a seeded match to the end. One seat
 * has privacy on. One seat joins late, and one spectator joins late. Both late
 * joiners get the warm-up snapshot with the tail. A snapshot request is
 * answered for each kind of recipient. Every frame that the server sends to
 * every recipient is parsed, and the scanner looks for the identity bits and
 * the entity handle of each card that the recipient must not know. A mod
 * event is emitted on every play, so the mod path is in the scan too.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using leak_scanner::HiddenCard;
using leak_scanner::Leak;
using match::engine::AssemblyResult;
using match::modload::BehaviorGraph;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::engine::ModSystem;
using match::modload::DeckDef;
using match::modload::LoadedMod;
using match::modload::LoadResult;
using match::modload::ScanModsDirectory;
using match::server::BotStep;
using match::server::HeuristicBotPolicy;
using match::server::MatchSession;
using nlohmann::json;

namespace {

constexpr const char* kModEventId = "test:apple_eaten";
constexpr int kLateJoinStep = 8;
constexpr int kSnapshotRequestStep = 14;
constexpr const char* kLateSeat = "bot2";
constexpr const char* kPrivateSeat = "bot1";

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        const fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef deck;
};

bool LoadContent(Content& out, const std::vector<std::string>& deck_mods) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    LoadResult load = ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "test:classic_bombs") out.deck = deck;
        }
    }
    if (out.deck.deck_id.empty()) return false;
    out.deck.mods = deck_mods;
    return true;
}

void AttachModEventOnPlay(MatchInstance& engine) {
    const json emit_node = json::parse(R"({
        "id": "n1",
        "op": "emit_event",
        "args": {
            "id": "test:apple_eaten",
            "payload": {"target": "@self", "color": "red", "count": 3,
                        "secret": "hidden"}
        }
    })");
    BehaviorGraph graph;
    graph.nodes = json::array({emit_node});
    graph.raw = json{{"nodes", graph.nodes}};
    match::engine::MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:emit_system";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}

struct FakeClock {
    int64_t now = 0;
    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

constexpr int64_t kStalledStepMs = 1000;

std::unique_ptr<MatchInstance> MakeEngine(Content& content, uint64_t seed,
                                          FakeClock& clock) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (const char* name : {"bot0", "bot1", "bot2"}) {
        MatchPlayerSpec spec;
        spec.username = name;
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.deck, options);
    const std::string message =
        result.error.has_value() ? result.error->message : "assembly failed";
    REQUIRE_MESSAGE(result.ok(), message);
    match::WindowConfig window;
    window.window_ms = 7000;
    window.mode = match::WindowMode::kFixed;
    auto engine = std::make_unique<MatchInstance>(std::move(result.assembly),
                                                  window, clock.Fn());
    AttachModEventOnPlay(*engine);
    return engine;
}

AppWebSocket* SocketOf(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x700 + index));
}

struct Recipient {
    std::string username;
    AppWebSocket* socket = nullptr;
    bool is_spectator = false;
    bool joined = false;
    std::size_t cursor = 0;
};

struct ScanReport {
    std::size_t frames_scanned = 0;
    std::size_t mod_events_seen = 0;
    std::size_t tails_seen = 0;
    std::size_t snapshot_answers_seen = 0;
    std::vector<std::string> findings;
};

void ScanNewFrames(const FakeBroadcaster& fake, Recipient& recipient,
                   const std::vector<HiddenCard>& hidden,
                   ScanReport& report) {
    for (; recipient.cursor < fake.sent.size(); ++recipient.cursor) {
        const SentFrame& frame = fake.sent[recipient.cursor];
        if (frame.to != recipient.socket) continue;
        ++report.frames_scanned;
        const json packet = json::parse(frame.payload);
        if (packet.value("id", std::string()) == kModEventId) {
            ++report.mod_events_seen;
        }
        if (packet.value("action", std::string()) == "match_state_updated"
            && packet["match_state"].contains("tail")) {
            ++report.tails_seen;
        }
        for (const Leak& leak : leak_scanner::FindLeaks(packet, hidden)) {
            report.findings.push_back(recipient.username + " " + leak.kind
                                      + " at " + leak.path + " in "
                                      + frame.payload.substr(0, 200));
        }
    }
}

std::vector<HiddenCard> HiddenFor(const MatchInstance& engine,
                                  const Recipient& recipient) {
    if (!recipient.is_spectator) {
        return hidden_cards::HiddenFrom(engine, recipient.username);
    }
    return hidden_cards::HiddenFromSpectator(engine, {kPrivateSeat});
}

ScanReport PlayAndScanEveryone(uint64_t seed,
                               const std::vector<std::string>& deck_mods) {
    Content content;
    REQUIRE(LoadContent(content, deck_mods));
    MatchSession::SocketMap socket_map;
    socket_map["bot0"] = SocketOf(0);
    socket_map["bot1"] = SocketOf(1);
    socket_map[kLateSeat] = nullptr;
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, seed, clock);
    MatchSession session(std::move(engine), std::move(content.mods),
                         socket_map);
    session.SetSeatPrivacy(kPrivateSeat, true);
    session.SetSnapshotRequestIntervalMs(0);

    std::vector<Recipient> recipients = {
        {"bot0", SocketOf(0), false, true, 0},
        {"bot1", SocketOf(1), false, true, 0},
        {kLateSeat, SocketOf(2), false, false, 0},
        {"watcher", SocketOf(3), true, true, 0},
        {"late_watcher", SocketOf(4), true, false, 0},
    };
    FakeBroadcaster fake;
    ScanReport report;
    session.BindViewer("watcher", SocketOf(3));
    HeuristicBotPolicy policy(12345, {});

    const auto scan_all = [&]() {
        for (Recipient& recipient : recipients) {
            if (!recipient.joined) continue;
            ScanNewFrames(fake, recipient,
                          HiddenFor(session.Engine(), recipient), report);
        }
    };
    const auto join_late = [&]() {
        REQUIRE(session.BindSocket(kLateSeat, SocketOf(2)));
        session.BindViewer("late_watcher", SocketOf(4));
        for (Recipient& recipient : recipients) {
            if (recipient.joined) continue;
            recipient.joined = true;
            session.SendSnapshot(fake, recipient.socket, recipient.username,
                                 recipient.is_spectator);
        }
    };
    const auto answer_snapshot_requests = [&]() {
        for (const Recipient& recipient : recipients) {
            const std::size_t before = fake.sent.size();
            CHECK(session.HandleSnapshotRequest(
                fake, recipient.username, match::view::SnapshotOptions{}));
            for (std::size_t index = before; index < fake.sent.size();
                 ++index) {
                if (fake.sent[index].to == recipient.socket) {
                    ++report.snapshot_answers_seen;
                }
            }
        }
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
        if (!acted) clock.now += kStalledStepMs;
        session.Tick();
        session.EmitEvents(fake);
        session.BroadcastSnapshot(fake);
        if (steps == kLateJoinStep) join_late();
        if (steps == kSnapshotRequestStep) answer_snapshot_requests();
        scan_all();
        ++steps;
        idle = acted ? 0 : idle + 1;
    }
    REQUIRE(session.Engine().IsMatchOver());
    REQUIRE(steps > kSnapshotRequestStep);
    return report;
}

}  // namespace

TEST_CASE("acceptance: no byte sent to any recipient names a hidden card") {
    const std::vector<std::vector<std::string>> configurations = {
        {"uni", "test"},
        {"uni", "test", "seven_zero", "draw_stacking", "jump_in"},
    };
    for (const std::vector<std::string>& deck_mods : configurations) {
        for (const uint64_t seed : {7u, 11u, 23u}) {
            CAPTURE(seed);
            CAPTURE(deck_mods.size());
            const ScanReport report = PlayAndScanEveryone(seed, deck_mods);
            CHECK(report.frames_scanned > 300);
            CHECK(report.mod_events_seen > 0);
            CHECK(report.tails_seen >= 2);
            CHECK(report.snapshot_answers_seen >= 5);
            for (const std::string& finding : report.findings) {
                FAIL_CHECK("seed " << seed << ": " << finding);
            }
        }
    }
}
