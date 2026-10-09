#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/view/event_tail.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file view_event_tail_test.cpp
 * @brief The store of logical client events: retention, pruning and the cap.
 *
 * The tests build engine descriptors by hand, so they fix the turn and the
 * round that the engine would have stamped at emit time.
 */

using match::view::EventTail;
using nlohmann::json;

namespace {

int64_t next_event_id = 0;

json EngineEvent(const std::string& type, uint32_t turn, uint32_t round = 1,
                 json payload = json::object()) {
    return json{{"type", type},
                {"payload", std::move(payload)},
                {"event_id", next_event_id++},
                {"causal_frame",
                 json{{"turn", turn}, {"round", round}, {"actor", nullptr}}}};
}

std::vector<std::string> StoredTypes(const EventTail& tail) {
    std::vector<std::string> types;
    for (const match::view::TailEntry& entry : tail.Entries()) {
        types.push_back(entry.engine_event["type"].get<std::string>());
    }
    return types;
}

struct EnvOverride {
    std::string key;
    explicit EnvOverride(std::string name, const std::string& value)
        : key(std::move(name)) {
        Env::SetEnv(key, value);
    }
    ~EnvOverride() { Env::SetEnv(key, std::string()); }
};

}  // namespace

TEST_CASE("event tail: the log limit comes from UNI_EVENT_TAIL_MAX") {
    SUBCASE("default") {
        Env::SetEnv("UNI_EVENT_TAIL_MAX", std::string());
        CHECK(EventTail::LimitFromEnv() == 200);
    }
    SUBCASE("override") {
        EnvOverride limit("UNI_EVENT_TAIL_MAX", "7");
        CHECK(EventTail::LimitFromEnv() == 7);
        CHECK(EventTail().Limit() == 7);
    }
    SUBCASE("a negative value falls back to the default") {
        EnvOverride limit("UNI_EVENT_TAIL_MAX", "-3");
        CHECK(EventTail::LimitFromEnv() == 200);
    }
    SUBCASE("trailing junk falls back to the default") {
        EnvOverride limit("UNI_EVENT_TAIL_MAX", "12abc");
        CHECK(EventTail::LimitFromEnv() == 200);
    }
    SUBCASE("text with no digits falls back to the default") {
        EnvOverride limit("UNI_EVENT_TAIL_MAX", "many");
        CHECK(EventTail::LimitFromEnv() == 200);
    }
    SUBCASE("a value above the maximum is clamped to 10000") {
        EnvOverride limit("UNI_EVENT_TAIL_MAX", "20000");
        CHECK(EventTail::LimitFromEnv() == 10000);
    }
    SUBCASE("a huge value is clamped, not wrapped") {
        EnvOverride limit("UNI_EVENT_TAIL_MAX", "99999999999999999999");
        CHECK(EventTail::LimitFromEnv() == 10000);
    }
    SUBCASE("zero means no tail") {
        EnvOverride limit("UNI_EVENT_TAIL_MAX", "0");
        CHECK(EventTail::LimitFromEnv() == 0);
        EventTail tail;
        tail.Record(EngineEvent("card_played", 1));
        tail.Record(EngineEvent("card_moved", 1));
        CHECK(tail.Entries().empty());
    }
}

TEST_CASE("event tail: a turn value that falls does not prune wrongly") {
    EventTail tail(10);
    tail.Record(EngineEvent("card_moved", 9, 1));
    tail.Record(EngineEvent("turn_advance", 10, 1));
    tail.Record(EngineEvent("card_moved", 10, 1));
    tail.Record(EngineEvent("round_advance", 0, 2));
    tail.Record(EngineEvent("card_moved", 0, 2));
    tail.Record(EngineEvent("card_moved", 0, 2));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"round_advance", "card_moved",
                                      "card_moved"});
    tail.Record(EngineEvent("turn_advance", 1, 2));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"round_advance", "turn_advance"});
}

TEST_CASE("event tail: a transient event is never stored") {
    EventTail tail(10);
    tail.Record(EngineEvent("reshuffle", 1));
    tail.Record(EngineEvent("window_opened", 1));
    tail.Record(EngineEvent("window_open", 1));
    CHECK(tail.Entries().empty());
}

TEST_CASE("event tail: the old name of an alias pair is not stored") {
    EventTail tail(10);
    tail.Record(EngineEvent("match_end", 3));
    tail.Record(EngineEvent("match_ended", 3));
    CHECK(StoredTypes(tail) == std::vector<std::string>{"match_ended"});
}

TEST_CASE("event tail: until_turn_end drops at the next turn counter value") {
    EventTail tail(10);
    tail.Record(EngineEvent("card_moved", 4));
    tail.Record(EngineEvent("card_played", 4));
    tail.Record(EngineEvent("turn_advance", 5));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"card_played", "turn_advance"});
}

TEST_CASE("event tail: a jump-in raises the counter and ends the turn") {
    EventTail tail(10);
    tail.Record(EngineEvent("card_moved", 4));
    tail.Record(EngineEvent("turn_advance", 5));
    tail.Record(EngineEvent("card_moved", 5));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"turn_advance", "card_moved"});
}

TEST_CASE("event tail: an extra turn for the same seat also ends the turn") {
    EventTail tail(10);
    tail.Record(EngineEvent("cards_drawn", 6, 1, json{{"target", "a"}}));
    tail.Record(EngineEvent("extra_turn_granted", 6));
    tail.Record(EngineEvent("turn_advance", 7,
                            1, json{{"from", "a"}, {"to", "a"}}));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"extra_turn_granted", "turn_advance"});
}

TEST_CASE("event tail: a transient event of a new turn still ends the turn") {
    EventTail tail(10);
    tail.Record(EngineEvent("card_moved", 2));
    tail.Record(EngineEvent("reshuffle", 3));
    CHECK(tail.Entries().empty());
}

TEST_CASE("event tail: until_round_end survives turns, not the round") {
    EventTail tail(10);
    tail.Record(EngineEvent("restriction_added", 2, 1));
    tail.Record(EngineEvent("turn_advance", 3, 1));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"restriction_added", "turn_advance"});
    tail.Record(EngineEvent("round_advance", 4, 2));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"round_advance"});
}

TEST_CASE("event tail: log events survive turns and rounds") {
    EventTail tail(10);
    tail.Record(EngineEvent("card_played", 1, 1));
    tail.Record(EngineEvent("round_advance", 9, 2));
    tail.Record(EngineEvent("turn_advance", 10, 2));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"card_played", "round_advance",
                                      "turn_advance"});
}

TEST_CASE("event tail: the log keeps the newest events up to the limit") {
    EventTail tail(3);
    for (uint32_t turn = 1; turn <= 5; ++turn) {
        tail.Record(EngineEvent("card_played", turn, 1,
                                json{{"order", turn}}));
    }
    REQUIRE(tail.Entries().size() == 3);
    CHECK(tail.Entries()[0].engine_event["payload"]["order"] == 3);
    CHECK(tail.Entries()[2].engine_event["payload"]["order"] == 5);
}

TEST_CASE("event tail: the log limit does not evict a turn event") {
    EventTail tail(2);
    tail.Record(EngineEvent("card_moved", 1));
    for (uint32_t index = 0; index < 5; ++index) {
        tail.Record(EngineEvent("card_played", 1));
    }
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"card_moved", "card_played",
                                      "card_played"});
}

TEST_CASE("event tail: every class stays bounded by the limit") {
    EventTail tail(4);
    for (uint32_t index = 0; index < 50; ++index) {
        tail.Record(EngineEvent("card_moved", 1));
        tail.Record(EngineEvent("restriction_added", 1, 1));
    }
    CHECK(tail.Entries().size() <= 8);
}

TEST_CASE("event tail: a draw follows its payload") {
    EventTail tail(10);
    tail.Record(EngineEvent("cards_drawn", 1, 1, json{{"held", true}}));
    CHECK(tail.Entries().empty());
    tail.Record(EngineEvent("cards_drawn", 1, 1, json{{"from_debt", true}}));
    CHECK(tail.Entries().size() == 1);
}

TEST_CASE("event tail: a mod event with no row is kept as log") {
    EventTail tail(10);
    tail.Record(EngineEvent("mod_event", 1));
    tail.Record(EngineEvent("turn_advance", 2));
    CHECK(StoredTypes(tail)
          == std::vector<std::string>{"mod_event", "turn_advance"});
}
