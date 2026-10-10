#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_util.hpp>

#include "support/fake_broadcaster.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file event_stream_golden_test.cpp
 * @brief The wire stream of a seeded bot match equals a stored golden file.
 *
 * The test drives four bots to the end of a match and records every
 * `match_event` packet for two seats and one spectator. For each packet the
 * golden file holds the id, the sequence, the cause, the envelope keys, the
 * payload keys and a digest of the payload. A seat has no gap in `sequence`.
 *
 * Set `UNI_UPDATE_GOLDEN=1` to write the golden files again; a default run
 * never writes them.
 */

namespace fs = std::filesystem;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadedMod;
using match::server::BotStep;
using match::server::HeuristicBotPolicy;
using match::server::MatchSession;
using nlohmann::json;

namespace {

const std::vector<uint64_t> kSeeds = {7, 11, 23};

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

fs::path GoldenPath(uint64_t seed) {
    return ProjectRoot() / "tests" / "fixtures" / "golden_event_stream"
           / ("seed_" + std::to_string(seed) + ".json");
}

AppWebSocket* Socket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x900 + index));
}

std::vector<std::string> SortedKeys(const json& object) {
    std::vector<std::string> keys;
    if (object.is_object()) {
        for (auto it = object.begin(); it != object.end(); ++it) {
            keys.push_back(it.key());
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

json Summary(const json& packet) {
    json out = json::object();
    out["id"] = packet["id"];
    out["sequence"] = packet["sequence"];
    out["cause"] = packet.contains("cause") ? packet["cause"] : json(nullptr);
    out["envelope_keys"] = SortedKeys(packet);
    out["payload_keys"] = SortedKeys(packet["payload"]);
    out["payload_digest"] =
        match::view::StableDigest(packet["payload"].dump());
    return out;
}

/** @brief The summaries of every `match_event` frame sent to `socket`. */
json StreamOf(const FakeBroadcaster& fake, AppWebSocket* socket) {
    std::vector<json> frames;
    for (const SentFrame& sent : fake.sent) {
        if (sent.to != socket) continue;
        const json frame = json::parse(sent.payload);
        if (frame.value("action", std::string()) != "match_event") continue;
        frames.push_back(frame);
    }
    json stream = json::array();
    for (const json& frame : frames) stream.push_back(Summary(frame));
    return stream;
}

json PlayMatch(uint64_t seed) {
    match::modload::LoadResult load = match::modload::ScanModsDirectory(
        (ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    DeckDef classic;
    for (const LoadedMod& mod : load.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") classic = deck;
        }
    }
    REQUIRE_FALSE(classic.deck_id.empty());

    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    MatchSession::SocketMap sockets;
    for (int index = 0; index < 4; ++index) {
        MatchPlayerSpec spec;
        spec.username = "bot" + std::to_string(index);
        options.players.push_back(spec);
        sockets[spec.username] = Socket(index);
    }
    match::engine::AssemblyResult result =
        MatchAssembler::Assemble(load.mods, classic, options);
    REQUIRE(result.ok());
    int64_t now = 0;
    match::WindowConfig window;
    window.window_ms = 7000;
    window.mode = match::WindowMode::kFixed;
    MatchSession session(
        std::make_unique<MatchInstance>(std::move(result.assembly), window,
                                        [&now]() { return now; }),
        std::move(load.mods), sockets);
    FakeBroadcaster fake;
    session.BindViewer("watcher", Socket(9));
    session.SetSeatPrivacy("bot1", true);
    session.EmitMatchStart(fake);
    HeuristicBotPolicy policy(seed, {});

    int idle = 0;
    for (int steps = 0; steps < 20000 && !session.Engine().IsMatchOver();
         ++steps) {
        bool acted = false;
        const std::optional<json> pending = session.Engine().PendingInput();
        if (pending.has_value()) {
            const std::string target = match::view::ResolvePlayer(
                session.Engine(), pending->value("target", json()));
            if (!target.empty()) acted = BotStep(session, policy, target);
        } else if (session.Engine().WindowOpen()) {
            const json open = session.Engine().ExportWindow();
            for (const json& responder :
                 open.value("responders", json::array())) {
                if (!responder.is_string()) continue;
                acted = BotStep(session, policy, responder.get<std::string>())
                        || acted;
            }
        } else {
            const std::string current =
                session.Engine().GetCurrentPlayerUsername();
            if (!current.empty()) acted = BotStep(session, policy, current);
        }
        session.Tick();
        session.EmitEvents(fake);
        idle = acted ? 0 : idle + 1;
        if (idle > 200) break;
    }
    REQUIRE(session.Engine().IsMatchOver());

    json streams = json::object();
    streams["bot0"] = StreamOf(fake, Socket(0));
    streams["bot1"] = StreamOf(fake, Socket(1));
    streams["spectator"] = StreamOf(fake, Socket(9));
    return streams;
}

}  // namespace

TEST_CASE("event stream: a seeded bot match equals the golden file") {
    const char* update = std::getenv("UNI_UPDATE_GOLDEN");
    for (const uint64_t seed : kSeeds) {
        CAPTURE(seed);
        const json streams = PlayMatch(seed);
        for (auto it = streams.begin(); it != streams.end(); ++it) {
            CAPTURE(it.key());
            REQUIRE(it.value().size() > 20);
        }
        if (update != nullptr && std::string(update) == "1") {
            fs::create_directories(GoldenPath(seed).parent_path());
            std::ofstream(GoldenPath(seed)) << streams.dump(1) << "\n";
        }
        std::ifstream input(GoldenPath(seed));
        REQUIRE(input.good());
        const json golden = json::parse(input);
        CHECK(streams == golden);
    }
}

TEST_CASE("event stream: a seat sees no gap in sequence") {
    for (const uint64_t seed : kSeeds) {
        CAPTURE(seed);
        const json streams = PlayMatch(seed);
        for (auto it = streams.begin(); it != streams.end(); ++it) {
            CAPTURE(it.key());
            uint32_t expected = 0;
            for (const json& entry : it.value()) {
                CHECK(entry["sequence"].get<uint32_t>() == expected);
                ++expected;
            }
        }
    }
}
