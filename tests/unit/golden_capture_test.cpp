#include <doctest/doctest.h>
#include <common/lobby.hpp>
#include <common/match/card_types.hpp>
#include <common/match/effect.hpp>
#include <match/match_instance.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

/**
 * @file golden_capture_test.cpp
 * @brief Captures deterministic "golden transcripts" from the legacy
 *        match::MatchInstance engine before the controller removes it.
 *
 * @warning TODO: this harness is deleted or replaced at the engine
 *          swap. It exists only to freeze the OLD engine's scripted
 *          outcomes so the data-driven engine can be compared against them.
 *
 * Each scenario reloads a hand-authored saved_state JSON (no shuffle/deal)
 * through the reload constructor, then runs a scripted sequence of public
 * API calls. After every scripted action the full ExportState() is recorded
 * as an ordered step. All scenarios are byte-deterministic: ExportState()
 * carries no wall-clock field (unlike SerializeBaseState).
 *
 * draw_stacking scenarios are marked may_differ=true: rebuilds
 * draw stacking on response windows, so their transcripts are expected to
 * diverge by design.
 */

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using namespace match;

/**
 * @brief Builds a compact card JSON value (type/value/id packed u32).
 */
json card(Type type, Value value, uint16_t id) {
    return MakeCard(type, value, id);
}

/**
 * @brief Builds one entry of the saved_state "players" array.
 */
json player_json(const std::string& username, json hand, bool is_bot = false) {
    return json{
        {"username", username},
        {"hand", std::move(hand)},
        {"is_bot", is_bot}
    };
}

/**
 * @brief Builds a hand-authored kPlaying saved_state for the reload ctor.
 */
json build_state(json players, json draw_pile, json discard_pile,
                 int current_player_index, int play_direction,
                 json rules = json::array(), int active_type = 0,
                 int pending_draws = 0) {
    json state;
    state["rules"] = std::move(rules);
    state["status"] = 1;  // MatchStatus::kPlaying
    state["active_type"] = active_type;
    state["current_player_index"] = current_player_index;
    state["play_direction"] = play_direction;
    state["pending_draws"] = pending_draws;
    state["pending_player"] = "";
    state["discard_pile"] = std::move(discard_pile);
    state["draw_pile"] = std::move(draw_pile);
    state["players"] = std::move(players);
    return state;
}

/**
 * @brief Lobby settings carrying the given rule mods.
 */
LobbySettings settings_with_mods(const std::vector<std::string>& mods) {
    LobbySettings settings;
    settings.starting_cards = 7;
    settings.turn_time_limit_ms = 15000;
    settings.active_mods = mods;
    return settings;
}

/**
 * @brief Accumulates the ordered action/state steps of one scenario.
 */
class Transcript {
public:
    Transcript(MatchInstance& match, std::string scenario, bool may_differ)
        : match_(match), scenario_(std::move(scenario)),
          may_differ_(may_differ) {}

    void record(const std::string& action) {
        steps_.push_back({{"action", action}, {"state", match_.ExportState()}});
    }

    json finish() const {
        json out;
        out["scenario"] = scenario_;
        out["engine"] = "old";
        out["may_differ"] = may_differ_;
        out["steps"] = steps_;
        return out;
    }

private:
    MatchInstance& match_;
    std::string scenario_;
    bool may_differ_;
    json steps_ = json::array();
};

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

/**
 * @brief Standard two-player baseline: draw, legal play, turn advance.
 */
json scenario_standard_baseline() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kRed, Value::k3, 1),
                card(Type::kBlue, Value::k5, 2)})),
            player_json("Bob", json::array({
                card(Type::kRed, Value::k2, 4),
                card(Type::kGreen, Value::k9, 3)}))
        }),
        json::array({card(Type::kGreen, Value::k4, 201),
                     card(Type::kBlue, Value::k3, 200)}),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1);

    MatchInstance m(saved, settings_with_mods({}));
    Transcript t(m, "standard_baseline", false);

    t.record("load");

    REQUIRE(m.DrawCard("Alice"));
    m.Tick();
    t.record("draw:Alice");

    REQUIRE(m.PlayCard("Bob", 4));
    m.Tick();
    t.record("play:Bob:red2");

    REQUIRE(m.PlayCard("Alice", 1));
    m.Tick();
    t.record("play:Alice:red3");

    REQUIRE(m.DrawCard("Bob"));
    m.Tick();
    t.record("draw:Bob");

    return t.finish();
}

/**
 * @brief seven_zero: a 7 prompts for a swap target, ProvideInput resolves.
 */
json scenario_seven_zero_swap() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kRed, Value::k7, 7),
                card(Type::kBlue, Value::k5, 2)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::k9, 3),
                card(Type::kRed, Value::k2, 4)}))
        }),
        json::array(),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1,
        json::array({"seven_zero"}));

    MatchInstance m(saved, settings_with_mods({"seven_zero"}));
    Transcript t(m, "seven_zero_swap", false);

    t.record("load");

    REQUIRE(m.PlayCard("Alice", 7));
    m.Tick();
    t.record("play:Alice:red7");
    REQUIRE(m.IsWaitingForInput());
    CHECK_EQ(m.GetPendingAction(), Action::kChooseTarget);

    m.ProvideInput("Alice", "Bob");
    m.Tick();
    t.record("input:Alice:swap_with_Bob");

    return t.finish();
}

/**
 * @brief seven_zero: a 0 rotates every hand one seat in play direction.
 */
json scenario_seven_zero_zero_rotate() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kRed, Value::k0, 10),
                card(Type::kBlue, Value::k5, 2)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::k9, 3)})),
            player_json("Carol", json::array({
                card(Type::kYellow, Value::k8, 5)}))
        }),
        json::array(),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1,
        json::array({"seven_zero"}));

    MatchInstance m(saved, settings_with_mods({"seven_zero"}));
    Transcript t(m, "seven_zero_zero_rotate", false);

    t.record("load");

    REQUIRE(m.PlayCard("Alice", 10));
    m.Tick();
    t.record("play:Alice:red0");

    return t.finish();
}

/**
 * @brief draw_stacking: +2 stacks across players, then the debt resolves.
 *
 * @warning may_differ=true: rebuilds this on response windows.
 */
json scenario_draw_stacking() {
    json draw_cards = json::array({
        card(Type::kBlue, Value::k1, 300),
        card(Type::kBlue, Value::k2, 301),
        card(Type::kBlue, Value::k3, 302),
        card(Type::kBlue, Value::k4, 303),
        card(Type::kBlue, Value::k5, 304),
        card(Type::kBlue, Value::k6, 305),
        card(Type::kBlue, Value::k7, 306),
        card(Type::kBlue, Value::k8, 307)
    });
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kRed, Value::kDraw2, 20),
                card(Type::kBlue, Value::k5, 2)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::kDraw2, 21),
                card(Type::kRed, Value::k9, 4)}))
        }),
        draw_cards,
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1,
        json::array({"draw_stacking"}));

    MatchInstance m(saved, settings_with_mods({"draw_stacking"}));
    Transcript t(m, "draw_stacking", true);

    t.record("load");

    REQUIRE(m.PlayCard("Alice", 20));
    m.Tick();
    t.record("play:Alice:red_draw2");
    CHECK_EQ(m.ExportState()["pending_draws"].get<int>(), 2);

    REQUIRE(m.PlayCard("Bob", 21));
    m.Tick();
    t.record("play:Bob:green_draw2_stack");
    CHECK_EQ(m.ExportState()["pending_draws"].get<int>(), 4);

    REQUIRE(m.DrawCard("Alice"));
    m.Tick();
    t.record("draw:Alice:takes_4");

    return t.finish();
}

/**
 * @brief progressive: draw until a playable card turns up, then decide.
 */
json scenario_progressive() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kBlue, Value::k5, 2)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::k9, 3)}))
        }),
        json::array({card(Type::kRed, Value::k3, 302),
                     card(Type::kGreen, Value::k7, 301),
                     card(Type::kBlue, Value::k9, 300)}),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1,
        json::array({"progressive"}));

    MatchInstance m(saved, settings_with_mods({"progressive"}));
    Transcript t(m, "progressive", false);

    t.record("load");

    REQUIRE(m.DrawCard("Alice"));
    m.Tick();
    t.record("draw:Alice:until_playable");
    REQUIRE(m.IsWaitingForInput());
    CHECK_EQ(m.GetPendingAction(), Action::kPlayDrawn);

    m.ProvideInput("Alice", "1");
    m.Tick();
    t.record("input:Alice:keep");

    return t.finish();
}

/**
 * @brief force_play: a playable drawn card is played with no prompt.
 */
json scenario_force_play() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kBlue, Value::k5, 2)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::k9, 3)}))
        }),
        json::array({card(Type::kGreen, Value::k4, 201),
                     card(Type::kRed, Value::k3, 500)}),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1,
        json::array({"force_play"}));

    MatchInstance m(saved, settings_with_mods({"force_play"}));
    Transcript t(m, "force_play", false);

    t.record("load");

    REQUIRE(m.DrawCard("Alice"));
    m.Tick();
    t.record("draw:Alice:forced_play");

    return t.finish();
}

/**
 * @brief jump_in: an identical out-of-turn card steals the turn.
 */
json scenario_jump_in() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kBlue, Value::k3, 1)})),
            player_json("Bob", json::array({
                card(Type::kRed, Value::k5, 50),
                card(Type::kGreen, Value::k9, 3)}))
        }),
        json::array(),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1,
        json::array({"jump_in"}));

    MatchInstance m(saved, settings_with_mods({"jump_in"}));
    Transcript t(m, "jump_in", false);

    t.record("load");

    REQUIRE(m.PlayCard("Bob", 50));
    m.Tick();
    t.record("play:Bob:red5_out_of_turn");

    return t.finish();
}

/**
 * @brief no_bluffing: +4 is legal when holding no matching colour.
 */
json scenario_no_bluffing_allowed() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kBlue, Value::k3, 11),
                card(Type::kWhite, Value::kJollyDraw4, 12)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::k9, 3),
                card(Type::kRed, Value::k2, 4)}))
        }),
        json::array({card(Type::kBlue, Value::k1, 300),
                     card(Type::kBlue, Value::k2, 301),
                     card(Type::kBlue, Value::k3, 302),
                     card(Type::kBlue, Value::k4, 303),
                     card(Type::kBlue, Value::k5, 304),
                     card(Type::kBlue, Value::k6, 305)}),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1,
        json::array({"no_bluffing"}));

    MatchInstance m(saved, settings_with_mods({"no_bluffing"}));
    Transcript t(m, "no_bluffing_allowed", false);

    t.record("load");

    REQUIRE(m.PlayCard("Alice", 12));
    m.Tick();
    t.record("play:Alice:draw4");
    REQUIRE(m.IsWaitingForInput());
    CHECK_EQ(m.GetPendingAction(), Action::kChooseType);

    m.ProvideInput("Alice", "3");
    m.Tick();
    t.record("input:Alice:yellow");

    return t.finish();
}

/**
 * @brief no_bluffing: +4 is rejected while holding the active colour.
 */
json scenario_no_bluffing_denied() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kRed, Value::k7, 13),
                card(Type::kWhite, Value::kJollyDraw4, 12)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::k9, 3)}))
        }),
        json::array(),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1,
        json::array({"no_bluffing"}));

    MatchInstance m(saved, settings_with_mods({"no_bluffing"}));
    Transcript t(m, "no_bluffing_denied", false);

    t.record("load");

    CHECK_FALSE(m.PlayCard("Alice", 12));
    t.record("play:Alice:draw4_denied");

    REQUIRE(m.PlayCard("Alice", 13));
    m.Tick();
    t.record("play:Alice:red7");

    return t.finish();
}

/**
 * @brief Standard Jolly prompts for a colour; ProvideInput resolves it.
 */
json scenario_prompt_choose_color() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kWhite, Value::kJolly, 30),
                card(Type::kBlue, Value::k5, 2)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::k9, 3)}))
        }),
        json::array(),
        json::array({card(Type::kRed, Value::k5, 100)}),
        0, 1);

    MatchInstance m(saved, settings_with_mods({}));
    Transcript t(m, "prompt_choose_color", false);

    t.record("load");

    REQUIRE(m.PlayCard("Alice", 30));
    m.Tick();
    t.record("play:Alice:jolly");
    REQUIRE(m.IsWaitingForInput());
    CHECK_EQ(m.GetPendingAction(), Action::kChooseType);

    m.ProvideInput("Alice", "1");
    m.Tick();
    t.record("input:Alice:blue");

    return t.finish();
}

/**
 * @brief A single bot turn on a hand-authored state.
 */
json scenario_bot_turn() {
    json saved = build_state(
        json::array({
            player_json("Alice", json::array({
                card(Type::kBlue, Value::k3, 1)})),
            player_json("Bob", json::array({
                card(Type::kGreen, Value::k9, 3),
                card(Type::kRed, Value::k2, 4)}), true)
        }),
        json::array(),
        json::array({card(Type::kRed, Value::k5, 100)}),
        1, 1);

    MatchInstance m(saved, settings_with_mods({}));
    Transcript t(m, "bot_turn", false);

    t.record("load:Bob_to_move");

    m.TakeBotTurn();
    t.record("bot:Bob");

    return t.finish();
}

/**
 * @brief One registered scenario: name, may_differ flag, runner.
 */
struct Scenario {
    std::string name;
    bool may_differ;
    std::function<json()> run;
};

/**
 * @brief The full scenario set; order defines the capture order.
 */
std::vector<Scenario> all_scenarios() {
    return {
        {"standard_baseline",     false, scenario_standard_baseline},
        {"seven_zero_swap",       false, scenario_seven_zero_swap},
        {"seven_zero_zero_rotate", false, scenario_seven_zero_zero_rotate},
        {"draw_stacking",         true,  scenario_draw_stacking},
        {"progressive",           false, scenario_progressive},
        {"force_play",            false, scenario_force_play},
        {"jump_in",               false, scenario_jump_in},
        {"no_bluffing_allowed",   false, scenario_no_bluffing_allowed},
        {"no_bluffing_denied",    false, scenario_no_bluffing_denied},
        {"prompt_choose_color",   false, scenario_prompt_choose_color},
        {"bot_turn",              false, scenario_bot_turn}
    };
}

/**
 * @brief Walks up from this test file to find the repository root.
 */
fs::path repo_root() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (fs::exists(p / "CMakeLists.txt", ec) &&
            fs::is_directory(p / "tests" / "unit", ec)) {
            return p;
        }
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return fs::current_path();
}

/**
 * @brief Location of the committed golden transcript fixtures.
 */
fs::path golden_dir() {
    return repo_root() / "tests" / "fixtures" / "golden";
}

/**
 * @brief Writes one transcript as pretty JSON with a trailing newline.
 */
void write_transcript(const fs::path& dir, const json& transcript) {
    fs::path out = dir / (transcript["scenario"].get<std::string>() + ".json");
    std::ofstream file(out);
    REQUIRE_MESSAGE(file.good(), "cannot write ", out.string());
    file << transcript.dump(2) << "\n";
}

}  // namespace

/**
 * @brief Capture mode (env UNI_GOLDEN_CAPTURE=1) regenerates every fixture;
 *        otherwise this re-executes each scenario on the old engine and
 *        deep-compares the produced `ExportState()` step sequence against the
 *        committed fixture (may_differ scenarios skip state-value comparison
 *        but keep shape, step-count and action-label checks).
 */
TEST_CASE("golden: legacy-engine transcripts (capture or verify)") {
    const char* capture_env = std::getenv("UNI_GOLDEN_CAPTURE");
    const bool capture = capture_env != nullptr &&
                         std::string(capture_env) == "1";
    const fs::path dir = golden_dir();

    if (capture) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        REQUIRE_MESSAGE(!ec, "failed to create ", dir.string(), ": ",
                        ec.message());

        for (const auto& scenario : all_scenarios()) {
            json transcript = scenario.run();
            CHECK_EQ(transcript["scenario"].get<std::string>(), scenario.name);
            write_transcript(dir, transcript);
        }

        MESSAGE("captured ", all_scenarios().size(), " transcripts into ",
                dir.string());
        return;
    }

    REQUIRE_MESSAGE(fs::is_directory(dir), "golden fixture dir missing: ",
                    dir.string());

    std::vector<std::string> expected_names;
    for (const auto& scenario : all_scenarios()) {
        expected_names.push_back(scenario.name);
        fs::path path = dir / (scenario.name + ".json");

        INFO("fixture: ", path.string());
        REQUIRE_MESSAGE(fs::exists(path), "missing golden fixture for ",
                        scenario.name);
        std::ifstream in(path);
        REQUIRE(in.good());

        json committed;
        REQUIRE_NOTHROW(committed = json::parse(in));

        /* INFO: shape checks always apply, even for may_differ scenarios. */
        CHECK_EQ(committed.value("scenario", ""), scenario.name);
        CHECK_EQ(committed.value("engine", ""), "old");
        CHECK_EQ(committed.value("may_differ", false), scenario.may_differ);
        REQUIRE(committed.contains("steps"));
        CHECK(committed["steps"].is_array());
        CHECK_FALSE(committed["steps"].empty());
        for (const auto& step : committed["steps"]) {
            CHECK(step.contains("action"));
            CHECK(step.contains("state"));
        }

        /* INFO: verify mode RE-EXECUTES the scenario on the old engine so a
         *       corrupted or hand-edited fixture cannot pass CI. The produced
         *       transcript is deep-compared against the committed one; for
         *       may_differ scenarios the per-step state values are skipped
         *       (only shape, step count and action labels are compared). */
        const json produced = scenario.run();
        CHECK_EQ(produced.value("scenario", ""), scenario.name);
        CHECK_EQ(produced.value("engine", ""), "old");
        CHECK_EQ(produced.value("may_differ", false), scenario.may_differ);
        REQUIRE(produced.contains("steps"));
        CHECK(produced["steps"].is_array());

        const std::size_t committed_steps = committed["steps"].size();
        const std::size_t produced_steps = produced["steps"].size();
        CHECK_EQ(committed_steps, produced_steps);

        const std::size_t common = std::min(committed_steps, produced_steps);
        for (std::size_t i = 0; i < common; ++i) {
            INFO("step ", i);
            CHECK_EQ(committed["steps"][i].value("action", ""),
                     produced["steps"][i].value("action", ""));
            if (scenario.may_differ) {
                CHECK(committed["steps"][i]["state"].is_object());
                CHECK(produced["steps"][i]["state"].is_object());
            } else {
                CHECK(committed["steps"][i]["state"]
                      == produced["steps"][i]["state"]);
            }
        }
    }

    // INFO: a fixture whose scenario is no longer registered is stale and
    //       must be removed, so flag any orphan files.
    std::vector<std::string> actual_names;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            actual_names.push_back(entry.path().stem().string());
        }
    }
    for (const auto& name : actual_names) {
        INFO("orphan fixture: ", name);
        CHECK(std::find(expected_names.begin(), expected_names.end(), name)
              != expected_names.end());
    }
}
