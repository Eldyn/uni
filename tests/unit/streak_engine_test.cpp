#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/modload/artifacts.hpp>
#include <match/streak/streak_engine.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/**
 * @file streak_engine_test.cpp
 * @brief The streak engine: run, total, and last_value semantics, grouping,
 *        reset events, the derived-event guard, determinism, and the cost and
 *        context size limits.
 */

using match::modload::StreakDef;
using match::modload::StreakMatcherDef;
using match::modload::StreakRegistry;
using match::modload::StreakTestDef;
using match::streak::ClientEventView;
using match::streak::StreakEngine;
using match::streak::StreakLimits;
using nlohmann::json;

namespace {

struct ConsoleCapture {
    std::ostringstream text;
    std::streambuf* saved;
    ConsoleCapture() : saved(std::cout.rdbuf(text.rdbuf())) {}
    ~ConsoleCapture() { std::cout.rdbuf(saved); }
    bool Has(const std::string& needle) const {
        return text.str().find(needle) != std::string::npos;
    }
};

struct EnvOverride {
    std::string key;
    EnvOverride(std::string name, const std::string& value)
        : key(std::move(name)) {
        Env::SetEnv(key, value);
    }
    ~EnvOverride() { Env::SetEnv(key, std::string()); }
};

StreakDef Streak(const std::string& id, const std::string& type,
                 const std::string& event) {
    StreakDef streak;
    streak.streak_id = "uni:" + id;
    streak.namespace_id = "uni";
    streak.id = id;
    streak.type = type;
    streak.matcher.event = event;
    return streak;
}

StreakMatcherDef Event(const std::string& event) {
    StreakMatcherDef matcher;
    matcher.event = event;
    return matcher;
}

ClientEventView View(const std::string& id, json payload = json::object(),
                     int64_t turn = 1) {
    ClientEventView view;
    view.id = id;
    view.payload = std::move(payload);
    view.state.turn = turn;
    return view;
}

StreakEngine Engine(std::vector<StreakDef> streaks,
                    StreakLimits limits = StreakLimits{}) {
    StreakRegistry registry;
    for (StreakDef& streak : streaks) {
        registry[streak.streak_id] = std::move(streak);
    }
    StreakEngine engine(limits);
    engine.SetDefinitions(registry);
    return engine;
}

}  // namespace

TEST_CASE("streak limits: the defaults and the strict parse") {
    {
        const StreakLimits limits = StreakLimits::FromEnv();
        CHECK(limits.cost_max_per_turn == 2048);
        CHECK(limits.context_bytes_max == 512);
    }
    {
        EnvOverride cost("UNI_STREAK_COST_MAX", "7");
        EnvOverride size("UNI_CONTEXT_SIZE_MAX", "99");
        const StreakLimits limits = StreakLimits::FromEnv();
        CHECK(limits.cost_max_per_turn == 7);
        CHECK(limits.context_bytes_max == 99);
    }
    for (const char* bad : {"12abc", "-4", "abc", " "}) {
        ConsoleCapture console;
        EnvOverride cost("UNI_STREAK_COST_MAX", bad);
        EnvOverride size("UNI_CONTEXT_SIZE_MAX", bad);
        const StreakLimits limits = StreakLimits::FromEnv();
        CHECK_MESSAGE(limits.cost_max_per_turn == 2048, bad);
        CHECK_MESSAGE(limits.context_bytes_max == 512, bad);
    }
    {
        EnvOverride cost("UNI_STREAK_COST_MAX", "999999999999");
        EnvOverride size("UNI_CONTEXT_SIZE_MAX", "999999999999");
        const StreakLimits limits = StreakLimits::FromEnv();
        CHECK(limits.cost_max_per_turn == StreakLimits::kMaxCost);
        CHECK(limits.context_bytes_max == StreakLimits::kMaxContextBytes);
    }
}

TEST_CASE("streak run: counts consecutive matches and a reset event clears") {
    StreakDef run = Streak("reverse_run", "run", "direction_changed");
    run.reset_on = {Event("cards_drawn")};
    StreakEngine engine = Engine({run});
    CHECK(engine.ValueOf("uni:reverse_run") == 0);
    engine.Observe(View("direction_changed"));
    engine.Observe(View("direction_changed"));
    CHECK(engine.ValueOf("uni:reverse_run") == 2);
    engine.Observe(View("turn_advance"));
    CHECK(engine.ValueOf("uni:reverse_run") == 2);
    engine.Observe(View("cards_drawn"));
    CHECK(engine.ValueOf("uni:reverse_run") == 0);
    engine.Observe(View("direction_changed"));
    CHECK(engine.ValueOf("uni:reverse_run") == 1);
}

TEST_CASE("streak run: a different group starts the count again at one") {
    StreakDef run = Streak("skip_run", "run", "turn_skipped");
    run.group_by = "target";
    run.reset_on = {Event("cards_drawn")};
    StreakEngine engine = Engine({run});
    engine.Observe(View("turn_skipped", {{"target", "ann"}}));
    CHECK(engine.ValueOf("uni:skip_run") == 1);
    engine.Observe(View("turn_skipped", {{"target", "ann"}}));
    CHECK(engine.ValueOf("uni:skip_run") == 2);
    engine.Observe(View("turn_skipped", {{"target", "bob"}}));
    CHECK(engine.ValueOf("uni:skip_run") == 1);
    engine.Observe(View("turn_skipped", {{"target", "ann"}}));
    CHECK(engine.ValueOf("uni:skip_run") == 1);
    engine.Observe(View("cards_drawn"));
    engine.Observe(View("turn_skipped", {{"target", "ann"}}));
    CHECK(engine.ValueOf("uni:skip_run") == 1);
}

TEST_CASE("streak run: a reset matcher can test the played card tags") {
    StreakDef run = Streak("skip_run", "run", "turn_skipped");
    StreakMatcherDef reset = Event("card_played");
    StreakTestDef test;
    test.test = "has_tag";
    test.tag = "uni:skip";
    test.negate = true;
    reset.tests = {test};
    run.reset_on = {reset};
    StreakEngine engine = Engine({run});
    engine.Observe(View("turn_skipped"));
    engine.Observe(View("card_played", {{"tags", json::array({"uni:skip"})}}));
    CHECK(engine.ValueOf("uni:skip_run") == 1);
    engine.Observe(View("card_played", {{"tags", json::array({"colored"})}}));
    CHECK(engine.ValueOf("uni:skip_run") == 0);
}

TEST_CASE("streak total: counts every match, for each group when grouped") {
    StreakDef all = Streak("all_skips", "total", "turn_skipped");
    StreakDef grouped = Streak("skips_by_seat", "total", "turn_skipped");
    grouped.group_by = "target";
    StreakEngine engine = Engine({all, grouped});
    engine.Observe(View("turn_skipped", {{"target", "ann"}}));
    engine.Observe(View("direction_changed"));
    engine.Observe(View("turn_skipped", {{"target", "bob"}}));
    engine.Observe(View("turn_skipped", {{"target", "ann"}}));
    CHECK(engine.ValueOf("uni:all_skips") == 3);
    CHECK(engine.ValueOf("uni:skips_by_seat")
          == json{{"ann", 2}, {"bob", 1}});
}

TEST_CASE("streak last_value: keeps the last value and the one before") {
    StreakDef last = Streak("wild_color", "last_value", "active_type_changed");
    last.value_field = "color";
    StreakEngine engine = Engine({last});
    CHECK(engine.ValueOf("uni:wild_color")
          == json{{"last", nullptr}, {"previous", nullptr}});
    engine.Observe(View("active_type_changed", {{"color", "red"}}));
    engine.Observe(View("active_type_changed", {{"color", "blue"}}));
    CHECK(engine.ValueOf("uni:wild_color")
          == json{{"last", "blue"}, {"previous", "red"}});
}

TEST_CASE("streak engine: a derived event does not feed a streak") {
    StreakDef run = Streak("reverse_run", "run", "direction_changed");
    StreakEngine engine = Engine({run});
    ClientEventView derived = View("direction_changed");
    derived.derived = true;
    engine.Observe(derived);
    engine.Observe(derived);
    CHECK(engine.ValueOf("uni:reverse_run") == 0);
    CHECK(engine.EvaluationsInTurn(1) == 0);
    engine.Observe(View("direction_changed"));
    CHECK(engine.ValueOf("uni:reverse_run") == 1);
}

TEST_CASE("streak engine: an event that no streak reads costs nothing") {
    StreakEngine engine =
        Engine({Streak("reverse_run", "run", "direction_changed")});
    engine.Observe(View("turn_advance"));
    CHECK(engine.EvaluationsInTurn(1) == 0);
    engine.Observe(View("direction_changed"));
    CHECK(engine.EvaluationsInTurn(1) == 1);
}

TEST_CASE("streak engine: the same events give the same values") {
    const auto run_once = [] {
        StreakDef run = Streak("skip_run", "run", "turn_skipped");
        run.group_by = "target";
        StreakDef total = Streak("skips", "total", "turn_skipped");
        StreakEngine engine = Engine({run, total});
        for (const char* seat : {"ann", "ann", "bob", "ann", "ann"}) {
            engine.Observe(View("turn_skipped", {{"target", seat}}));
        }
        return engine.AllValues("uni");
    };
    CHECK(run_once().dump() == run_once().dump());
    CHECK(run_once() == json{{"uni:skip_run", 2}, {"uni:skips", 5}});
}

TEST_CASE("streak cost: the streaks past the limit are skipped and logged") {
    StreakLimits limits;
    limits.cost_max_per_turn = 3;
    StreakEngine engine = Engine({Streak("a_run", "run", "turn_skipped"),
                                  Streak("b_run", "run", "turn_skipped"),
                                  Streak("c_run", "run", "turn_skipped"),
                                  Streak("d_run", "run", "turn_skipped")},
                                 limits);
    ConsoleCapture console;
    engine.Observe(View("turn_skipped", json::object(), 1));
    CHECK(engine.ValueOf("uni:a_run") == 1);
    CHECK(engine.ValueOf("uni:b_run") == 1);
    CHECK(engine.ValueOf("uni:c_run") == 1);
    CHECK(engine.ValueOf("uni:d_run") == 0);
    CHECK(engine.EvaluationsInTurn(1) == 3);
    CHECK(console.Has("uni:d_run"));
    CHECK(console.Has("UNI_STREAK_COST_MAX"));
    engine.Observe(View("turn_skipped", json::object(), 2));
    CHECK(engine.ValueOf("uni:a_run") == 2);
    CHECK(engine.ValueOf("uni:d_run") == 0);
}

TEST_CASE("streak cost: the skip is logged one time for each streak and turn") {
    StreakLimits limits;
    limits.cost_max_per_turn = 1;
    StreakEngine engine = Engine({Streak("a_run", "run", "turn_skipped"),
                                  Streak("b_run", "run", "turn_skipped")},
                                 limits);
    ConsoleCapture console;
    for (int count = 0; count < 5; ++count) {
        engine.Observe(View("turn_skipped"));
    }
    const std::string text = console.text.str();
    std::size_t lines = 0;
    for (std::size_t at = text.find("uni:b_run"); at != std::string::npos;
         at = text.find("uni:b_run", at + 1)) {
        ++lines;
    }
    CHECK(lines == 1);
}

TEST_CASE("streak context: the object names only the requested streaks") {
    StreakEngine engine =
        Engine({Streak("a_run", "run", "turn_skipped"),
                Streak("b_run", "run", "direction_changed")});
    engine.Observe(View("turn_skipped"));
    engine.Observe(View("direction_changed"));
    engine.Observe(View("direction_changed"));
    CHECK(engine.ContextFor({"uni:b_run"}) == json{{"uni:b_run", 2}});
    CHECK(engine.ContextFor({"uni:b_run", "uni:missing", "uni:a_run"})
          == json{{"uni:b_run", 2}, {"uni:a_run", 1}});
    CHECK(engine.ContextFor({}).is_null());
    CHECK(engine.ContextFor({"uni:missing"}).is_null());
}

TEST_CASE("streak context: the size limit drops the extra values and logs") {
    StreakLimits limits;
    limits.context_bytes_max = 30;
    StreakEngine engine = Engine({Streak("a_run", "run", "turn_skipped"),
                                  Streak("b_run", "run", "turn_skipped"),
                                  Streak("c_run", "run", "turn_skipped")},
                                 limits);
    engine.Observe(View("turn_skipped"));
    ConsoleCapture console;
    const json context =
        engine.ContextFor({"uni:a_run", "uni:b_run", "uni:c_run"});
    CHECK(context.dump().size() <= 30);
    CHECK(context.contains("uni:a_run"));
    CHECK_FALSE(context.contains("uni:c_run"));
    CHECK(console.Has("UNI_CONTEXT_SIZE_MAX"));
}

TEST_CASE("streak engine: a hidden field cannot feed a streak") {
    // INFO: the validator rejects such a streak; the engine reads the view,
    //       and the view holds `all` fields only, so a value that the view
    //       lacks cannot show up in a value.
    StreakDef last = Streak("secret_last", "last_value", "mod_thing");
    last.value_field = "secret";
    StreakEngine engine = Engine({last});
    engine.Observe(View("mod_thing", {{"color", "red"}}));
    CHECK(engine.ValueOf("uni:secret_last")
          == json{{"last", nullptr}, {"previous", nullptr}});
}

TEST_CASE("streak cost: a skipped streak is left out of the context") {
    StreakLimits limits;
    limits.cost_max_per_turn = 1;
    StreakEngine engine = Engine({Streak("a_run", "run", "turn_skipped"),
                                  Streak("b_run", "run", "turn_skipped")},
                                 limits);
    ConsoleCapture console;
    engine.Observe(View("turn_skipped", json::object(), 1));
    const json context =
        engine.ContextFor({"uni:a_run", "uni:b_run"});
    REQUIRE(context.is_object());
    CHECK(context.contains("uni:a_run"));
    CHECK_FALSE(context.contains("uni:b_run"));
    engine.Observe(View("turn_skipped", json::object(), 2));
    CHECK(engine.ContextFor({"uni:a_run"}).contains("uni:a_run"));
}

TEST_CASE("streak snapshot: values of every namespace are listed") {
    StreakDef other = Streak("blink", "run", "turn_skipped");
    other.streak_id = "other:blink";
    other.namespace_id = "other";
    StreakEngine engine =
        Engine({Streak("a_run", "run", "turn_skipped"), other});
    const json all = engine.AllValues();
    CHECK(all.contains("uni:a_run"));
    CHECK(all.contains("other:blink"));
}
