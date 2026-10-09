#pragma once

#include <array>
#include <cstddef>
#include <string_view>

/**
 * @file event_definition_keys.hpp
 * @brief The key names and the closed word lists of an event definition file.
 *
 * ONE table. The loader, the validator, and the tests read the names from
 * here, so a rename in spec B changes this file only. The JSON schema
 * `contract/schemas/events.schema.json` repeats the names; a test fails when
 * the schema and this table differ.
 *
 * WARN: the event id, the retention, the timing of the event, and the field
 * NAMES and TYPES (the `defs` manifest) are public to every client. Do not
 * put a hidden fact in them. Only the field VALUES follow the audiences.
 *
 * Example file (format version 1):
 *
 *     {
 *       "uni:format_version": "1.0.0",
 *       "uni:event": {
 *         "uni:description": { "uni:identifier": "my_mod:apple_eaten" },
 *         "uni:retention": "log",
 *         "uni:payload": { "uni:fields": {
 *           "target": { "uni:type": "seat", "uni:audience": "all" } } },
 *         "uni:context": ["uni:skip_streak"]
 *       }
 *     }
 */

namespace match::modload::event_keys {

inline constexpr std::string_view kFormatVersion = "uni:format_version";
inline constexpr std::string_view kEvent = "uni:event";
inline constexpr std::string_view kDescription = "uni:description";
inline constexpr std::string_view kIdentifier = "uni:identifier";
inline constexpr std::string_view kRetention = "uni:retention";
inline constexpr std::string_view kPayload = "uni:payload";
inline constexpr std::string_view kFields = "uni:fields";
inline constexpr std::string_view kType = "uni:type";
inline constexpr std::string_view kAudience = "uni:audience";
inline constexpr std::string_view kContext = "uni:context";

/** @brief The major version of the file style that this build reads. */
inline constexpr int kSupportedFormatMajor = 1;

/** @brief Payload field names that name a seat role; their type is `seat`. */
inline constexpr std::string_view kTargetRole = "target";
inline constexpr std::string_view kOwnerRole = "owner";
inline constexpr std::string_view kSeatType = "seat";

/** @brief Longest string value of a payload field, in bytes. */
inline constexpr std::size_t kMaxEventStringBytes = 256;

/** @brief Longest serialized payload of one mod event, in bytes. */
inline constexpr std::size_t kMaxEventPayloadBytes = 1024;

/** @brief Most payload fields one event definition may declare. */
inline constexpr std::size_t kMaxEventFields = 16;

/** @brief What a mod event does when the file sets no retention. */
inline constexpr std::string_view kDefaultRetention = "log";

/** @brief What a field gets when the file sets no audience. */
inline constexpr std::string_view kDefaultAudience = "none";

inline constexpr std::array<std::string_view, 4> kRetentions = {
    "transient", "until_turn_end", "until_round_end", "log"};

inline constexpr std::array<std::string_view, 5> kAudiences = {
    "all", "actor", "target", "owner", "none"};

inline constexpr std::array<std::string_view, 6> kFieldTypes = {
    "seat", "color", "integer", "number", "boolean", "string"};

}  // namespace match::modload::event_keys
