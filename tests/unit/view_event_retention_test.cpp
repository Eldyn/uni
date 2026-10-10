#include <doctest/doctest.h>

#include <match/view/event_audience.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

/**
 * @file view_event_retention_test.cpp
 * @brief The retention table that sits beside the audience table.
 */

using match::view::Retention;
using match::view::RetentionOf;
using nlohmann::json;

TEST_CASE("retention: core events declare the agreed retention") {
    CHECK(RetentionOf("card_played", json::object()) == Retention::kLog);
    CHECK(RetentionOf("turn_skipped", json::object()) == Retention::kLog);
    CHECK(RetentionOf("direction_changed", json::object()) == Retention::kLog);
    CHECK(RetentionOf("active_type_changed", json::object())
          == Retention::kLog);
    CHECK(RetentionOf("match_ended", json::object()) == Retention::kLog);
    CHECK(RetentionOf("turn_advance", json::object())
          == Retention::kUntilTurnEnd);
    CHECK(RetentionOf("card_moved", json::object())
          == Retention::kUntilTurnEnd);
    CHECK(RetentionOf("round_advance", json::object())
          == Retention::kUntilRoundEnd);
    CHECK(RetentionOf("placement_set", json::object())
          == Retention::kUntilRoundEnd);
    CHECK(RetentionOf("reshuffle", json::object()) == Retention::kTransient);
    CHECK(RetentionOf("window_opened", json::object())
          == Retention::kTransient);
    CHECK(RetentionOf("turn_timer_changed", json::object())
          == Retention::kTransient);
    CHECK(RetentionOf("prompt_opened", json::object())
          == Retention::kTransient);
}

TEST_CASE("retention: the window, roll and finish events") {
    CHECK(RetentionOf("window_opened", json::object())
          == Retention::kTransient);
    CHECK(RetentionOf("window_responded", json::object())
          == Retention::kUntilTurnEnd);
    CHECK(RetentionOf("window_closed", json::object())
          == Retention::kUntilTurnEnd);
    CHECK(RetentionOf("outcome_rolled", json::object())
          == Retention::kUntilTurnEnd);
    CHECK(RetentionOf("placement_set", json::object())
          == Retention::kUntilRoundEnd);
    CHECK(RetentionOf("match_ended", json::object()) == Retention::kLog);
}

TEST_CASE("retention: a draw follows its payload") {
    CHECK(RetentionOf("cards_drawn", json{{"held", false}})
          == Retention::kUntilTurnEnd);
    CHECK(RetentionOf("cards_drawn", json{{"from_debt", true}})
          == Retention::kLog);
    CHECK(RetentionOf("cards_drawn", json{{"held", true}})
          == Retention::kTransient);
}

TEST_CASE("retention: a type without a row defaults to log (mod seam)") {
    CHECK(RetentionOf("mod_event", json::object()) == Retention::kLog);
    CHECK(RetentionOf("some_mod:thing", json::object()) == Retention::kLog);
}
