#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/streak/derived_engine.hpp>
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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file streak_parity_test.cpp
 * @brief The server streaks equal the old client counters over the same bot
 *        matches.
 *
 * The test ports the way that the old play log turned client events into log
 * events (`playLogEmitter.ts`). The golden files
 * `tests/fixtures/golden_streak_parity/seed_<n>.json` hold, for each client
 * event, the log events and the server values. The files
 * `expected_seed_<n>.json` hold the values of the old `StreakTracker` for the
 * same log events. The frontend test `streakParity.test.ts` wrote them from
 * the real tracker before the tracker was deleted. They are stored expected
 * output now: nothing rewrites them.
 *
 * Set `UNI_UPDATE_GOLDEN=1` to write `seed_<n>.json` again; a default run
 * never writes a golden file.
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

/** @brief The counters that the parity test compares, by golden key. */
const std::vector<std::pair<std::string, std::string>> kParityStreaks = {
    {"skip_run", kSkipRun},
    {"reverse_run", kReverseRun},
    {"draw_run", "uni:draw_run"},
    {"total_draws", "uni:total_draws"},
    {"wild_run", "uni:wild_run"},
    {"last_wild_color", "uni:last_wild_color"},
};

const std::vector<uint64_t> kExtraSeeds = {1, 13, 17, 19, 27, 30, 42};

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

fs::path GoldenDirectory() {
    return ProjectRoot() / "tests" / "fixtures" / "golden_streak_parity";
}

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

/** @brief The hand sizes that the client sees after one bot action. */
using HandSizes = std::map<std::string, int64_t>;

HandSizes CurrentHandSizes(const MatchInstance& engine,
                           const std::string& prefix = "bot",
                           int seat_count = 4) {
    HandSizes sizes;
    for (int index = 0; index < seat_count; ++index) {
        const std::string name = prefix + std::to_string(index);
        const std::optional<ecs::Entity> player = engine.FindPlayer(name);
        const ecs::Hand* hand =
            player.has_value() ? engine.Store().Get<ecs::Hand>(*player)
                               : nullptr;
        if (hand != nullptr) {
            sizes[name] = static_cast<int64_t>(hand->cards.size());
        }
    }
    return sizes;
}

/**
 * @brief Drive four bots until the match ends.
 *
 * @param step_hands Receives, for each client event, the hand sizes after the
 *                   bot action that made it. The old play log read the near
 *                   win from the snapshot of that moment.
 */
void DriveToCompletion(MatchSession& session, HeuristicBotPolicy& policy,
                       std::vector<HandSizes>& step_hands) {
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
        const HandSizes hands = CurrentHandSizes(session.Engine());
        while (step_hands.size() < session.Engine().Events().size()) {
            step_hands.push_back(hands);
        }
        if (idle > 200) break;
    }
}

bool EndsWith(const std::string& text, const std::string& tail) {
    return text.size() >= tail.size()
           && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

bool IsWildKind(const std::string& kind_id) {
    return kind_id == "uni:wild" || kind_id == "uni:wild_draw4";
}

/**
 * @brief Port of the way the old play log made log events.
 *
 * INFO: the port follows the live path of `playLogEmitter.ts` with two
 *       settings. A penalty draw comes from a forced `cards_drawn` event
 *       (no `held` field), not from the stack status line, because the new
 *       client reads events only. The near win reads the hand size after the
 *       bot action (the old client read it from the snapshot).
 */
class LogEventPort {
   public:
    explicit LogEventPort(const MatchInstance& engine) : engine_(engine) {}

    std::vector<json> Convert(const json& event, const HandSizes& hands) {
        std::vector<json> out;
        const std::string type = event.value("type", "");
        const json body = event.value("payload", json::object());
        const auto view = match::view::BuildStreakEventView(engine_, event);
        const json fields = view.has_value() ? view->payload : json::object();

        if (type == "card_played") {
            const std::string kind =
                match::view::KindIdFor(engine_, body.value("card", json()));
            const std::string player = match::view::ResolvePlayer(
                engine_, body.value("player", json()));
            if (IsWildKind(kind)) {
                pending_wild_ = json{{"player", player}, {"card_kind", kind}};
                return out;
            }
            pending_wild_.reset();
            AppendPlay(out, player, kind, hands);
        } else if (type == "active_type_changed") {
            const std::string color = fields.value("color", "");
            if (!pending_wild_.has_value() || color.empty()
                || color == "white") {
                return out;
            }
            const json wild = *pending_wild_;
            pending_wild_.reset();
            AppendPlay(out, wild["player"], wild["card_kind"], hands);
            out.push_back(json{{"kind", "wild"},
                               {"player", wild["player"]},
                               {"color", color}});
        } else if (type == "turn_skipped") {
            json skip = {{"kind", "skip"},
                         {"player", fields.value("target", "")}};
            if (view.has_value() && !view->actor.empty()) {
                skip["actor"] = view->actor;
            }
            out.push_back(std::move(skip));
        } else if (type == "direction_changed") {
            out.push_back(json{{"kind", "reverse"}});
        } else if (type == "cards_drawn") {
            const std::string player = fields.value("target", "");
            // INFO: a forced draw has no `held` field. The old client logged
            //       it as a penalty draw of the victim (a +2 or +4 stack).
            const bool penalty = !fields.contains("held");
            json draw = {{"kind", "draw"},
                         {"player", player},
                         {"count", fields.value("count", 0)},
                         {"penalty", penalty}};
            if (penalty) draw["victim"] = player;
            out.push_back(std::move(draw));
        } else if (type == "drawn_card_kept") {
            out.push_back(json{{"kind", "keep"},
                               {"player", fields.value("target", "")}});
        } else if (type == "match_ended") {
            out.push_back(json{{"kind", "win"},
                               {"player", fields.value("target", "")}});
        }
        return out;
    }

   private:
    static void AppendPlay(std::vector<json>& out, const std::string& player,
                           const std::string& kind, const HandSizes& hands) {
        out.push_back(json{{"kind", "play"},
                           {"player", player},
                           {"card_kind", kind}});
        const bool draws_cards =
            EndsWith(kind, "_draw2") || EndsWith(kind, "_draw4");
        const auto size = hands.find(player);
        if (!draws_cards && size != hands.end() && size->second == 1) {
            out.push_back(json{{"kind", "near_win"}, {"player", player}});
        }
    }

    const MatchInstance& engine_;
    std::optional<json> pending_wild_;
};

struct Step {
    std::string event;
    std::vector<json> log_events;
    json server = json::object();
};

/** @brief The window as the golden file stores it. */
json WindowJson(const match::streak::WindowState& window) {
    return json{{"target", window.seat.empty() ? json() : json(window.seat)},
                {"open", window.open}};
}

/** @brief Replay the finished match through the port and the server. */
std::vector<Step> Replay(const MatchInstance& engine,
                         const std::vector<HandSizes>& step_hands,
                         json& replayed_derived, json& live_derived) {
    match::streak::StreakEngine server;
    server.SetDefinitions(engine.Registries().streaks);
    match::streak::DerivedEngine derived;
    derived.SetDefinitions(engine.Registries().derived);
    LogEventPort port(engine);
    std::vector<Step> steps;
    std::size_t index = 0;
    for (const json& event : engine.Events()) {
        const HandSizes& hands = step_hands[index++];
        if (match::view::IsDerivedEvent(event.value("type", ""))) {
            live_derived.push_back(
                json{{"id", event["type"]},
                     {"target", event["payload"]["target"]}});
        }
        auto view = match::view::BuildStreakEventView(engine, event);
        // INFO: the replay runs after the match, so the hand of the actor is
        //       not the hand at the time of the event. The hand sizes that the
        //       old client saw (after the bot action) take its place. The test
        //       "live derived events equal the replay" checks that they match
        //       the hand size that the live engine used.
        if (view.has_value() && !view->actor.empty()) {
            const auto size = hands.find(view->actor);
            if (size != hands.end()) view->state.hand_size = size->second;
        }
        const match::streak::WindowState before =
            derived.WindowFor("near_win_entered");
        std::vector<match::streak::Derivation> derivations;
        if (view.has_value()) {
            server.Observe(*view);
            derivations = derived.Evaluate(
                *view, [&server](int64_t turn, const std::string&) {
                    return server.TryCharge(turn);
                });
        }
        Step step;
        step.event = event.value("type", "");
        step.log_events = port.Convert(event, hands);
        if (view.has_value()) {
            for (const auto& [key, streak_id] : kParityStreaks) {
                step.server[key] = server.ValueOf(streak_id);
            }
        }
        if (view.has_value()) {
            bool blocked = false;
            for (const auto& derivation : derivations) {
                blocked = blocked || derivation.event_id == "blocked";
                replayed_derived.push_back(
                    json{{"id", derivation.event_id},
                         {"target", derivation.seat}});
            }
            step.server["near_win"] = WindowJson(
                derived.WindowFor("near_win_entered"));
            step.server["near_win_before"] = WindowJson(before);
            step.server["blocked"] = blocked;
        }
        const bool read = !step.log_events.empty() || view.has_value();
        if (!read) continue;
        steps.push_back(std::move(step));
    }
    return steps;
}

json StepsToJson(const std::vector<Step>& steps) {
    json out = json::array();
    for (const Step& step : steps) {
        out.push_back(json{{"event", step.event},
                           {"log_events", step.log_events},
                           {"server", step.server}});
    }
    return out;
}

std::optional<json> ReadJson(const fs::path& path) {
    std::ifstream input(path);
    if (!input.good()) return std::nullopt;
    return json::parse(input);
}

fs::path GoldenPath(uint64_t seed) {
    return GoldenDirectory() / ("seed_" + std::to_string(seed) + ".json");
}

fs::path ExpectedPath(uint64_t seed) {
    return GoldenDirectory()
           / ("expected_seed_" + std::to_string(seed) + ".json");
}

struct Played {
    std::vector<Step> steps;
    json replayed_derived = json::array();
    json live_derived = json::array();
};

Played PlayMatch(uint64_t seed, Content& content) {
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
    std::vector<HandSizes> step_hands;
    DriveToCompletion(session, policy, step_hands);
    REQUIRE(session.Engine().IsMatchOver());
    while (step_hands.size() < session.Engine().Events().size()) {
        step_hands.push_back(CurrentHandSizes(session.Engine()));
    }
    Played played;
    played.steps = Replay(session.Engine(), step_hands, played.replayed_derived,
                          played.live_derived);
    return played;
}

/**
 * @brief The server value that answers one old counter at one step.
 *
 * `draw_run` is a count for each seat on the server; the old counter is the
 * count of the seat that drew, and it reads 0 at a step that is not a draw.
 * The two wild colors come from one `last_value` streak (`last`, `previous`).
 */
json ServerAnswer(const json& server, const std::string& counter,
                  const std::string& drawer) {
    if (counter == "draw_run") {
        return server["draw_run"].value(drawer, 0);
    }
    if (counter == "last_wild_color") return server[counter]["last"];
    if (counter == "prev_wild_color") return server["last_wild_color"]["previous"];
    return server[counter];
}

/**
 * @brief Compare the server values of a step with the tracker values.
 *
 * Both sides are read at a step that has a log event. A wild `card_played`
 * has no log event: the play log waits for the color choice, the server does
 * not. The two values meet again at that next step.
 */
void CheckAgainstExpected(uint64_t seed, const json& golden,
                          const json& expected) {
    REQUIRE(golden.size() == expected.size());
    for (std::size_t index = 0; index < golden.size(); ++index) {
        const json& row = golden[index];
        if (row["log_events"].empty()) continue;
        const json& old_values = expected[index]["old"];
        REQUIRE_MESSAGE(old_values.is_object(),
                        "seed " << seed << " step " << index);
        std::string drawer;
        for (const json& log_event : row["log_events"]) {
            if (log_event["kind"] == "draw") drawer = log_event["player"];
        }
        const std::string where = "seed " + std::to_string(seed) + " step "
                                  + std::to_string(index) + " " + row.dump();
        for (const char* counter :
             {"skip_run", "reverse_run", "total_draws", "wild_run",
              "last_wild_color", "prev_wild_color"}) {
            CHECK_MESSAGE(ServerAnswer(row["server"], counter, "")
                              == old_values[counter],
                          counter << " " << where);
        }
        if (!drawer.empty()) {
            CHECK_MESSAGE(ServerAnswer(row["server"], "draw_run", drawer)
                              == old_values["draw_run"],
                          "draw_run " << where);
        }
        // INFO: the old tracker reports the window with its entering values
        //       at a blocking event, and the window after the event at every
        //       other event. The server blocks when `blocked` is true.
        const json& window = row["server"]["blocked"] == true
                                 ? row["server"]["near_win_before"]
                                 : row["server"]["near_win"];
        CHECK_MESSAGE(window["open"] == old_values["near_win_open"],
                      "near_win_open " << where);
        if (window["open"] == true) {
            CHECK_MESSAGE(window["target"] == old_values["near_win_target"],
                          "near_win_target " << where);
        }
    }
}

void RunSeed(uint64_t seed) {
    Content content = LoadContent();
    const Played played = PlayMatch(seed, content);
    const json actual = StepsToJson(played.steps);
    REQUIRE_FALSE(actual.empty());
    // INFO: the derived events of the live engine (hand size at event time)
    //       equal the derived events of the replay (hand size of the old
    //       client).
    CHECK_MESSAGE(played.live_derived == played.replayed_derived,
                  "seed " << seed);

    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(GoldenDirectory());
        std::ofstream(GoldenPath(seed)) << actual.dump(1) << "\n";
    }
    const std::optional<json> golden = ReadJson(GoldenPath(seed));
    REQUIRE_MESSAGE(golden.has_value(),
                    "missing golden file " << GoldenPath(seed));
    CHECK(*golden == actual);

    const std::optional<json> expected = ReadJson(ExpectedPath(seed));
    CHECK_MESSAGE(expected.has_value(),
                  "missing expected file " << ExpectedPath(seed));
    if (expected.has_value()) CheckAgainstExpected(seed, actual, *expected);
}

// --- scenarios that the bot matches seldom reach -------------------------

/** @brief A hand-driven table of three seats; red is active. */
class Scenario {
   public:
    explicit Scenario(int seat_count = 3)
        : content_(LoadContent()), seat_count_(seat_count) {
        MatchAssemblyOptions options;
        options.starting_cards = 7;
        options.seed = 42;
        for (int index = 0; index < seat_count; ++index) {
            MatchPlayerSpec spec;
            spec.username = "player" + std::to_string(index);
            options.players.push_back(spec);
        }
        AssemblyResult result =
            MatchAssembler::Assemble(content_.mods, content_.classic, options);
        REQUIRE(result.ok());
        engine_ = std::make_unique<MatchInstance>(std::move(result.assembly));
        SetRed();
    }

    MatchInstance& Engine() { return *engine_; }

    ecs::Entity Card(const std::string& kind, std::size_t copy = 0) {
        std::vector<ecs::Entity> found;
        for (ecs::Entity card : engine_->Registries().cards) {
            const ecs::CardIdentity* identity =
                engine_->Store().Get<ecs::CardIdentity>(card);
            if (identity != nullptr && identity->kind_id == kind) {
                found.push_back(card);
            }
        }
        REQUIRE(found.size() > copy);
        return found[copy];
    }

    void Hand(const std::string& username,
              const std::vector<ecs::Entity>& cards) {
        const ecs::Entity player = *engine_->FindPlayer(username);
        const std::vector<ecs::Entity> existing =
            engine_->Store().Get<ecs::Hand>(player)->cards;
        for (ecs::Entity card : existing) {
            match::ops::MoveCardToZone(
                engine_->Store(), card,
                ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
        }
        for (ecs::Entity card : cards) {
            match::ops::MoveCardToZone(
                engine_->Store(), card,
                ecs::ZoneRef{ecs::ZoneKind::kHand, player});
        }
    }

    void SetRed() {
        engine_->Store()
            .Get<ecs::ActiveTypeReq>(engine_->Registries().match)
            ->type = "red";
    }

    void Play(const std::string& username, ecs::Entity card) {
        SetRed();
        REQUIRE(engine_->PlayCard(username, card));
        Note();
    }

    void Draw(const std::string& username) {
        REQUIRE(engine_->DrawCard(username));
        if (engine_->PendingPlayDrawnState().has_value()) {
            REQUIRE(engine_->KeepDrawn(username));
        }
        Note();
    }

    /** @brief Close the step: store the hands after the action. */
    void Note() {
        const HandSizes hands =
            CurrentHandSizes(*engine_, "player", seat_count_);
        while (hands_.size() < engine_->Events().size()) {
            hands_.push_back(hands);
        }
    }

    Played Result() {
        Played played;
        played.steps = Replay(*engine_, hands_, played.replayed_derived,
                              played.live_derived);
        return played;
    }

   private:
    Content content_;
    int seat_count_;
    std::unique_ptr<MatchInstance> engine_;
    std::vector<HandSizes> hands_;
};

/** @brief player0 near win; player2 skips player0. */
Played SkipBlockScenario() {
    Scenario table;
    table.Hand("player0", {table.Card("uni:red_5"), table.Card("uni:red_7")});
    table.Hand("player1", {table.Card("uni:red_8"), table.Card("uni:red_9"),
                           table.Card("uni:red_1")});
    table.Hand("player2", {table.Card("uni:red_skip"), table.Card("uni:red_6"),
                           table.Card("uni:red_2")});
    table.Play("player0", table.Card("uni:red_5"));
    table.Play("player1", table.Card("uni:red_8"));
    table.Play("player2", table.Card("uni:red_skip"));
    return table.Result();
}

/** @brief player0 near win; player2 hits player0 with a plus two. */
Played PenaltyBlockScenario() {
    Scenario table;
    table.Hand("player0", {table.Card("uni:red_5"), table.Card("uni:red_7")});
    table.Hand("player1", {table.Card("uni:red_8"), table.Card("uni:red_9"),
                           table.Card("uni:red_1")});
    table.Hand("player2", {table.Card("uni:red_draw2"),
                           table.Card("uni:red_6"), table.Card("uni:red_2")});
    table.Play("player0", table.Card("uni:red_5"));
    table.Play("player1", table.Card("uni:red_8"));
    table.Play("player2", table.Card("uni:red_draw2"));
    return table.Result();
}

/** @brief player0 near win; player0 draws before anybody blocks. */
Played EscapeScenario() {
    Scenario table;
    table.Hand("player0", {table.Card("uni:red_5"), table.Card("uni:red_7")});
    table.Hand("player1", {table.Card("uni:red_8"), table.Card("uni:red_9"),
                           table.Card("uni:red_1")});
    table.Hand("player2", {table.Card("uni:red_skip"), table.Card("uni:red_6"),
                           table.Card("uni:red_2")});
    table.Play("player0", table.Card("uni:red_5"));
    table.Play("player1", table.Card("uni:red_8"));
    table.Play("player2", table.Card("uni:red_6"));
    table.Draw("player0");
    table.Play("player1", table.Card("uni:red_9"));
    table.Play("player2", table.Card("uni:red_skip"));
    return table.Result();
}

/**
 * @brief Two seats: a skip always hits the other seat, so player0 skips
 *        player1 two times in a row (a skip run of 2), then a plain card
 *        ends the run.
 */
Played SkipRepeatScenario() {
    Scenario table(2);
    table.Hand("player0",
               {table.Card("uni:red_skip", 0), table.Card("uni:red_skip", 1),
                table.Card("uni:red_5"), table.Card("uni:red_7")});
    table.Hand("player1", {table.Card("uni:red_8"), table.Card("uni:red_9"),
                           table.Card("uni:red_1")});
    table.Play("player0", table.Card("uni:red_skip", 0));
    table.Play("player0", table.Card("uni:red_skip", 1));
    table.Play("player0", table.Card("uni:red_5"));
    return table.Result();
}

/** @brief A near win that a new near win replaces. */
Played ReplaceScenario() {
    Scenario table;
    table.Hand("player0", {table.Card("uni:red_5"), table.Card("uni:red_7")});
    table.Hand("player1", {table.Card("uni:red_8"), table.Card("uni:red_9")});
    table.Hand("player2", {table.Card("uni:red_6"), table.Card("uni:red_2"),
                           table.Card("uni:red_3")});
    table.Play("player0", table.Card("uni:red_5"));
    table.Play("player1", table.Card("uni:red_8"));
    table.Play("player2", table.Card("uni:red_6"));
    return table.Result();
}

void RunScenario(const std::string& name, const Played& played) {
    const json actual = StepsToJson(played.steps);
    REQUIRE_FALSE(actual.empty());
    CHECK_MESSAGE(played.live_derived == played.replayed_derived, name);
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(GoldenDirectory());
        std::ofstream(GoldenDirectory() / (name + ".json"))
            << actual.dump(1) << "\n";
    }
    const std::optional<json> golden =
        ReadJson(GoldenDirectory() / (name + ".json"));
    REQUIRE_MESSAGE(golden.has_value(), "missing golden file " << name);
    CHECK(*golden == actual);
    const std::optional<json> expected =
        ReadJson(GoldenDirectory() / ("expected_" + name + ".json"));
    CHECK_MESSAGE(expected.has_value(), "missing expected file " << name);
    if (expected.has_value()) CheckAgainstExpected(0, actual, *expected);
}

}  // namespace

TEST_CASE("streak parity: seed 7 bot match, all counters") { RunSeed(7); }

TEST_CASE("streak parity: seed 11 bot match, all counters") { RunSeed(11); }

TEST_CASE("streak parity: seed 23 bot match, all counters") { RunSeed(23); }

// INFO: the three seeds above reach no wild run of 2 and no reverse run of 3.
//       These extra seeds do (found by a scan of seeds 1 to 80).
TEST_CASE("streak parity: extra seeds reach long wild and reverse runs") {
    for (const uint64_t seed : kExtraSeeds) {
        RunSeed(seed);
    }
}

TEST_CASE("streak parity: the golden matches reach skip and reverse") {
    int skip_points = 0;
    int reverse_points = 0;
    for (const uint64_t seed : {7, 11, 23}) {
        const std::optional<json> golden = ReadJson(GoldenPath(seed));
        REQUIRE(golden.has_value());
        for (const json& row : *golden) {
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

TEST_CASE("streak parity: a default run never rewrites a golden file") {
    const fs::path path = GoldenPath(7);
    std::error_code error;
    const auto before = fs::last_write_time(path, error);
    Content content = LoadContent();
    (void)PlayMatch(7, content).steps;
    CHECK(fs::last_write_time(path, error) == before);
}

TEST_CASE("streak parity: the shipped uni mod defines the core streaks") {
    Content content = LoadContent();
    std::vector<std::string> ids;
    for (const LoadedMod& mod : content.mods) {
        if (mod.manifest.id != "uni") continue;
        for (const StreakDef& streak : mod.streaks) {
            ids.push_back(streak.streak_id);
        }
    }
    for (const auto& [key, streak_id] : kParityStreaks) {
        CHECK_MESSAGE(std::find(ids.begin(), ids.end(), streak_id) != ids.end(),
                      streak_id);
    }
    for (const auto& row : match::view::audience_table::kCoreContext) {
        CHECK_MESSAGE(std::find(ids.begin(), ids.end(),
                                std::string(row.streak_id))
                          != ids.end(),
                      row.streak_id);
    }
}

TEST_CASE("streak parity: scenario, a skip blocks the near-win seat") {
    const Played played = SkipBlockScenario();
    RunScenario("scenario_skip_block", played);
    CHECK(played.live_derived.size() == 2);
    CHECK(played.live_derived[1]["id"] == "blocked");
}

TEST_CASE("streak parity: scenario, a plus two blocks the near-win seat") {
    const Played played = PenaltyBlockScenario();
    RunScenario("scenario_penalty_block", played);
    CHECK(played.live_derived.size() == 2);
    CHECK(played.live_derived[1]["id"] == "blocked");
}

TEST_CASE("streak parity: scenario, the target escapes by drawing") {
    const Played played = EscapeScenario();
    RunScenario("scenario_escape", played);
    for (const json& derived : played.live_derived) {
        CHECK(derived["id"] != "blocked");
    }
    CHECK(played.live_derived.size() >= 1);
}

TEST_CASE("streak parity: scenario, a new near win replaces the old one") {
    const Played played = ReplaceScenario();
    RunScenario("scenario_replace", played);
    CHECK(played.live_derived.size() == 2);
    CHECK(played.live_derived[0]["target"] == "player0");
    CHECK(played.live_derived[1]["target"] == "player1");
}

TEST_CASE("streak parity: scenario, the same seat is skipped two times") {
    const Played played = SkipRepeatScenario();
    RunScenario("scenario_skip_repeat", played);
    int highest = 0;
    const json actual = StepsToJson(played.steps);
    for (const json& row : actual) {
        highest = std::max<int>(highest, row["server"].value("skip_run", 0));
    }
    CHECK(highest == 2);
}
