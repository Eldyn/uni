#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/timers.hpp>
#include <match/server/bot_policy.hpp>
#include <match/server/match_session.hpp>
#include <match/view/view_util.hpp>

#include "support/fake_broadcaster.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
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
namespace ecs = match::ecs;
namespace ops = match::ops;

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

namespace {

constexpr int64_t kJumpInHoldMs = 800;
constexpr int64_t kStackingWindowMs = 7000;

/** @brief Deterministic engine clock so the hold can be stepped through. */
struct StepClock {
    int64_t now = 0;

    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

/** @brief Offers the whole hand (or nothing) to every window. */
class ScriptedWindowPolicy : public match::server::IBotPolicy {
public:
    explicit ScriptedWindowPolicy(bool offer_hand) : offer_hand_(offer_hand) {}

    std::optional<uint32_t> ChoosePlay(const BotView&) override {
        return std::nullopt;
    }
    json ChoosePrompt(const BotView&) override { return nullptr; }
    std::vector<uint32_t> ChooseWindowResponses(const BotView& view) override {
        std::vector<uint32_t> bits;
        if (!offer_hand_) return bits;
        for (const BotHandCard& card : view.hand) bits.push_back(card.bits);
        return bits;
    }

private:
    bool offer_hand_;
};

std::optional<ecs::Entity> FindCardByFace(MatchInstance& engine,
                                          const std::string& color,
                                          const std::string& label,
                                          std::optional<ecs::Entity> skip) {
    for (ecs::Entity card : engine.Registries().cards) {
        if (skip.has_value() && card == *skip) continue;
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == color && face->label == label) {
            return card;
        }
    }
    return std::nullopt;
}

void SetHand(MatchInstance& engine, ecs::Entity player,
             const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kDrawPile,
                                         ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

std::size_t HandCount(MatchInstance& engine, const std::string& username) {
    const ecs::Hand* hand =
        engine.Store().Get<ecs::Hand>(*engine.FindPlayer(username));
    return hand == nullptr ? 0 : hand->cards.size();
}

/** @brief Picks the held drawn card (or nothing) and records the view seen. */
class DrawnChoicePolicy : public match::server::IBotPolicy {
public:
    explicit DrawnChoicePolicy(bool take) : take_(take) {}

    std::optional<uint32_t> ChoosePlay(const BotView& view) override {
        seen_hand_size = view.hand.size();
        seen_drawn = view.drawn_card;
        if (!take_ || !view.drawn_card.has_value()) return std::nullopt;
        // INFO: the driver must hand over a hand holding only the draw.
        if (view.hand.size() == 1
            && view.hand.front().bits == *view.drawn_card) {
            return view.drawn_card;
        }
        return std::nullopt;
    }
    json ChoosePrompt(const BotView&) override { return nullptr; }
    std::vector<uint32_t> ChooseWindowResponses(const BotView&) override {
        return {};
    }

    std::size_t seen_hand_size = 0;
    std::optional<uint32_t> seen_drawn;

private:
    bool take_;
};

/** @brief A two-bot session with bot0's turn parked on a playable draw. */
struct HeldDraw {
    Content content;
    std::unique_ptr<MatchSession> session;
    ecs::Entity drawn{};
    std::size_t hand_before = 0;
};

bool OpenHeldDraw(HeldDraw& out) {
    if (!LoadContent(out.content)) return false;
    MatchAssemblyOptions options;
    options.starting_cards = 5;
    options.seed = 7;
    for (int index = 0; index < 2; ++index) {
        MatchPlayerSpec spec;
        spec.username = "bot" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result = MatchAssembler::Assemble(
        out.content.mods, out.content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    auto engine = std::make_unique<MatchInstance>(std::move(result.assembly));
    MatchInstance& raw = *engine;
    REQUIRE(raw.GetCurrentPlayerUsername() == "bot0");

    ecs::PileContents* draw =
        raw.Store().Get<ecs::PileContents>(raw.Registries().draw_pile);
    if (draw == nullptr) return false;
    std::optional<ecs::Entity> chosen;
    for (std::size_t i = 0; i < draw->cards.size(); ++i) {
        const ecs::FaceSpec* face =
            raw.Store().Get<ecs::FaceSpec>(draw->cards[i]);
        if (face == nullptr || face->color == "white") continue;
        if (face->label.size() != 1) continue;
        if (!std::isdigit(static_cast<unsigned char>(face->label[0]))) continue;
        std::swap(draw->cards[i], draw->cards.back());
        for (std::size_t j = 0; j < draw->cards.size(); ++j) {
            if (ecs::InZone* in = raw.Store().Get<ecs::InZone>(draw->cards[j])) {
                in->ordinal = static_cast<uint32_t>(j);
            }
        }
        chosen = draw->cards.back();
        break;
    }
    if (!chosen.has_value()) return false;
    const ecs::FaceSpec* face = raw.Store().Get<ecs::FaceSpec>(*chosen);
    if (face == nullptr) return false;
    ecs::ActiveTypeReq* req =
        raw.Store().Get<ecs::ActiveTypeReq>(raw.Registries().match);
    if (req == nullptr) return false;
    req->type = face->color;

    out.drawn = *chosen;
    out.hand_before = HandCount(raw, "bot0");
    if (!raw.DrawCard("bot0")) return false;
    if (!raw.PendingPlayDrawnState().has_value()) return false;

    out.session = std::make_unique<MatchSession>(
        std::move(engine), std::move(out.content.mods),
        MatchSession::SocketMap{{"bot0", PlayerSocket(0)},
                                {"bot1", PlayerSocket(1)}});
    return true;
}

/** @brief bot0 played a +2 into a jump_in + draw_stacking group. */
struct DebtGroup {
    Content content;
    StepClock clock;
    std::unique_ptr<MatchSession> session;
    std::size_t victim_hand = 0;
};

bool OpenDebtGroup(DebtGroup& group) {
    if (!LoadContent(group.content)) return false;
    group.content.classic.mods = {"vanilla", "jump_in", "draw_stacking"};
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < 3; ++index) {
        MatchPlayerSpec spec;
        spec.username = "bot" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result = MatchAssembler::Assemble(
        group.content.mods, group.content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    auto engine = std::make_unique<MatchInstance>(std::move(result.assembly),
                                                  group.clock.Fn());
    MatchInstance& raw = *engine;

    const std::optional<ecs::Entity> draw2 =
        FindCardByFace(raw, "red", "+2", std::nullopt);
    const std::optional<ecs::Entity> filler =
        FindCardByFace(raw, "blue", "5", std::nullopt);
    const std::optional<ecs::Entity> victim_card =
        FindCardByFace(raw, "blue", "6", std::nullopt);
    const std::optional<ecs::Entity> bystander_card =
        FindCardByFace(raw, "blue", "7", std::nullopt);
    if (!draw2 || !filler || !victim_card || !bystander_card) return false;
    SetHand(raw, *raw.FindPlayer("bot0"), {*draw2, *filler});
    SetHand(raw, *raw.FindPlayer("bot1"), {*victim_card});
    SetHand(raw, *raw.FindPlayer("bot2"), {*bystander_card});
    raw.Store().Get<ecs::ActiveTypeReq>(raw.Registries().match)->type = "red";
    group.victim_hand = HandCount(raw, "bot1");
    if (!raw.PlayCard("bot0", *draw2) || !raw.WindowOpen()) return false;

    group.session = std::make_unique<MatchSession>(
        std::move(engine), std::move(group.content.mods),
        MatchSession::SocketMap{{"bot0", PlayerSocket(0)},
                                {"bot1", PlayerSocket(1)},
                                {"bot2", PlayerSocket(2)}});
    return true;
}

}  // namespace

TEST_CASE("bot policy: a victim with nothing to stack passes after the hold") {
    DebtGroup group;
    REQUIRE(OpenDebtGroup(group));
    ScriptedWindowPolicy declines(false);
    MatchInstance& engine = group.session->Engine();

    group.clock.now = kJumpInHoldMs - 1;
    CHECK_FALSE(BotStep(*group.session, declines, "bot1"));
    CHECK(engine.WindowOpen());
    CHECK(HandCount(engine, "bot1") == group.victim_hand);

    group.clock.now = kJumpInHoldMs;
    CHECK(BotStep(*group.session, declines, "bot1"));
    CHECK_FALSE(engine.WindowOpen());
    CHECK(HandCount(engine, "bot1") == group.victim_hand + 2);
}

TEST_CASE("bot policy: a non-victim bot never passes a debt group") {
    DebtGroup group;
    REQUIRE(OpenDebtGroup(group));
    ScriptedWindowPolicy declines(false);
    MatchInstance& engine = group.session->Engine();

    for (int64_t now : {int64_t{0}, kJumpInHoldMs, kStackingWindowMs - 1}) {
        group.clock.now = now;
        CHECK_FALSE(BotStep(*group.session, declines, "bot2"));
    }
    CHECK(engine.WindowOpen());
    CHECK(engine.ExportWindow()["responses"].empty());
}

TEST_CASE("bot policy: a jump-in candidate is still played in the hold") {
    DebtGroup group;
    REQUIRE(OpenDebtGroup(group));
    ScriptedWindowPolicy stacks(true);
    MatchInstance& engine = group.session->Engine();
    const ecs::Entity bot2 = *engine.FindPlayer("bot2");
    const std::optional<ecs::Entity> identical = [&]() {
        const ecs::PileContents* discard =
            engine.Store().Get<ecs::PileContents>(
                engine.Registries().discard_pile);
        return FindCardByFace(engine, "red", "+2", discard->cards.back());
    }();
    REQUIRE(identical.has_value());
    SetHand(engine, bot2, {*identical});

    group.clock.now = 100;
    CHECK(BotStep(*group.session, stacks, "bot2"));
    const json responses = engine.ExportWindow()["responses"];
    REQUIRE(responses.size() == 1);
    CHECK(responses[0]["player"] == "bot2");
    CHECK(responses[0]["pass"] == false);
}

TEST_CASE("bot policy: nobody passes a jump_in-only window") {
    Content content;
    REQUIRE(LoadContent(content));
    content.classic.mods = {"vanilla", "jump_in"};
    StepClock clock;
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < 3; ++index) {
        MatchPlayerSpec spec;
        spec.username = "bot" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    auto engine =
        std::make_unique<MatchInstance>(std::move(result.assembly), clock.Fn());
    MatchInstance& raw = *engine;

    const ecs::PileContents* discard = raw.Store().Get<ecs::PileContents>(
        raw.Registries().discard_pile);
    REQUIRE(discard != nullptr);
    const ecs::Entity top = discard->cards.back();
    const ecs::FaceSpec* top_face = raw.Store().Get<ecs::FaceSpec>(top);
    REQUIRE(top_face != nullptr);
    const std::optional<ecs::Entity> twin =
        FindCardByFace(raw, top_face->color, top_face->label, top);
    REQUIRE(twin.has_value());
    SetHand(raw, *raw.FindPlayer("bot1"), {*twin});
    REQUIRE(raw.PlayCard("bot1", *twin));
    REQUIRE(raw.WindowOpen());

    MatchSession session(
        std::move(engine), std::move(content.mods),
        MatchSession::SocketMap{{"bot0", PlayerSocket(0)},
                                {"bot1", PlayerSocket(1)},
                                {"bot2", PlayerSocket(2)}});
    ScriptedWindowPolicy declines(false);
    for (int64_t now : {int64_t{0}, kJumpInHoldMs - 1}) {
        clock.now = now;
        CHECK_FALSE(BotStep(session, declines, "bot0"));
        CHECK_FALSE(BotStep(session, declines, "bot2"));
    }
    CHECK(session.Engine().WindowOpen());
    CHECK(session.Engine().ExportWindow()["responses"].empty());
}

TEST_CASE("bot policy: a policy that picks the drawn card plays it") {
    HeldDraw fixture;
    REQUIRE(OpenHeldDraw(fixture));
    DrawnChoicePolicy policy(true);
    MatchSession& session = *fixture.session;
    MatchInstance& engine = session.Engine();
    const std::optional<ecs::CompactCardV2> bits =
        engine.Registries().CardId(fixture.drawn);
    REQUIRE(bits.has_value());

    CHECK(BotStep(session, policy, "bot0"));

    // INFO: the driver offered a view whose hand held only the drawn card.
    CHECK(policy.seen_hand_size == 1);
    REQUIRE(policy.seen_drawn.has_value());
    CHECK(*policy.seen_drawn == bits->bits);

    CHECK_FALSE(engine.PendingPlayDrawnState().has_value());
    const ecs::InZone* zone = engine.Store().Get<ecs::InZone>(fixture.drawn);
    REQUIRE(zone != nullptr);
    CHECK(zone->zone.kind == ecs::ZoneKind::kDiscardPile);
    CHECK(engine.GetCurrentPlayerUsername() == "bot1");
    CHECK(HandCount(engine, "bot0") == fixture.hand_before);
}

TEST_CASE("bot policy: a policy that picks nothing keeps the drawn card") {
    HeldDraw fixture;
    REQUIRE(OpenHeldDraw(fixture));
    DrawnChoicePolicy policy(false);
    MatchSession& session = *fixture.session;
    MatchInstance& engine = session.Engine();

    CHECK(BotStep(session, policy, "bot0"));

    CHECK(policy.seen_hand_size == 1);
    REQUIRE(policy.seen_drawn.has_value());

    // INFO: keeping clears the hold, passes the turn and leaves the card.
    CHECK_FALSE(engine.PendingPlayDrawnState().has_value());
    const ecs::InZone* zone = engine.Store().Get<ecs::InZone>(fixture.drawn);
    REQUIRE(zone != nullptr);
    CHECK(zone->zone.kind == ecs::ZoneKind::kHand);
    CHECK(engine.GetCurrentPlayerUsername() == "bot1");
    CHECK(HandCount(engine, "bot0") == fixture.hand_before + 1);
}

TEST_CASE("bot policy: the held draw is exposed only to its owner") {
    HeldDraw fixture;
    REQUIRE(OpenHeldDraw(fixture));
    MatchSession& session = *fixture.session;
    MatchInstance& engine = session.Engine();
    const std::optional<ecs::CompactCardV2> bits =
        engine.Registries().CardId(fixture.drawn);
    REQUIRE(bits.has_value());

    const BotView owner = match::server::BuildBotView(session, "bot0");
    const BotView other = match::server::BuildBotView(session, "bot1");
    REQUIRE(owner.drawn_card.has_value());
    CHECK(*owner.drawn_card == bits->bits);
    CHECK_FALSE(other.drawn_card.has_value());

    // INFO: a non-owner's step is a no-op; the owner's choice stays parked.
    DrawnChoicePolicy policy(false);
    CHECK_FALSE(BotStep(session, policy, "bot1"));
    CHECK(engine.PendingPlayDrawnState().has_value());
}
