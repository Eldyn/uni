#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <match/expr/diagnostic.hpp>
#include <match/expr/types.hpp>

namespace match::expr {

using json = nlohmann::json;

// INFO: a key that occurs more than once inside one JSON object, with the RFC
// 6901 pointer of the object that holds it. nlohmann keeps the last value of a
// repeated key, so a schema with a duplicate loses data unless the repeat is
// reported.
struct DuplicateKey {
    std::string pointer;
    std::string key;
};

// INFO: parses `text` as JSON and records every key repeated inside one object;
// returns an empty optional when the text is not valid JSON.
std::optional<json> ParseSchemaJson(const std::string& text,
                                    std::vector<DuplicateKey>& duplicates);

// INFO: builds the Schema the conformance corpus and the verifier share. The
// JSON keys are `seat_properties`, `card_properties`, `match_properties` and
// `settings` (an object from a property key to a type name), `card_slots` (an
// array of slot names, each a member set) and `variables` (an object from a
// name to `{ "type": ..., "optional": ... }`). The type names are `bool`,
// `int`, `float`, `string`, `member_set`, `seat`, `seat_set`, `card` and
// `card_set`. Every key outside the allowed set is rejected, at the top level
// and inside a variable object, so a typo is never silently dropped.
// `diagnostics` collects every problem with a message, an optional suggestion
// and an RFC 6901 pointer under `file_label`; it is empty exactly when a schema
// is returned.
std::optional<Schema> LoadSchema(const json& value, std::string_view file_label,
                                 std::vector<Diagnostic>& diagnostics);

// INFO: also reports every key listed in `duplicates` as a diagnostic, with the
// pointer of the object that holds it.
std::optional<Schema> LoadSchema(const json& value, std::string_view file_label,
                                 const std::vector<DuplicateKey>& duplicates,
                                 std::vector<Diagnostic>& diagnostics);

// INFO: the readable-error overload for callers that only need one message; the
// schema is still built by the same loader.
std::optional<Schema> LoadSchema(const json& value, std::string& error);

}  // namespace match::expr
