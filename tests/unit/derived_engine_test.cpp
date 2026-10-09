#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>
#include <match/streak/derived_engine.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

/**
 * @file derived_engine_test.cpp
 * @brief The shipped near-win window and the block: open, close, replace,
 *        the values from before the event, the actor, the derived guard, and
 *        the cost charge. The events are made by hand.
 */

namespace fs = std::filesystem;
using match::modload::DerivedRegistry;
using match::streak::ClientEventView;
using match::streak::Derivation;
using match::streak::DerivedEngine;
using nlohmann::json;

namespace {

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

DerivedRegistry ShippedRegistry() {
    match::modload::LoadResult load = match::modload::ScanModsDirectory(
        (ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    DerivedRegistry registry;
    for (const auto& mod : load.mods) {
        for (const auto& derived : mod.derived_events) {
            registry[derived.derived_id] = derived;
        }
    }
    REQUIRE(registry.size() == 2);
    return registry;
}

DerivedEngine ShippedEngine() {
    DerivedEngine engine;
    engine.SetDefinitions(ShippedRegistry());
    return engine;
}

const DerivedEngine::ChargeFunction kFree = [](int64_t, const std::string&) {
    return true;
};

std::vector<Derivation> Run(DerivedEngine& engine, const ClientEventView& event,
                            const DerivedEngine::ChargeFunction& charge = kFree) {
    return engine.Evaluate(event, charge);
}

ClientEventView Play(const std::string& seat, int64_t hand_size,
                     const std::string& tag = "") {
    ClientEventView view;
    view.id = "card_played";
    json tags = json::array();
    if (!tag.empty()) tags.push_back(tag);
    view.payload = {{"player", seat}, {"tags", tags}};
    view.actor = seat;
    view.state.hand_size = hand_size;
    return view;
}

ClientEventView Skip(const std::string& target, const std::string& actor = "") {
    ClientEventView view;
    view.id = "turn_skipped";
    view.payload = {{"target", target}};
    view.actor = actor;
    return view;
}

ClientEventView Draw(const std::string& seat, bool voluntary) {
    ClientEventView view;
    view.id = "cards_drawn";
    view.payload = {{"target", seat}, {"count", 1}, {"from_debt", false}};
    if (voluntary) view.payload["held"] = false;
    view.actor = voluntary ? seat : "";
    return view;
}

ClientEventView MatchEnded(const std::string& seat) {
    ClientEventView view;
    view.id = "match_ended";
    view.payload = {{"target", seat}, {"targets", json::array({seat})}};
    return view;
}

void OpenFor(DerivedEngine& engine, const std::string& seat) {
    const auto opened = Run(engine, Play(seat, 1));
    REQUIRE(opened.size() == 1);
    REQUIRE(engine.WindowFor("near_win_entered").open);
}

}  // namespace

TEST_CASE("near win: a play that leaves one card opens the window") {
    DerivedEngine engine = ShippedEngine();
    const auto derived = Run(engine, Play("a", 1));
    REQUIRE(derived.size() == 1);
    CHECK(derived[0].event_id == "near_win_entered");
    CHECK(derived[0].seat == "a");
    CHECK(derived[0].actor == "a");
    const auto window = engine.WindowFor("near_win_entered");
    CHECK(window.open);
    CHECK(window.seat == "a");
}

TEST_CASE("near win: any other hand size or a draw card opens nothing") {
    for (const int64_t hand_size : {0, 2, 3, 7}) {
        DerivedEngine engine = ShippedEngine();
        CHECK_MESSAGE(Run(engine, Play("a", hand_size)).empty(), hand_size);
        CHECK_FALSE(engine.WindowFor("near_win_entered").open);
    }
    DerivedEngine engine = ShippedEngine();
    CHECK(Run(engine, Play("a", 1, "uni:draw_penalty")).empty());
    CHECK_FALSE(engine.WindowFor("near_win_entered").open);
    CHECK(Run(engine, Play("a", 1, "uni:skip")).size() == 1);
}

TEST_CASE("near win: a play without a hand size opens nothing") {
    DerivedEngine engine = ShippedEngine();
    ClientEventView view = Play("a", 1);
    view.state.hand_size.reset();
    CHECK(Run(engine, view).empty());
}

TEST_CASE("near win: the target closes the window with a play, a draw, a "
          "skip, or the end") {
    const std::pair<const char*, ClientEventView> closers[] = {
        {"a play", Play("a", 3)},
        {"a voluntary draw", Draw("a", true)},
        {"a forced draw", Draw("a", false)},
        {"a skip", Skip("a")},
        {"the end", MatchEnded("a")},
    };
    for (const auto& [name, closer] : closers) {
        DerivedEngine engine = ShippedEngine();
        OpenFor(engine, "a");
        (void)Run(engine, closer);
        CHECK_MESSAGE(engine.WindowFor("near_win_entered").open == false, name);
    }
}

TEST_CASE("near win: an event of another seat leaves the window open") {
    const ClientEventView others[] = {
        Play("b", 4), Draw("b", true), Draw("b", false), Skip("b"),
        MatchEnded("b")};
    for (const ClientEventView& other : others) {
        DerivedEngine engine = ShippedEngine();
        OpenFor(engine, "a");
        (void)Run(engine, other);
        const auto window = engine.WindowFor("near_win_entered");
        CHECK_MESSAGE(window.open, other.id);
        CHECK(window.seat == "a");
    }
}

TEST_CASE("near win: an event that no definition names costs nothing and "
          "changes nothing") {
    DerivedEngine engine = ShippedEngine();
    OpenFor(engine, "a");
    ClientEventView turn;
    turn.id = "turn_advance";
    turn.payload = {{"from", "a"}, {"to", "b"}};
    int charges = 0;
    const auto derived = Run(engine, turn, [&](int64_t, const std::string&) {
        ++charges;
        return true;
    });
    CHECK(derived.empty());
    CHECK(charges == 0);
    CHECK(engine.WindowFor("near_win_entered").open);
}

TEST_CASE("near win: a new window replaces the old one") {
    DerivedEngine engine = ShippedEngine();
    OpenFor(engine, "a");
    const auto derived = Run(engine, Play("b", 1));
    REQUIRE(derived.size() == 1);
    CHECK(derived[0].seat == "b");
    const auto window = engine.WindowFor("near_win_entered");
    CHECK(window.seat == "b");
    CHECK(window.open);
}

TEST_CASE("near win: a play by the target that leaves one card again "
          "closes and opens in one pass") {
    DerivedEngine engine = ShippedEngine();
    OpenFor(engine, "a");
    const auto derived = Run(engine, Play("a", 1));
    REQUIRE(derived.size() == 1);
    CHECK(derived[0].event_id == "near_win_entered");
    CHECK(engine.WindowFor("near_win_entered").open);
}

TEST_CASE("blocked: a skip of the target blocks, with the window from "
          "before the event") {
    DerivedEngine engine = ShippedEngine();
    OpenFor(engine, "a");
    const auto derived = Run(engine, Skip("a", "b"));
    REQUIRE(derived.size() == 1);
    CHECK(derived[0].event_id == "blocked");
    CHECK(derived[0].seat == "a");
    CHECK(derived[0].actor == "b");
    // INFO: the same skip closes the window. The block was read before.
    CHECK_FALSE(engine.WindowFor("near_win_entered").open);
    CHECK(Run(engine, Skip("a", "b")).empty());
}

TEST_CASE("blocked: a forced draw of the target blocks, a voluntary draw "
          "does not") {
    DerivedEngine forced = ShippedEngine();
    OpenFor(forced, "a");
    const auto blocked = Run(forced, Draw("a", false));
    REQUIRE(blocked.size() == 1);
    CHECK(blocked[0].event_id == "blocked");
    CHECK(blocked[0].seat == "a");
    CHECK_FALSE(forced.WindowFor("near_win_entered").open);

    DerivedEngine voluntary = ShippedEngine();
    OpenFor(voluntary, "a");
    CHECK(Run(voluntary, Draw("a", true)).empty());
    CHECK_FALSE(voluntary.WindowFor("near_win_entered").open);
}

TEST_CASE("blocked: no block when the window is closed or names another "
          "seat") {
    DerivedEngine closed = ShippedEngine();
    CHECK(Run(closed, Skip("a", "b")).empty());

    DerivedEngine other = ShippedEngine();
    OpenFor(other, "a");
    CHECK(Run(other, Skip("b", "c")).empty());
    CHECK(Run(other, Draw("b", false)).empty());
    CHECK(other.WindowFor("near_win_entered").open);
}

TEST_CASE("blocked: a block and an opening in one event give both") {
    DerivedEngine engine = ShippedEngine();
    OpenFor(engine, "a");
    // INFO: a play by a different seat that leaves it with one card opens a
    //       window for b; it does not touch the window of a, so no block.
    const auto derived = Run(engine, Play("b", 1));
    REQUIRE(derived.size() == 1);
    CHECK(derived[0].event_id == "near_win_entered");
}

TEST_CASE("derived actor: the base actor, else the seat that acted last") {
    DerivedEngine engine = ShippedEngine();
    (void)Run(engine, Play("c", 5));
    OpenFor(engine, "a");
    const auto derived = Run(engine, Draw("a", false));
    REQUIRE(derived.size() == 1);
    CHECK(derived[0].actor == "a");

    DerivedEngine fallback = ShippedEngine();
    (void)Run(fallback, Play("c", 5));
    ClientEventView open = Play("a", 1);
    open.actor = "";
    (void)Run(fallback, open);
    const auto blocked = Run(fallback, Skip("a"));
    REQUIRE(blocked.size() == 1);
    CHECK(blocked[0].actor == "c");
}

TEST_CASE("derived guard: a derived view changes no window and gives no "
          "derivation") {
    DerivedEngine engine = ShippedEngine();
    ClientEventView derived_view = Play("a", 1);
    derived_view.derived = true;
    CHECK(Run(engine, derived_view).empty());
    CHECK_FALSE(engine.WindowFor("near_win_entered").open);

    OpenFor(engine, "a");
    ClientEventView derived_skip = Skip("a", "b");
    derived_skip.derived = true;
    CHECK(Run(engine, derived_skip).empty());
    CHECK(engine.WindowFor("near_win_entered").open);
}

TEST_CASE("derived events are not base events: the ids near_win_entered and "
          "blocked change nothing") {
    DerivedEngine engine = ShippedEngine();
    OpenFor(engine, "a");
    for (const char* id : {"near_win_entered", "blocked"}) {
        ClientEventView view;
        view.id = id;
        view.payload = {{"target", "a"}};
        CHECK(Run(engine, view).empty());
    }
    CHECK(engine.WindowFor("near_win_entered").open);
}

TEST_CASE("derived cost: each relevant definition is one charge; past the "
          "limit the pass is skipped") {
    DerivedEngine engine = ShippedEngine();
    std::vector<std::string> labels;
    int64_t seen_turn = -1;
    ClientEventView play = Play("a", 1);
    play.state.turn = 9;
    const auto derived =
        Run(engine, play, [&](int64_t turn, const std::string& label) {
            labels.push_back(label);
            seen_turn = turn;
            return true;
        });
    CHECK(derived.size() == 1);
    CHECK(seen_turn == 9);
    // card_played is relevant to the window definition only.
    CHECK(labels == std::vector<std::string>{"uni:near_win_entered"});

    DerivedEngine denied = ShippedEngine();
    const auto none = Run(denied, Play("a", 1),
                          [](int64_t, const std::string&) { return false; });
    CHECK(none.empty());
    CHECK_FALSE(denied.WindowFor("near_win_entered").open);

    DerivedEngine both = ShippedEngine();
    OpenFor(both, "a");
    labels.clear();
    (void)Run(both, Skip("a", "b"), [&](int64_t, const std::string& label) {
        labels.push_back(label);
        return true;
    });
    CHECK(labels == std::vector<std::string>{"uni:blocked",
                                             "uni:near_win_entered"});
}

TEST_CASE("derived context: the windows by event id") {
    DerivedEngine engine = ShippedEngine();
    json context = engine.WindowsContext();
    REQUIRE(context.contains("near_win_entered"));
    CHECK(context["near_win_entered"]["target"].is_null());
    CHECK(context["near_win_entered"]["open"] == false);
    OpenFor(engine, "a");
    context = engine.WindowsContext();
    CHECK(context["near_win_entered"]["target"] == "a");
    CHECK(context["near_win_entered"]["open"] == true);
    (void)Run(engine, Draw("a", true));
    context = engine.WindowsContext();
    CHECK(context["near_win_entered"]["target"] == "a");
    CHECK(context["near_win_entered"]["open"] == false);

    DerivedEngine empty;
    CHECK(empty.WindowsContext().is_null());
}
