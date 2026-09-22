#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/view/view_util.hpp>

#include "support/fake_broadcaster.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file bot_policy_test.cpp
 * @brief Bot policy tests.
 *
 * Two layers: pure heuristic decisions against a hand-built `BotView`, and a
 * full 4-bot vanilla match driven through `MatchSession` until `IsMatchOver()`,
 * asserting a bounded completion and well-formed placements.
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
using match::server::BotHandCard;
using match::server::BotPlayerRow;
using match::server::BotStep;
using match::server::BotView;
using match::server::HeuristicBotPolicy;
using match::server::MatchSession;
using nlohmann::json;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

/* INFO: locate the project root from this file so the test is cwd-independent
 *       (mirrors match_session_test.cpp). */
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
            if (deck.deck_id == "vanilla:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (int i = 0; i < players; ++i) {
        MatchPlayerSpec spec;
        spec.username = "bot" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

/** @brief An opaque test socket key; never dereferenced by the fake. */
AppWebSocket* PlayerSocket(int index) {
    return reinterpret_cast<AppWebSocket*>(
        static_cast<std::uintptr_t>(0x200 + index));
}

BotHandCard Card(uint32_t bits, std::string color, std::string value) {
    BotHandCard card;
    card.bits = bits;
    card.color = std::move(color);
    card.value = std::move(value);
    card.can_play = true;
    return card;
}

BotView View(std::string username) {
    BotView view;
    view.username = std::move(username);
    return view;
}

/**
 * @brief Drive `session` with bots until the match ends.
 *
 * Answers the parked prompt, replies to an open window, else takes the turn.
 * Returns the step count; a long idle streak stops early (a stall, not a
 * completion, and the caller's `IsMatchOver` assertion then fails).
 */
int DriveToCompletion(MatchSession& session,
                      match::server::IBotPolicy& policy, int max_steps) {
    int steps = 0;
    int idle = 0;
    while (!session.Engine().IsMatchOver() && steps < max_steps) {
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
        ++steps;
        idle = acted ? 0 : idle + 1;
        if (idle > 200) break;
    }
    return steps;
}

}  // namespace

TEST_CASE("bot policy: four-bot vanilla match completes") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 4, 7);
    REQUIRE(engine->GetCurrentPlayerUsername() == "bot0");

    const std::vector<std::string> bots = {"bot0", "bot1", "bot2", "bot3"};
    std::vector<AppWebSocket*> sockets = {
        PlayerSocket(0), PlayerSocket(1), PlayerSocket(2), PlayerSocket(3)};
    MatchSession session(
        std::move(engine), std::move(content.mods),
        {{bots[0], sockets[0]},
         {bots[1], sockets[1]},
         {bots[2], sockets[2]},
         {bots[3], sockets[3]}});

    HeuristicBotPolicy policy(12345, {});
    const int steps = DriveToCompletion(session, policy, 20000);

    CHECK_MESSAGE(session.Engine().IsMatchOver(),
                  "match did not finish in " << steps << " steps");
    CHECK(steps < 20000);

    const std::string winner = session.Engine().GetWinner();
    CHECK_FALSE(winner.empty());
    CHECK(std::find(bots.begin(), bots.end(), winner) != bots.end());

    const std::vector<std::string> placements =
        session.Engine().GetPlacements();
    REQUIRE_FALSE(placements.empty());
    CHECK(placements.front() == winner);
    for (std::size_t i = 0; i < placements.size(); ++i) {
        CHECK(std::find(bots.begin(), bots.end(), placements[i])
              != bots.end());
        for (std::size_t j = i + 1; j < placements.size(); ++j) {
            CHECK(placements[i] != placements[j]);
        }
    }

    // INFO: the wire path still terminates cleanly after the bot-driven match.
    FakeBroadcaster fake;
    session.EmitEvents(fake);
    session.BroadcastSnapshot(fake);
    CHECK(session.MatchOverNotified());
}

TEST_CASE("bot policy: choose_color is the most common hand colour") {
    HeuristicBotPolicy policy(1);
    BotView view = View("bot0");
    view.prompt_kind = "choose_color";
    view.hand = {Card(1, "red", "1"), Card(2, "blue", "2"),
                 Card(3, "red", "3"), Card(4, "white", "jolly")};
    CHECK(policy.ChoosePrompt(view) == json("red"));

    // INFO: wilds carry no chosen colour; an all-wild hand falls back.
    view.hand = {Card(5, "white", "jolly")};
    CHECK(policy.ChoosePrompt(view) == json("red"));
}

TEST_CASE("bot policy: choose_player is the other fewest-cards seat") {
    HeuristicBotPolicy policy(1);
    BotView view = View("bot0");
    view.prompt_kind = "choose_player";
    view.players = {{"bot0", 5}, {"bot1", 3}, {"bot2", 1}, {"bot3", 4}};
    CHECK(policy.ChoosePrompt(view) == json("bot2"));

    // INFO: the bot never targets itself when another seat exists.
    view.players = {{"bot0", 1}, {"bot1", 3}};
    CHECK(policy.ChoosePrompt(view) == json("bot1"));
}

TEST_CASE("bot policy: choose_card takes the first option") {
    HeuristicBotPolicy policy(1);
    BotView view = View("bot0");
    view.prompt_kind = "choose_card";
    view.prompt_payload = json{{"options", json::array({7, 8, 9})}};
    CHECK(policy.ChoosePrompt(view) == json(7));
}

TEST_CASE("bot policy: choose_value takes the middle of the range") {
    HeuristicBotPolicy policy(1);
    BotView view = View("bot0");
    view.prompt_kind = "choose_value";
    view.prompt_payload = json{{"min", 2}, {"max", 6}};
    CHECK(policy.ChoosePrompt(view) == json(4));

    view.prompt_payload = json{{"min", 3}};
    CHECK(policy.ChoosePrompt(view) == json(3));
}

TEST_CASE("bot policy: yes_no accepts by default") {
    HeuristicBotPolicy policy(1);
    BotView view = View("bot0");
    view.prompt_kind = "choose_yes_no";
    CHECK(policy.ChoosePrompt(view) == json(true));

    view.prompt_kind = "yes_no";
    view.prompt_payload = json{{"default", false}};
    CHECK(policy.ChoosePrompt(view) == json(false));
}

TEST_CASE("bot policy: unknown kind uses default else schema-valid value") {
    HeuristicBotPolicy policy(1);
    BotView view = View("bot0");
    view.prompt_kind = "mod:weird";
    view.prompt_payload = json{{"default", "chosen"}};
    CHECK(policy.ChoosePrompt(view) == json("chosen"));

    // INFO: no payload default - the injected mod schema drives the fallback.
    HeuristicBotPolicy with_schema(
        1, {{"mod:weird",
             json{{"type", "string"}, {"enum", {"first", "second"}}}}});
    view.prompt_payload = json::object();
    CHECK(with_schema.ChoosePrompt(view) == json("first"));
}

TEST_CASE("bot policy: window response is deterministic and ~20%") {
    const std::string name = "bot0";
    HeuristicBotPolicy policy(999, {});
    BotView view = View(name);
    view.window_open = true;
    view.is_responder = true;
    view.hand = {Card(11, "red", "skip"), Card(12, "blue", "draw2")};

    int responded = 0;
    int first_responded_window = -1;
    for (uint64_t window_id = 0; window_id < 200; ++window_id) {
        view.window_id = window_id;
        const std::vector<uint32_t> candidates =
            policy.ChooseWindowResponses(view);
        if (!candidates.empty()) {
            ++responded;
            if (first_responded_window < 0) {
                first_responded_window = static_cast<int>(window_id);
            }
        }
    }
    CHECK(responded > 0);
    CHECK(responded < 200);
    CHECK(responded >= 10);
    CHECK(responded <= 90);

    // INFO: the same window rolls the same way (idempotent, replay-stable).
    view.window_id = static_cast<uint64_t>(first_responded_window);
    const std::vector<uint32_t> once =
        policy.ChooseWindowResponses(view);
    const std::vector<uint32_t> twice =
        policy.ChooseWindowResponses(view);
    CHECK(once == twice);

    // INFO: draw penalties outrank utility cards in the offered order.
    REQUIRE_FALSE(once.empty());
    CHECK(once.front() == 12u);

    // INFO: a non-responder never offers anything.
    view.is_responder = false;
    CHECK(policy.ChooseWindowResponses(view).empty());
}
