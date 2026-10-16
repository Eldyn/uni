#pragma once

#include <array>
#include <cstddef>
#include <string_view>

/**
 * @file streak_definition_keys.hpp
 * @brief The key names and the closed word lists of a streak definition file.
 *
 * ONE table. The loader, the validator, the engine, and the tests read the
 * names from here, so a rename in spec B changes this file only. The JSON
 * schema `contract/schemas/streaks.schema.json` repeats the names; a test
 * fails when the schema and this table differ.
 *
 * A streak is a counter that the engine updates from client events. Types:
 *
 * - `run`: counts consecutive matches. With `uni:group_by`, a match of a
 *   different group starts the count again at 1. A `uni:reset_on` event sets
 *   the count to 0.
 * - `total`: counts all matches (for each group, when `uni:group_by` is set).
 * - `last_value`: stores `uni:value_field` of the last match and of the one
 *   before it.
 *
 * WARN: a streak value is public to every seat (it goes into `context`). A
 * matcher, `uni:group_by`, and `uni:value_field` may read a payload field
 * with the audience `all` only. The validator checks this.
 *
 * Example file (format version 1):
 *
 *     {
 *       "uni:format_version": "1.0.0",
 *       "uni:streak": {
 *         "uni:description": { "uni:identifier": "uni:skip_run" },
 *         "uni:type": "run",
 *         "uni:matcher": { "uni:event": "turn_skipped" },
 *         "uni:group_by": "target",
 *         "uni:reset_on": [ { "uni:event": "cards_drawn" } ]
 *       }
 *     }
 *
 * A matcher is `{ "uni:event": <client event id>, "uni:tests": [...] }`. A
 * test is `{ "uni:test": <word>, ... }` with the parameters of its word. Every
 * test can set `"uni:negate": true`. All tests of a matcher must be true.
 */

namespace match::modload::streak_keys {

inline constexpr std::string_view kFormatVersion = "uni:format_version";
inline constexpr std::string_view kStreak = "uni:streak";
inline constexpr std::string_view kDescription = "uni:description";
inline constexpr std::string_view kIdentifier = "uni:identifier";
inline constexpr std::string_view kType = "uni:type";
inline constexpr std::string_view kMatcher = "uni:matcher";
inline constexpr std::string_view kGroupBy = "uni:group_by";
inline constexpr std::string_view kValueField = "uni:value_field";
inline constexpr std::string_view kResetOn = "uni:reset_on";

inline constexpr std::string_view kMatcherEvent = "uni:event";
inline constexpr std::string_view kTests = "uni:tests";
inline constexpr std::string_view kTest = "uni:test";
inline constexpr std::string_view kField = "uni:field";
inline constexpr std::string_view kState = "uni:state";
inline constexpr std::string_view kValue = "uni:value";
inline constexpr std::string_view kValues = "uni:values";
inline constexpr std::string_view kTag = "uni:tag";
inline constexpr std::string_view kRelation = "uni:relation";
inline constexpr std::string_view kNegate = "uni:negate";

inline constexpr std::string_view kRun = "run";
inline constexpr std::string_view kTotal = "total";
inline constexpr std::string_view kLastValue = "last_value";

inline constexpr std::string_view kEquals = "equals";
inline constexpr std::string_view kIn = "in";
inline constexpr std::string_view kExists = "exists";
inline constexpr std::string_view kHasTag = "has_tag";
inline constexpr std::string_view kRelationTest = "relation";

/** @brief The payload field that carries the tags of a played card. */
inline constexpr std::string_view kTagsField = "tags";

inline constexpr std::array<std::string_view, 3> kStreakTypes = {
    kRun, kTotal, kLastValue};

inline constexpr std::array<std::string_view, 5> kMatcherTests = {
    kEquals, kIn, kExists, kHasTag, kRelationTest};

/** @brief Most tests in one matcher. */
inline constexpr std::size_t kMaxMatcherTests = 8;

/** @brief Most `uni:reset_on` matchers in one streak. */
inline constexpr std::size_t kMaxResetMatchers = 8;

}  // namespace match::modload::streak_keys
