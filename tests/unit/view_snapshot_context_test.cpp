#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/status.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file view_snapshot_context_test.cpp
 * @brief The warm-up snapshot `context`: engine-owned values for late join.
 *
 * A late joiner reads the active color, the direction, the draw debt, the
 * play-drawn hold, the live timers and the sequence of the last client event
 * from `match_state.context`, so it rebuilds nothing from events.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;
using match::view::EventSink;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

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

struct FakeClock {
    int64_t now = 0;
    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

match::WindowConfig FixedWindow(int64_t milliseconds) {
    match::WindowConfig config;
    config.window_ms = milliseconds;
    config.mode = match::WindowMode::kFixed;
    return config;
}

std::unique_ptr<MatchInstance> MakeEngine(
    Content& content, const std::vector<std::string>& mods,
    FakeClock& clock, uint64_t seed = 42) {
    DeckDef deck = content.classic;
    if (!mods.empty()) deck.mods = mods;
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = seed;
    for (int index = 0; index < 3; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly),
                                           FixedWindow(7000), clock.Fn());
}

std::optional<ecs::Entity> FindCard(MatchInstance& engine,
                                    const std::string& color,
                                    const std::string& label,
                                    std::optional<ecs::Entity> excluded = {}) {
    for (ecs::Entity card : engine.Registries().cards) {
        if (excluded.has_value() && card == *excluded) continue;
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == color && face->label == label) {
            return card;
        }
    }
    return std::nullopt;
}

void ForceHand(MatchInstance& engine, ecs::Entity player,
               const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        match::ops::MoveCardToZone(
            engine.Store(), card, ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

void SetActiveColor(MatchInstance& engine, const std::string& color) {
    engine.Store().Get<ecs::ActiveTypeReq>(engine.Registries().match)->type =
        color;
}

/** @brief Top the draw pile with a colored card and make it playable. */
std::optional<ecs::Entity> ArmPlayableDraw(MatchInstance& engine) {
    for (ecs::Entity pile : engine.Store().EntitiesWith<ecs::PileContents>()) {
        ecs::PileContents* draw = engine.Store().Get<ecs::PileContents>(pile);
        if (draw == nullptr || draw->kind != ecs::PileKind::kDraw) continue;
        for (std::size_t index = 0; index < draw->cards.size(); ++index) {
            const ecs::FaceSpec* face =
                engine.Store().Get<ecs::FaceSpec>(draw->cards[index]);
            if (face == nullptr || face->color == "white") continue;
            std::swap(draw->cards[index], draw->cards.back());
            for (std::size_t slot = 0; slot < draw->cards.size(); ++slot) {
                if (ecs::InZone* zone =
                        engine.Store().Get<ecs::InZone>(draw->cards[slot])) {
                    zone->ordinal = static_cast<uint32_t>(slot);
                }
            }
            SetActiveColor(engine, face->color);
            return draw->cards.back();
        }
    }
    return std::nullopt;
}

bool JsonHasNumber(const json& value, uint32_t needle) {
    if (value.is_number_integer()) {
        return value.get<int64_t>() == static_cast<int64_t>(needle);
    }
    if (value.is_array() || value.is_object()) {
        for (const json& item : value) {
            if (JsonHasNumber(item, needle)) return true;
        }
    }
    return false;
}

json ContextFor(const ViewBuilder& builder, const Viewer& viewer,
                const EventSink& sink) {
    const json snapshot = builder.BuildSnapshot(viewer, sink);
    REQUIRE(snapshot["match_state"].contains("context"));
    return snapshot["match_state"]["context"];
}

/** @brief Snapshot state without the values that depend on the stream. */
json StateWithoutStreamFields(json state) {
    state.erase("seq_watermark");
    state["context"].erase("last_event_sequence");
    return state;
}

/** @brief What a client learns from the packets of its own stream. */
struct FoldedStream {
    int64_t direction = 1;
    int64_t draw_debt = 0;
    json hold = nullptr;
    json last_sequence = nullptr;
};

/**
 * @brief A continuous seat: one real sink that receives every packet.
 *
 * `Deliver` wraps the engine events that the seat has not received yet, as the
 * session does after each move, and folds them into client-visible values.
 */
class ContinuousSeat {
public:
    ContinuousSeat(const ViewBuilder& builder, const MatchInstance& engine,
                   Viewer viewer)
        : builder_(builder), engine_(engine), viewer_(std::move(viewer)) {}

    void Deliver() {
        const std::vector<json>& events = engine_.Events();
        for (; delivered_ < events.size(); ++delivered_) {
            const std::optional<json> packet =
                builder_.Wrap(events[delivered_], viewer_, sink_);
            if (packet.has_value()) Fold(*packet);
        }
    }

    const EventSink& Sink() const { return sink_; }
    const FoldedStream& Folded() const { return folded_; }

    json Snapshot() const { return builder_.BuildSnapshot(viewer_, sink_); }

private:
    void Fold(const json& packet) {
        folded_.last_sequence = packet["sequence"];
        const std::string type = packet.value("type", std::string());
        const json& payload = packet["payload"];
        if (type == "direction_changed") {
            folded_.direction = payload["direction"];
        } else if (type == "cards_drawn" && payload.value("held", false)) {
            folded_.hold = json{{"player", payload["player"]}, {"held", true}};
        } else if (type == "drawn_card_kept" || type == "card_played"
                   || type == "turn_advance") {
            folded_.hold = nullptr;
        } else if (payload.value("status_kind", std::string())
                   == "uni:draw_debt") {
            const int64_t instance = payload["instance_id"];
            if (type == "status_applied" || type == "status_modified") {
                debt_by_instance_[instance] = payload["magnitude"];
            } else if (type == "status_removed") {
                debt_by_instance_.erase(instance);
            }
            folded_.draw_debt = 0;
            for (const auto& [instance_id, magnitude] : debt_by_instance_) {
                folded_.draw_debt += magnitude;
            }
        }
    }

    const ViewBuilder& builder_;
    const MatchInstance& engine_;
    Viewer viewer_;
    EventSink sink_;
    std::size_t delivered_ = 0;
    FoldedStream folded_;
    std::map<int64_t, int64_t> debt_by_instance_;
};

/** @brief Check a snapshot context against the values folded from events. */
void CheckContextMatchesStream(const json& context,
                               const FoldedStream& folded) {
    CHECK(context["direction"] == folded.direction);
    CHECK(context["draw_debt"] == folded.draw_debt);
    CHECK(context["hold"] == folded.hold);
    CHECK(context["last_event_sequence"] == folded.last_sequence);
}

/** @brief The snapshot that a seat joining now receives. */
json LateJoinerSnapshot(const ViewBuilder& builder, const Viewer& viewer) {
    EventSink fresh_sink;
    return builder.BuildSnapshot(viewer, fresh_sink);
}

}  // namespace

TEST_CASE("snapshot context: holds the engine-owned values") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {}, clock);
    ViewBuilder builder(*engine, content.mods);
    SetActiveColor(*engine, "green");

    EventSink sink;
    const json context = ContextFor(builder, Viewer::Player("player0"), sink);

    CHECK(context["active_color"] == "green");
    CHECK(context["direction"] == 1);
    CHECK(context["draw_debt"] == 0);
    CHECK(context["hold"].is_null());
    REQUIRE(context["timers"].is_object());
    CHECK(context["timers"]["turn_remaining_ms"] == 0);
    CHECK(context["timers"]["window_remaining_ms"] == 0);
    REQUIRE(context["streaks"].is_object());
    CHECK(context["streaks"].empty());
}

TEST_CASE("snapshot context: a hidden debt counts for its owner only") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {}, clock);
    ViewBuilder builder(*engine, content.mods);

    const auto apply_debt = [&](const std::string& seat, int magnitude,
                                bool hidden) {
        match::status::ApplyRequest request;
        request.status_id = "uni:draw_debt";
        request.magnitude = magnitude;
        request.has_hidden = true;
        request.hidden = hidden;
        REQUIRE(match::status::Apply(engine->Store(),
                                     *engine->FindPlayer(seat), request)
                    .applied);
    };
    apply_debt("player1", 4, true);

    EventSink sink;
    CHECK(ContextFor(builder, Viewer::Player("player1"), sink)["draw_debt"]
          == 4);
    CHECK(ContextFor(builder, Viewer::Player("player0"), sink)["draw_debt"]
          == 0);
    CHECK(ContextFor(builder, Viewer::Spectator(), sink)["draw_debt"] == 0);

    apply_debt("player2", 2, false);
    CHECK(ContextFor(builder, Viewer::Player("player1"), sink)["draw_debt"]
          == 6);
    CHECK(ContextFor(builder, Viewer::Player("player0"), sink)["draw_debt"]
          == 2);
    CHECK(ContextFor(builder, Viewer::Spectator(), sink)["draw_debt"] == 2);
}

TEST_CASE("snapshot context: the hold shows the card to its owner only") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {}, clock);
    ViewBuilder builder(*engine, content.mods);
    const std::optional<ecs::Entity> drawn = ArmPlayableDraw(*engine);
    REQUIRE(drawn.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());
    const uint32_t drawn_bits = engine->Registries().CardId(*drawn)->bits;

    EventSink sink;
    const json owner = ContextFor(builder, Viewer::Player("player0"), sink);
    CHECK(owner["hold"]["player"] == "player0");
    CHECK(owner["hold"]["held"] == true);
    CHECK(owner["hold"]["card"] == drawn_bits);

    const json opponent = ContextFor(builder, Viewer::Player("player1"), sink);
    CHECK(opponent["hold"]["player"] == "player0");
    CHECK(opponent["hold"]["held"] == true);
    CHECK_FALSE(opponent["hold"].contains("card"));
    CHECK_FALSE(JsonHasNumber(opponent, drawn_bits));

    const json spectator = ContextFor(builder, Viewer::Spectator(), sink);
    CHECK(spectator["hold"]["held"] == true);
    CHECK_FALSE(spectator["hold"].contains("card"));

    const json private_spectator =
        builder.BuildSnapshot(Viewer::Spectator({"player0"}), sink);
    CHECK(private_spectator["match_state"]["context"]["hold"]["held"] == true);
    CHECK_FALSE(JsonHasNumber(private_spectator["match_state"], drawn_bits));
}

TEST_CASE("snapshot context: names the sequence of the last client event") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {}, clock);
    ViewBuilder builder(*engine, content.mods);
    EventSink sink;

    CHECK(ContextFor(builder, Viewer::Player("player0"), sink)
              ["last_event_sequence"]
              .is_null());

    const json round =
        json{{"type", "round_advance"}, {"payload", json{{"round", 1}}}};
    const json placement =
        json{{"type", "placement"},
             {"payload", json{{"player", "player0"}, {"place", 1}}}};
    REQUIRE(builder.Wrap(round, Viewer::Player("player0"), sink).has_value());
    REQUIRE(builder.Wrap(placement, Viewer::Player("player0"), sink)
                .has_value());

    const json snapshot =
        builder.BuildSnapshot(Viewer::Player("player0"), sink);
    const json& state = snapshot["match_state"];
    CHECK(state["context"]["last_event_sequence"] == 1);

    const json next =
        builder.Wrap(round, Viewer::Player("player0"), sink).value();
    CHECK(next["sequence"].get<int64_t>()
          == state["context"]["last_event_sequence"].get<int64_t>() + 1);
    CHECK(state["seq_watermark"] == next["sequence"]);
}

TEST_CASE("snapshot context: a late join during the turn timer") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    clock.now = 10000;
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {}, clock);
    ViewBuilder builder(*engine, content.mods);
    ecs::TurnState* turn = engine->Store().Get<ecs::TurnState>(
        *engine->GetCurrentPlayer());
    REQUIRE(turn != nullptr);
    REQUIRE(engine->Timers().Turn().Arm(*turn, 30000));

    clock.now = 12500;
    EventSink sink;
    const json context = ContextFor(builder, Viewer::Player("player2"), sink);
    CHECK(context["timers"]["turn_deadline_ms"] == 40000);
    CHECK(context["timers"]["turn_remaining_ms"] == 27500);
    CHECK(context["timers"]["window_remaining_ms"] == 0);
}

TEST_CASE("snapshot context: a late join during an open window") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    clock.now = 1000;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, {"uni", "jump_in", "draw_stacking"}, clock);
    ViewBuilder builder(*engine, content.mods);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> draw_two = FindCard(*engine, "red", "+2");
    REQUIRE(draw_two.has_value());
    ForceHand(*engine, player0, {*draw_two});
    SetActiveColor(*engine, "red");
    ecs::TurnState* turn = engine->Store().Get<ecs::TurnState>(player0);
    REQUIRE(engine->Timers().Turn().Arm(*turn, 30000));
    REQUIRE(engine->PlayCard("player0", *draw_two));
    REQUIRE(engine->WindowOpen());

    clock.now = 3000;
    EventSink sink;
    const json context = ContextFor(builder, Viewer::Player("player1"), sink);
    CHECK(context["timers"]["window_deadline_ms"] == 8000);
    CHECK(context["timers"]["window_remaining_ms"] == 5000);
    CHECK(context["timers"]["turn_deadline_ms"] == 0);
    CHECK(context["timers"]["turn_remaining_ms"] == 30000);
}

TEST_CASE("snapshot context: the context equals the folded stream") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, {"uni", "draw_stacking"}, clock);
    ViewBuilder builder(*engine, content.mods);
    ContinuousSeat seat(builder, *engine, Viewer::Player("player1"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const auto reverse = FindCard(*engine, "red", "reverse");
    const auto filler = FindCard(*engine, "red", "5");
    const auto draw_two = FindCard(*engine, "red", "+2");
    REQUIRE((reverse && filler && draw_two));
    ForceHand(*engine, player0, {*reverse, *filler});
    SetActiveColor(*engine, "red");

    REQUIRE(engine->PlayCard("player0", *reverse));
    seat.Deliver();
    CHECK(seat.Folded().direction == -1);
    CheckContextMatchesStream(seat.Snapshot()["match_state"]["context"],
                              seat.Folded());

    const std::string next_seat = engine->GetCurrentPlayerUsername();
    ForceHand(*engine, *engine->FindPlayer(next_seat), {*draw_two});
    SetActiveColor(*engine, "red");
    REQUIRE(engine->PlayCard(next_seat, *draw_two));
    seat.Deliver();
    CHECK(seat.Folded().draw_debt == 2);
    const json continuous = seat.Snapshot();
    CheckContextMatchesStream(continuous["match_state"]["context"],
                              seat.Folded());

    const json late = LateJoinerSnapshot(builder, Viewer::Player("player1"));
    CHECK(StateWithoutStreamFields(continuous["match_state"])
          == StateWithoutStreamFields(late["match_state"]));
    CHECK(late["match_state"]["context"]["last_event_sequence"].is_null());
    CHECK(late["match_state"]["context"]["draw_debt"]
          == seat.Folded().draw_debt);
    CHECK(late["match_state"]["context"]["direction"]
          == seat.Folded().direction);
}

TEST_CASE("snapshot context: the hold equals the folded stream") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, {}, clock);
    ViewBuilder builder(*engine, content.mods);
    ContinuousSeat seat(builder, *engine, Viewer::Player("player1"));

    REQUIRE(ArmPlayableDraw(*engine).has_value());
    REQUIRE(engine->DrawCard("player0"));
    seat.Deliver();
    CHECK(seat.Folded().hold["player"] == "player0");
    const json held = seat.Snapshot();
    CheckContextMatchesStream(held["match_state"]["context"], seat.Folded());
    const json late = LateJoinerSnapshot(builder, Viewer::Player("player1"));
    CHECK(StateWithoutStreamFields(held["match_state"])
          == StateWithoutStreamFields(late["match_state"]));

    REQUIRE(engine->KeepDrawn("player0"));
    seat.Deliver();
    CHECK(seat.Folded().hold.is_null());
    CheckContextMatchesStream(seat.Snapshot()["match_state"]["context"],
                              seat.Folded());
    CHECK(LateJoinerSnapshot(builder, Viewer::Player("player1"))
              ["match_state"]["context"]["hold"]
              .is_null());
}

TEST_CASE("snapshot context: a mid-match jump-in equals the folded stream") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, {"uni", "jump_in"}, clock);
    ViewBuilder builder(*engine, content.mods);
    ContinuousSeat seat(builder, *engine, Viewer::Player("player1"));
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player2 = *engine->FindPlayer("player2");
    const auto played = FindCard(*engine, "red", "6");
    REQUIRE(played.has_value());
    const auto twin = FindCard(*engine, "red", "6", played);
    const auto filler = FindCard(*engine, "blue", "5");
    REQUIRE((twin && filler));
    ForceHand(*engine, player0, {*played, *filler});
    ForceHand(*engine, player2, {*twin, *filler});
    SetActiveColor(*engine, "red");
    REQUIRE(engine->PlayCard("player0", *played));
    REQUIRE(engine->WindowOpen());
    REQUIRE(engine->RespondWindow("player2", *twin));
    clock.now += 7000;
    seat.Deliver();

    const json continuous = seat.Snapshot();
    CheckContextMatchesStream(continuous["match_state"]["context"],
                              seat.Folded());
    const json late = LateJoinerSnapshot(builder, Viewer::Player("player1"));
    CHECK(StateWithoutStreamFields(continuous["match_state"])
          == StateWithoutStreamFields(late["match_state"]));
}

TEST_CASE("snapshot context: a window after the turn deadline clamps to 0") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    clock.now = 1000;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, {"uni", "draw_stacking"}, clock);
    ViewBuilder builder(*engine, content.mods);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const auto draw_two = FindCard(*engine, "red", "+2");
    REQUIRE(draw_two.has_value());
    ForceHand(*engine, player0, {*draw_two});
    SetActiveColor(*engine, "red");
    ecs::TurnState* turn = engine->Store().Get<ecs::TurnState>(player0);
    REQUIRE(engine->Timers().Turn().Arm(*turn, 30000));
    clock.now = 40000;
    REQUIRE(engine->PlayCard("player0", *draw_two));
    REQUIRE(engine->WindowOpen());

    EventSink sink;
    const json context = ContextFor(builder, Viewer::Player("player1"), sink);
    CHECK(context["timers"]["turn_remaining_ms"] == 0);
    CHECK(context["timers"]["window_remaining_ms"] == 7000);
}
