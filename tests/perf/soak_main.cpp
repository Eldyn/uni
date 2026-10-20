// tests/perf/soak_main.cpp
// Memory and latency soak: N headless lobbies (four bots and a spectator
// each) play through the real engine, view projector and wire dump, the same
// path MatchSession takes on the server. A counting broadcaster stands in
// for the sockets, so the numbers show engine and view cost, not recorder
// cost.
//
//   uni_soak [--lobbies=N] [--max-steps=N] [--root=PATH] [--seed=N]
#include <malloc.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/timers.hpp>
#include <match/view/view_util.hpp>
#include <transport/ibroadcaster.hpp>

#include "mem_track.hpp"

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

const std::vector<std::string> kShippedMods = {
    "uni", "seven_zero", "test", "draw_stacking", "jump_in"};
std::vector<std::string> kSeats = {"bot0", "bot1", "bot2", "bot3"};
constexpr const char* kSpectator = "watcher";
constexpr const char* kDeckId = "test:classic_bombs";
constexpr int64_t kStalledStepMs = 1000;
constexpr int kIdleLimit = 200;
constexpr int kWarmSteps = 50;
constexpr double kBytesPerKb = 1024.0;

struct Options {
    int lobbies = 100;
    int seats = 4;
    bool breakdown = false;
    int max_steps = 20000;
    uint64_t seed = 7;
    std::string root = ".";
};

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto value = [&](const std::string& key) -> std::optional<std::string> {
            if (argument.rfind(key, 0) != 0) return std::nullopt;
            return argument.substr(key.size());
        };
        if (auto text = value("--lobbies=")) options.lobbies = std::stoi(*text);
        else if (auto text = value("--seats=")) options.seats = std::stoi(*text);
        else if (argument == "--breakdown") options.breakdown = true;
        else if (auto text = value("--max-steps=")) options.max_steps = std::stoi(*text);
        else if (auto text = value("--seed=")) options.seed = std::stoull(*text);
        else if (auto text = value("--root=")) options.root = *text;
    }
    return options;
}

/** @brief Counts frames and bytes instead of storing them. */
class CountingBroadcaster : public IBroadcaster {
public:
    uint64_t frames = 0;
    uint64_t bytes = 0;

    bool Send(AppWebSocket*, const std::string& message, uWS::OpCode = uWS::OpCode::TEXT,
              bool = true, bool = false) override {
        ++frames;
        bytes += message.size();
        return true;
    }
    void Publish(const std::string&, const std::string& message,
                 uWS::OpCode = uWS::OpCode::TEXT, bool = true) override {
        ++frames;
        bytes += message.size();
    }
    void Subscribe(AppWebSocket*, const std::string&) override {}
    void Unsubscribe(AppWebSocket*, const std::string&) override {}
};

struct FakeClock {
    int64_t now = 0;
    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

AppWebSocket* SocketOf(std::size_t index) {
    return reinterpret_cast<AppWebSocket*>(static_cast<std::uintptr_t>(0x900 + index));
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef deck;
};

std::optional<Content> LoadContent(const fs::path& root) {
    auto load = match::modload::ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return std::nullopt;
    const match::modload::SemanticValidator validator(
        (root / "contract" / "schemas").string());
    match::modload::ValidateLoadedMods(load, validator);
    if (!load.ok()) return std::nullopt;
    Content content;
    content.mods = std::move(load.mods);
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == kDeckId) content.deck = deck;
        }
    }
    if (content.deck.deck_id.empty()) return std::nullopt;
    content.deck.mods = kShippedMods;
    return content;
}

/** @brief One headless lobby; the clock outlives the engine that reads it. */
struct Lobby {
    FakeClock clock;
    std::unique_ptr<MatchSession> session;
    std::unique_ptr<HeuristicBotPolicy> policy;
    CountingBroadcaster broadcaster;
    int idle = 0;
    int steps = 0;
    bool finished = false;
};

double HeapKb();

std::unique_ptr<Lobby> CreateLobby(const Content& content, uint64_t seed,
                                   bool breakdown = false) {
    double mark = HeapKb();
    const auto stage = [&](const char* name) {
        if (!breakdown) return;
        const double now = HeapKb();
        std::fprintf(stderr, "stage %-22s +%.0f kb\n", name, now - mark);
        mark = now;
    };
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
    std::vector<LoadedMod> mods = content.mods;
    stage("copy mod vector");
    auto assembly = MatchAssembler::Assemble(mods, content.deck, options);
    stage("assemble");
    if (!assembly.ok()) return nullptr;

    auto lobby = std::make_unique<Lobby>();
    match::WindowConfig window;
    window.window_ms = 7000;
    window.mode = match::WindowMode::kFixed;
    auto engine = std::make_unique<MatchInstance>(std::move(assembly.assembly),
                                                  window, lobby->clock.Fn());
    stage("match instance");
    lobby->session = std::make_unique<MatchSession>(std::move(engine),
                                                    std::move(mods), sockets);
    stage("match session");
    lobby->session->BindViewer(kSpectator, SocketOf(kSeats.size()));
    lobby->policy = std::make_unique<HeuristicBotPolicy>(seed * 31 + 1);
    lobby->session->EmitMatchStart(lobby->broadcaster);
    stage("emit match start");
    lobby->session->BroadcastSnapshot(lobby->broadcaster);
    stage("first snapshot");
    return lobby;
}

bool BotsAct(Lobby& lobby) {
    MatchSession& session = *lobby.session;
    bool acted = false;
    const std::optional<json> pending = session.Engine().PendingInput();
    if (pending.has_value()) {
        const std::string target = match::view::ResolvePlayer(
            session.Engine(), pending->value("target", json()));
        if (!target.empty()) acted = BotStep(session, *lobby.policy, target);
    } else if (session.Engine().WindowOpen()) {
        const json open_window = session.Engine().ExportWindow();
        for (const json& responder : open_window.value("responders", json::array())) {
            if (!responder.is_string()) continue;
            acted = BotStep(session, *lobby.policy, responder.get<std::string>()) || acted;
        }
    } else {
        const std::string current = session.Engine().GetCurrentPlayerUsername();
        if (!current.empty()) acted = BotStep(session, *lobby.policy, current);
    }
    return acted;
}

/** @brief One bot action plus the tick, event emit and snapshot broadcast. */
void StepLobby(Lobby& lobby, int max_steps) {
    const bool acted = BotsAct(lobby);
    if (!acted) lobby.clock.now += kStalledStepMs;
    lobby.session->Tick();
    lobby.session->EmitEvents(lobby.broadcaster);
    lobby.session->BroadcastSnapshot(lobby.broadcaster);
    ++lobby.steps;
    lobby.idle = acted ? 0 : lobby.idle + 1;
    lobby.finished = lobby.session->Engine().IsMatchOver() || lobby.steps >= max_steps
                     || lobby.idle > kIdleLimit;
}

double HeapKb() { return static_cast<double>(mallinfo2().uordblks) / kBytesPerKb; }

void ReportMemory(const char* phase, long baseline_rss_kb, double baseline_heap_kb,
                  int lobbies) {
    const long rss_kb = RssKb();
    const double heap_kb = HeapKb();
    const double divisor = std::max(lobbies, 1);
    std::printf("mem phase=%s rss_kb=%ld heap_kb=%.0f rss_per_lobby_kb=%.0f heap_per_lobby_kb=%.0f\n",
                phase, rss_kb, heap_kb, static_cast<double>(rss_kb - baseline_rss_kb) / divisor,
                (heap_kb - baseline_heap_kb) / divisor);
}

double Percentile(std::vector<double>& samples, double fraction) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>(static_cast<double>(samples.size() - 1) * fraction)];
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = ParseOptions(argc, argv);
    kSeats.clear();
    for (int seat = 0; seat < options.seats; ++seat) kSeats.push_back("bot" + std::to_string(seat));
    std::ostringstream discarded_log;
    std::streambuf* original_log = std::cout.rdbuf(discarded_log.rdbuf());

    const std::optional<Content> content = LoadContent(options.root);
    if (!content) {
        std::cout.rdbuf(original_log);
        std::fprintf(stderr, "soak: mods or deck not found under %s\n", options.root.c_str());
        return 1;
    }
    malloc_trim(0);
    const long baseline_rss_kb = RssKb();
    const double baseline_heap_kb = HeapKb();

    std::vector<std::unique_ptr<Lobby>> lobbies;
    lobbies.reserve(static_cast<std::size_t>(options.lobbies));
    for (int index = 0; index < options.lobbies; ++index) {
        auto lobby = CreateLobby(*content, options.seed + static_cast<uint64_t>(index), options.breakdown && index == 0);
        if (!lobby) {
            std::cout.rdbuf(original_log);
            std::fprintf(stderr, "soak: assembly failed for lobby %d\n", index);
            return 1;
        }
        lobbies.push_back(std::move(lobby));
    }
    std::cout.rdbuf(original_log);
    std::printf("lobbies=%d seats=%d mods_loaded=%zu\n", options.lobbies, options.seats, content->mods.size());
    ReportMemory("created", baseline_rss_kb, baseline_heap_kb, options.lobbies);

    std::vector<double> step_ns;
    bool warm_reported = false;
    int round = 0;
    int active = options.lobbies;
    std::cout.rdbuf(discarded_log.rdbuf());
    while (active > 0) {
        for (auto& lobby : lobbies) {
            if (lobby->finished) continue;
            const auto started = std::chrono::steady_clock::now();
            StepLobby(*lobby, options.max_steps);
            const auto elapsed = std::chrono::steady_clock::now() - started;
            step_ns.push_back(std::chrono::duration<double, std::nano>(elapsed).count());
            if (lobby->finished) --active;
        }
        ++round;
        if (!warm_reported && round == kWarmSteps) {
            std::cout.rdbuf(original_log);
            ReportMemory("warm", baseline_rss_kb, baseline_heap_kb, options.lobbies);
            std::cout.rdbuf(discarded_log.rdbuf());
            warm_reported = true;
        }
    }
    std::cout.rdbuf(original_log);
    ReportMemory("finished", baseline_rss_kb, baseline_heap_kb, options.lobbies);

    std::size_t engine_events = 0;
    std::size_t engine_event_json_bytes = 0;
    for (const auto& lobby : lobbies) {
        for (const json& event : lobby->session->Engine().Events()) {
            ++engine_events;
            engine_event_json_bytes += event.dump().size();
        }
    }
    std::printf("engine events_per_lobby=%.0f json_kb_per_lobby=%.0f\n",
                static_cast<double>(engine_events) / std::max(options.lobbies, 1),
                static_cast<double>(engine_event_json_bytes) / kBytesPerKb / std::max(options.lobbies, 1));
    uint64_t total_bytes = 0;
    uint64_t total_frames = 0;
    long total_steps = 0;
    int matches_over = 0;
    for (const auto& lobby : lobbies) {
        total_bytes += lobby->broadcaster.bytes;
        total_frames += lobby->broadcaster.frames;
        total_steps += lobby->steps;
        if (lobby->session->Engine().IsMatchOver()) ++matches_over;
    }
    const double lobby_count = std::max(options.lobbies, 1);
    std::printf("play steps=%ld matches_over=%d step_median_us=%.1f step_p99_us=%.1f\n",
                total_steps, matches_over, Percentile(step_ns, 0.5) / 1000.0,
                Percentile(step_ns, 0.99) / 1000.0);
    std::printf("wire frames_per_lobby=%.0f kb_per_lobby=%.0f bytes_per_frame=%.0f\n",
                static_cast<double>(total_frames) / lobby_count,
                static_cast<double>(total_bytes) / kBytesPerKb / lobby_count,
                total_frames == 0 ? 0.0
                                  : static_cast<double>(total_bytes) / static_cast<double>(total_frames));

    lobbies.clear();
    malloc_trim(0);
    ReportMemory("freed", baseline_rss_kb, baseline_heap_kb, options.lobbies);
    return 0;
}
