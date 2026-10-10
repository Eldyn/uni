#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/timers.hpp>
#include <match/view/view_util.hpp>

#include "support/fake_broadcaster.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

/**
 * @file acceptance_smoke_test.cpp
 * @brief One full-system run: shipped mods, the validator, four bots and a
 *        spectator, every event through the real wire projector.
 *
 * The mods load through the same scan and validation stages as the server.
 * The log output of the run is captured. An error-level line from a budget or
 * from the validator fails the test.
 */

namespace fs = std::filesystem;
using match::engine::AssemblyResult;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadedMod;
using match::modload::LoadResult;
using match::modload::ScanModsDirectory;
using match::modload::SemanticValidator;
using match::modload::ValidateLoadedMods;
using match::server::BotStep;
using match::server::HeuristicBotPolicy;
using match::server::MatchSession;
using nlohmann::json;

namespace {

const std::vector<std::string> kShippedMods = {
    "uni", "seven_zero", "test", "draw_stacking", "jump_in"};
const std::vector<std::string> kModsWithoutPresentation = {
    "seven_zero", "draw_stacking", "jump_in"};
const std::vector<std::string> kSeats = {"bot0", "bot1", "bot2", "bot3"};
constexpr const char* kSpectator = "watcher";
constexpr int64_t kStalledStepMs = 1000;
constexpr int kStepLimit = 20000;
constexpr int kIdleLimit = 200;

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

/** @brief Sends everything that the logger prints into a string. */
class LogCapture {
public:
    LogCapture() : previous_(std::cout.rdbuf(buffer_.rdbuf())) {}
    ~LogCapture() { std::cout.rdbuf(previous_); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::string Text() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
    std::streambuf* previous_;
};

struct FakeClock {
    int64_t now = 0;
    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef deck;
};

Content LoadValidatedContent() {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    LoadResult load = ScanModsDirectory((root / "mods").string());
    REQUIRE(load.ok());
    const SemanticValidator validator((root / "contract" / "schemas").string());
    ValidateLoadedMods(load, validator);
    REQUIRE(load.ok());
    Content content;
    content.mods = std::move(load.mods);
    for (const std::string& id : kShippedMods) {
        const bool present = std::ranges::any_of(
            content.mods,
            [&](const LoadedMod& mod) { return mod.manifest.id == id; });
        REQUIRE_MESSAGE(present, "mod removed by the validator: " << id);
    }
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "test:classic_bombs") content.deck = deck;
        }
    }
    REQUIRE_FALSE(content.deck.deck_id.empty());
    content.deck.mods = kShippedMods;
    return content;
}

AppWebSocket* SocketOf(std::size_t index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x900 + index));
}

struct RunResult {
    std::map<std::string, std::vector<json>> match_events;
    int steps = 0;
};

RunResult PlayToTheEnd(Content content, uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    MatchSession::SocketMap sockets;
    for (std::size_t index = 0; index < kSeats.size(); ++index) {
        MatchPlayerSpec spec;
        spec.username = kSeats[index];
        options.players.push_back(spec);
        sockets[kSeats[index]] = SocketOf(index);
    }
    AssemblyResult assembly =
        MatchAssembler::Assemble(content.mods, content.deck, options);
    REQUIRE(assembly.ok());
    FakeClock clock;
    match::WindowConfig window;
    window.window_ms = 7000;
    window.mode = match::WindowMode::kFixed;
    auto engine = std::make_unique<MatchInstance>(std::move(assembly.assembly),
                                                  window, clock.Fn());
    MatchSession session(std::move(engine), std::move(content.mods), sockets);
    session.BindViewer(kSpectator, SocketOf(kSeats.size()));
    HeuristicBotPolicy policy(12345, {});
    FakeBroadcaster fake;

    session.EmitMatchStart(fake);
    session.BroadcastSnapshot(fake);
    RunResult run;
    int idle = 0;
    while (!session.Engine().IsMatchOver() && run.steps < kStepLimit
           && idle <= kIdleLimit) {
        bool acted = false;
        const std::optional<json> pending = session.Engine().PendingInput();
        if (pending.has_value()) {
            const std::string target = match::view::ResolvePlayer(
                session.Engine(), pending->value("target", json()));
            if (!target.empty()) acted = BotStep(session, policy, target);
        } else if (session.Engine().WindowOpen()) {
            const json open_window = session.Engine().ExportWindow();
            for (const json& responder :
                 open_window.value("responders", json::array())) {
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
        ++run.steps;
        idle = acted ? 0 : idle + 1;
    }
    REQUIRE(session.Engine().IsMatchOver());

    std::map<AppWebSocket*, std::string> names;
    for (std::size_t index = 0; index < kSeats.size(); ++index) {
        names[SocketOf(index)] = kSeats[index];
    }
    names[SocketOf(kSeats.size())] = kSpectator;
    for (const SentFrame& frame : fake.sent) {
        const json packet = json::parse(frame.payload);
        if (packet.value("action", std::string()) != "match_event") continue;
        run.match_events[names.at(frame.to)].push_back(packet);
    }
    return run;
}

void CheckStreams(const RunResult& run) {
    std::vector<std::string> recipients = kSeats;
    recipients.emplace_back(kSpectator);
    for (const std::string& recipient : recipients) {
        CAPTURE(recipient);
        const std::vector<json>& packets = run.match_events.at(recipient);
        REQUIRE_FALSE(packets.empty());
        for (std::size_t index = 0; index < packets.size(); ++index) {
            CHECK_MESSAGE(packets[index]["sequence"] == index,
                          "gap at packet " << index);
        }
        const auto ended = std::ranges::count_if(
            packets, [](const json& packet) {
                return packet["id"] == "match_ended";
            });
        CHECK(ended == 1);
    }
}

}  // namespace

TEST_CASE("acceptance: the shipped mods play four bot matches to the end") {
    LogCapture log;
    Content content = LoadValidatedContent();
    for (const std::string& id : kModsWithoutPresentation) {
        const auto mod = std::ranges::find_if(
            content.mods,
            [&](const LoadedMod& entry) { return entry.manifest.id == id; });
        REQUIRE(mod != content.mods.end());
        CHECK_MESSAGE(mod->presentations.empty(), id);
        CHECK_MESSAGE(mod->bindings.empty(), id);
        CHECK_MESSAGE(mod->texts.empty(), id);
    }

    for (const uint64_t seed : {7u, 11u, 23u}) {
        CAPTURE(seed);
        RunResult run;
        CHECK_NOTHROW(run = PlayToTheEnd(content, seed));
        CHECK(run.steps > 0);
        CheckStreams(run);
    }

    const std::string output = log.Text();
    CHECK_MESSAGE(output.find("ERROR") == std::string::npos, output);
    CHECK(output.find("ModEventBudget") == std::string::npos);
}
