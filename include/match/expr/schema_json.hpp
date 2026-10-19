#pragma once

#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include <match/expr/types.hpp>

namespace match::expr {

using json = nlohmann::json;

// INFO: builds the Schema the conformance corpus and the verifier share. The
// JSON keys are `seat_properties`, `card_properties`, `match_properties` and
// `settings` (an object from a property key to a type name), `card_slots` (an
// array of slot names, each a member set) and `variables` (an object from a
// name to `{ "type": ..., "optional": ... }`). The type names are `bool`,
// `int`, `float`, `string`, `member_set`, `seat`, `seat_set`, `card` and
// `card_set`. On a malformed key `error` describes it and the result is empty.
std::optional<Schema> LoadSchema(const json& value, std::string& error);

}  // namespace match::expr
