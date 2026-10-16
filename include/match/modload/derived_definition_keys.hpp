#pragma once

#include <cstddef>
#include <string_view>

/**
 * @file derived_definition_keys.hpp
 * @brief The key names of a derived event definition file.
 *
 * ONE table. The loader, the validator, the engine, and the tests read the
 * names from here. The JSON schema `contract/schemas/derived.schema.json`
 * repeats the names; a test fails when the schema and this table differ. The
 * matcher keys (`uni:event`, `uni:tests`, `uni:test`, ...) are the keys of
 * `streak_definition_keys.hpp`.
 *
 * A derived event is a core client event that the engine emits after a base
 * client event, when a condition becomes true. It carries a `cause`: the base
 * event. Two kinds:
 *
 * - `window`: `uni:open_on` matchers open a window for the seat named by the
 *   payload field `uni:seat_field`; the engine emits the event. A matcher of
 *   `uni:close_on` closes the window when it names the seat of the window.
 *   Opening a window for a new seat replaces the old window.
 * - `block`: `uni:window_of` names a `window` definition. A matcher of
 *   `uni:block_on` that names the seat of the OPEN window makes the engine
 *   emit the event. The engine reads the window as it was BEFORE the base
 *   event, so an event that blocks and closes the window still blocks.
 *
 * Example file (format version 1):
 *
 *     {
 *       "uni:format_version": "1.0.0",
 *       "uni:derived_event": {
 *         "uni:description": { "uni:identifier": "uni:near_win_entered" },
 *         "uni:emits": "near_win_entered",
 *         "uni:open_on": [ { "uni:event": "card_played",
 *                            "uni:seat_field": "player" } ],
 *         "uni:close_on": [ { "uni:event": "cards_drawn",
 *                             "uni:seat_field": "target" } ]
 *       }
 *     }
 *
 * INFO: derived events are core events. Only the `uni` mod may define them.
 */

namespace match::modload::derived_keys {

inline constexpr std::string_view kDerivedEvent = "uni:derived_event";
inline constexpr std::string_view kEmits = "uni:emits";
inline constexpr std::string_view kOpenOn = "uni:open_on";
inline constexpr std::string_view kCloseOn = "uni:close_on";
inline constexpr std::string_view kBlockOn = "uni:block_on";
inline constexpr std::string_view kWindowOf = "uni:window_of";
inline constexpr std::string_view kSeatField = "uni:seat_field";

inline constexpr std::string_view kWindowKind = "window";
inline constexpr std::string_view kBlockKind = "block";

/** @brief Most seat matchers in one list of a derived event. */
inline constexpr std::size_t kMaxSeatMatchers = 8;

}  // namespace match::modload::derived_keys
