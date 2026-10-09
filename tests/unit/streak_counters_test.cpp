#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>
#include <match/streak/streak_engine.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <utility>

/**
 * @file streak_counters_test.cpp
 * @brief The shipped `uni` draw, total draws, wild run, and wild color
 *        streaks: the count, each reset event, and the stored colors.
 *
 * The test feeds hand-made client events to the real definitions of
 * `mods/uni/streaks`, so every reset matcher is read by a test.
 */

namespace fs = std::filesystem;
using match::modload::LoadedMod;
using match::modload::StreakRegistry;
using match::streak::ClientEventView;
using match::streak::StreakEngine;
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

StreakEngine ShippedEngine() {
    match::modload::LoadResult load = match::modload::ScanModsDirectory(
        (ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    StreakRegistry registry;
    for (const LoadedMod& mod : load.mods) {
        if (mod.manifest.id != "uni") continue;
        for (const auto& streak : mod.streaks) {
            registry[streak.streak_id] = streak;
        }
    }
    StreakEngine engine;
    engine.SetDefinitions(registry);
    return engine;
}

ClientEventView View(const std::string& id, json payload = json::object()) {
    ClientEventView view;
    view.id = id;
    view.payload = std::move(payload);
    return view;
}

ClientEventView Draw(const std::string& seat) {
    return View("cards_drawn", {{"target", seat}, {"count", 1}});
}

ClientEventView Play(const std::string& tag = "") {
    json tags = json::array();
    if (!tag.empty()) tags.push_back(tag);
    return View("card_played", {{"player", "a"}, {"tags", tags}});
}

ClientEventView WildColor(const std::string& color) {
    return View("active_type_changed", {{"color", color}});
}

int Number(const StreakEngine& engine, const char* streak_id) {
    return engine.ValueOf(streak_id).get<int>();
}

}  // namespace

TEST_CASE("draw run: a lifetime count for each seat that no event resets") {
    StreakEngine engine = ShippedEngine();
    engine.Observe(Draw("a"));
    engine.Observe(Draw("a"));
    engine.Observe(Draw("b"));
    engine.Observe(Play());
    engine.Observe(View("turn_skipped", {{"target", "a"}}));
    engine.Observe(View("drawn_card_kept", {{"target", "a"}}));
    engine.Observe(Draw("a"));
    const json value = engine.ValueOf("uni:draw_run");
    CHECK(value["a"] == 3);
    CHECK(value["b"] == 1);
    CHECK(value.size() == 2);
}

TEST_CASE("total draws: counts every draw and only grows") {
    StreakEngine engine = ShippedEngine();
    int previous = 0;
    for (const ClientEventView& event :
         {Draw("a"), Play(), Draw("b"), View("direction_changed"),
          View("turn_skipped", {{"target", "a"}}), Draw("a"),
          View("drawn_card_kept", {{"target", "a"}}), WildColor("red")}) {
        engine.Observe(event);
        const int total = Number(engine, "uni:total_draws");
        CHECK(total >= previous);
        previous = total;
    }
    CHECK(previous == 3);
}

TEST_CASE("wild run: a color choice counts and a wild card keeps the run") {
    StreakEngine engine = ShippedEngine();
    engine.Observe(Play("uni:wild"));
    engine.Observe(WildColor("red"));
    CHECK(Number(engine, "uni:wild_run") == 1);
    engine.Observe(Play("uni:wild"));
    engine.Observe(WildColor("blue"));
    CHECK(Number(engine, "uni:wild_run") == 2);
    engine.Observe(Play("uni:wild"));
    CHECK(Number(engine, "uni:wild_run") == 2);
}

TEST_CASE("wild run: each of the five events resets it") {
    const std::pair<const char*, ClientEventView> resets[] = {
        {"a plain card", Play("uni:skip")},
        {"a draw", Draw("a")},
        {"a keep", View("drawn_card_kept", {{"target", "a"}})},
        {"a skip", View("turn_skipped", {{"target", "a"}})},
        {"a reverse", View("direction_changed")},
    };
    for (const auto& [name, reset] : resets) {
        StreakEngine engine = ShippedEngine();
        engine.Observe(WildColor("red"));
        engine.Observe(WildColor("blue"));
        REQUIRE(Number(engine, "uni:wild_run") == 2);
        engine.Observe(reset);
        CHECK_MESSAGE(Number(engine, "uni:wild_run") == 0, name);
        engine.Observe(WildColor("green"));
        CHECK_MESSAGE(Number(engine, "uni:wild_run") == 1, name);
    }
}

TEST_CASE("wild colors: the last color and the one before, nothing else "
          "changes them") {
    StreakEngine engine = ShippedEngine();
    CHECK(engine.ValueOf("uni:last_wild_color")["last"].is_null());
    CHECK(engine.ValueOf("uni:last_wild_color")["previous"].is_null());
    engine.Observe(WildColor("red"));
    engine.Observe(Draw("a"));
    engine.Observe(Play());
    CHECK(engine.ValueOf("uni:last_wild_color")["last"] == "red");
    CHECK(engine.ValueOf("uni:last_wild_color")["previous"].is_null());
    engine.Observe(WildColor("blue"));
    engine.Observe(WildColor("green"));
    CHECK(engine.ValueOf("uni:last_wild_color")["last"] == "green");
    CHECK(engine.ValueOf("uni:last_wild_color")["previous"] == "blue");
}

TEST_CASE("counters: the shipped mod defines each of the six streaks") {
    const StreakEngine engine = ShippedEngine();
    for (const char* streak_id :
         {"uni:skip_run", "uni:reverse_run", "uni:draw_run",
          "uni:total_draws", "uni:wild_run", "uni:last_wild_color"}) {
        CHECK_MESSAGE(engine.Has(streak_id), streak_id);
    }
}
