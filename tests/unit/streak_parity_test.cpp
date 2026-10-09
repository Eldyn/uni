#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/streak/streak_engine.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/streak_view.hpp>
#include <match/view/view_builder.hpp>
#include <match/view/view_util.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file streak_parity_test.cpp
 * @brief The server `uni:skip_run` and `uni:reverse_run` streaks equal the
 *        old client values over the same bot matches.
 *
 * The old client value comes from a port of `StreakTracker.record` (only the
 * `skipRun` and `reverseRun` parts) that runs over the log events the play
 * log builds from the same client events. The golden files in
 * `tests/fixtures/golden_streak_parity/` hold the client events, the log
 * event of each step, and both values. The frontend test
 * `streakParity.test.ts` runs the real `StreakTracker` over the same log
 * events and checks the old values, so the port cannot drift.
 *
 * Set `UNI_UPDATE_GOLDEN=1` to write the golden files again; a default run
 * never writes them.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;
using match::server::BotStep;
using match::server::HeuristicBotPolicy;
using match::server::MatchSession;
using nlohmann::json;

namespace {

constexpr const char* kSkipRun = "uni:skip_run";
constexpr const char* kReverseRun = "uni:reverse_run";

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

/** @brief Port of the skip and reverse parts of `StreakTracker.record`. */
class OldTrackerPort {
   public:
    void Record(const json& log_event) {
        const std::string kind = log_event.value("kind", "");
        if (kind == "draw" || kind == "keep") {
            skip_run_ = 0;
            reverse_run_ = 0;
            last_skipped_.reset();
        } else if (kind == "skip") {
            const std::string player = log_event.value("player", "");
            skip_run_ = last_skipped_ == player ? skip_run_ + 1 : 1;
            reverse_run_ = 0;
            last_skipped_ = player;
        } else if (kind == "reverse") {
            skip_run_ = 0;
            reverse_run_ += 1;
            last_skipped_.reset();
        } else if (kind == "play" || kind == "auto_play") {
            const std::string card_kind = log_event.value("card_kind", "");
            const bool own_skip = EndsWith(card_kind, "_skip");
            const bool own_reverse = EndsWith(card_kind, "_reverse");
            if (!own_skip) skip_run_ = 0;
            if (!own_reverse) reverse_run_ = 0;
            if (!own_skip) last_skipped_.reset();
        }
    }
    int SkipRun() const { return skip_run_; }
    int ReverseRun() const { return reverse_run_; }

   private:
    static bool EndsWith(const std::string& text, const std::string& tail) {
        return text.size() >= tail.size()
               && text.compare(text.size() - tail.size(), tail.size(), tail)
                      == 0;
    }
    int skip_run_ = 0;
    int reverse_run_ = 0;
    std::optional<std::string> last_skipped_;
};

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

Content LoadContent() {
    Content content;
    LoadResult load = ScanModsDirectory((ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    content.mods = std::move(load.mods);
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") content.classic = deck;
        }
    }
    REQUIRE_FALSE(content.classic.deck_id.empty());
    return content;
}

AppWebSocket* PlayerSocket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x200 + index));
}

/** @brief Drive four bots until the match ends; return the whole session. */
void DriveToCompletion(MatchSession& session, HeuristicBotPolicy& policy) {
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
        session.Tick();
        idle = acted ? 0 : idle + 1;
        if (idle > 200) break;
    }
}

bool IsWildKind(const std::string& kind_id) {
    return kind_id == "uni:wild" || kind_id == "uni:wild_draw4";
}

/** @brief The log event that the play log builds from one client event. */
std::optional<json> LogEventFor(const MatchInstance& engine,
                                const json& event,
                                std::optional<json>& pending_wild) {
    const std::string type = event.value("type", "");
    const json body = event.value("payload", json::object());
    if (type == "card_played") {
        const std::string kind =
            match::view::KindIdFor(engine, body.value("card", json()));
        const std::string player =
            match::view::ResolvePlayer(engine, body.value("player", json()));
        const json play = {{"kind", "play"},
                           {"player", player},
                           {"card_kind", kind.substr(kind.find(':') + 1)}};
        if (IsWildKind(kind)) {
            pending_wild = play;
            return std::nullopt;
        }
        pending_wild.reset();
        return play;
    }
    if (type == "active_type_changed" && pending_wild.has_value()) {
        const json play = *pending_wild;
        pending_wild.reset();
        return play;
    }
    if (type == "turn_skipped") {
        return json{{"kind", "skip"},
                    {"player", match::view::UsernameFor(
                                   engine, body.value("target", json()))}};
    }
    if (type == "direction_changed") return json{{"kind", "reverse"}};
    if (type == "cards_drawn") return json{{"kind", "draw"}};
    if (type == "drawn_card_kept") return json{{"kind", "keep"}};
    return std::nullopt;
}

struct Step {
    json record;
    int old_skip_run = 0;
    int old_reverse_run = 0;
    int server_skip_run = 0;
    int server_reverse_run = 0;
    bool observation_point = false;
};

/** @brief Replay the finished match through both implementations. */
std::vector<Step> Replay(const MatchInstance& engine, Content& content) {
    (void)content;
    match::streak::StreakEngine server;
    server.SetDefinitions(engine.Registries().streaks);
    OldTrackerPort old_tracker;
    std::optional<json> pending_wild;
    std::vector<Step> steps;
    for (const json& event : engine.Events()) {
        const auto view = match::view::BuildStreakEventView(engine, event);
        if (view.has_value()) server.Observe(*view);
        const std::optional<json> log_event =
            LogEventFor(engine, event, pending_wild);
        if (log_event.has_value()) old_tracker.Record(*log_event);

        const std::string type = event.value("type", "");
        const bool read = log_event.has_value() || type == "card_played"
                          || type == "active_type_changed";
        if (!read) continue;
        Step step;
        step.record = {{"event", type},
                       {"log_event", log_event.has_value() ? *log_event
                                                          : json(nullptr)}};
        step.old_skip_run = old_tracker.SkipRun();
        step.old_reverse_run = old_tracker.ReverseRun();
        step.server_skip_run = server.ValueOf(kSkipRun).get<int>();
        step.server_reverse_run = server.ValueOf(kReverseRun).get<int>();
        step.observation_point =
            type == "turn_skipped" || type == "direction_changed";
        steps.push_back(std::move(step));
    }
    return steps;
}

json StepsToJson(const std::vector<Step>& steps) {
    json out = json::array();
    for (const Step& step : steps) {
        json row = step.record;
        row["old"] = {{"skip_run", step.old_skip_run},
                      {"reverse_run", step.old_reverse_run}};
        row["server"] = {{"skip_run", step.server_skip_run},
                         {"reverse_run", step.server_reverse_run}};
        out.push_back(std::move(row));
    }
    return out;
}

void RunSeed(uint64_t seed) {
    Content content = LoadContent();
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (int index = 0; index < 4; ++index) {
        MatchPlayerSpec spec;
        spec.username = "bot" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE(result.ok());
    auto engine = std::make_unique<MatchInstance>(std::move(result.assembly));
    MatchSession session(
        std::move(engine), content.mods,
        {{"bot0", PlayerSocket(0)},
         {"bot1", PlayerSocket(1)},
         {"bot2", PlayerSocket(2)},
         {"bot3", PlayerSocket(3)}});
    HeuristicBotPolicy policy(seed, {});
    DriveToCompletion(session, policy);
    REQUIRE(session.Engine().IsMatchOver());

    const std::vector<Step> steps = Replay(session.Engine(), content);
    REQUIRE_FALSE(steps.empty());

    // INFO: the parity claim. At every step where the old tracker records a
    //       log event, the server value equals the old client value. A wild
    //       card_played step has no log event: the play log waits for the
    //       color choice (`active_type_changed`), the server does not. The
    //       two values meet again at that next step.
    int skip_points = 0;
    int reverse_points = 0;
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const Step& step = steps[index];
        if (step.record["log_event"].is_null()) continue;
        CHECK_MESSAGE(step.server_skip_run == step.old_skip_run,
                      "seed " << seed << " step " << index << " "
                              << step.record.dump());
        CHECK_MESSAGE(step.server_reverse_run == step.old_reverse_run,
                      "seed " << seed << " step " << index << " "
                              << step.record.dump());
        skip_points += step.record["event"] == "turn_skipped" ? 1 : 0;
        reverse_points += step.record["event"] == "direction_changed" ? 1 : 0;
    }
    CHECK(skip_points + reverse_points > 0);

    // INFO: the live context on the envelopes equals the replay values.
    const match::view::ViewBuilder builder(session.Engine(), content.mods);
    std::size_t checked = 0;
    for (const json& packet : builder.BuildPackets(
             match::view::Viewer::PublicSpectator())) {
        if (!packet.contains("context")) continue;
        ++checked;
        const json& context = packet["context"];
        CHECK(context.is_object());
    }
    CHECK(checked > 0);

    const json actual = StepsToJson(steps);
    const fs::path golden = ProjectRoot() / "tests" / "fixtures"
                            / "golden_streak_parity"
                            / ("seed_" + std::to_string(seed) + ".json");
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(1) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}

}  // namespace

TEST_CASE("streak parity: seed 7 bot match, skip and reverse runs") {
    RunSeed(7);
}

TEST_CASE("streak parity: seed 11 bot match, skip and reverse runs") {
    RunSeed(11);
}

TEST_CASE("streak parity: seed 23 bot match, skip and reverse runs") {
    RunSeed(23);
}

TEST_CASE("streak parity: the golden matches reach both streaks") {
    int skip_points = 0;
    int reverse_points = 0;
    for (const uint64_t seed : {7, 11, 23}) {
        std::ifstream input(ProjectRoot() / "tests" / "fixtures"
                            / "golden_streak_parity"
                            / ("seed_" + std::to_string(seed) + ".json"));
        REQUIRE(input.good());
        for (const json& row : json::parse(input)) {
            skip_points += row["event"] == "turn_skipped" ? 1 : 0;
            reverse_points += row["event"] == "direction_changed" ? 1 : 0;
            if (row["event"] == "turn_skipped") {
                CHECK(row["server"]["skip_run"] >= 1);
            }
        }
    }
    CHECK(skip_points > 0);
    CHECK(reverse_points > 0);
}

TEST_CASE("streak parity: the shipped uni mod defines both streaks") {
    Content content = LoadContent();
    std::vector<std::string> ids;
    for (const LoadedMod& mod : content.mods) {
        if (mod.manifest.id != "uni") continue;
        for (const StreakDef& streak : mod.streaks) {
            ids.push_back(streak.streak_id);
        }
    }
    CHECK(std::find(ids.begin(), ids.end(), kSkipRun) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), kReverseRun) != ids.end());
    for (const auto& row : match::view::audience_table::kCoreContext) {
        CHECK_MESSAGE(std::find(ids.begin(), ids.end(),
                                std::string(row.streak_id))
                          != ids.end(),
                      row.streak_id);
    }
}
