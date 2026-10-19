#pragma once

#include <vector>

#include <match/cardschema/model.hpp>

namespace match::cardschema {

// INFO: checks the rules that need every mod at once. A member's attribute must
// be known and extensible for a foreign mod; two members of one attribute from
// two mods must not share a local name; a `type: member` property must name a
// known attribute and one mod must not declare a property twice; a deck entry
// must name a known, concrete template and only slots that template owns; and
// every contribution `when` is checked against the card schema built from the
// card-scope properties and the slot names of all mods.
//
// INFO: reports errors only, each at the owning file and pointer. The catalog
// expansion reports its own diagnostics; ValidateCatalog reports the remaining
// cross-mod rules and reuses ExpandDeck's wording for an unknown or abstract
// deck kind so the caller's DedupeDiagnostics removes the exact repeats.
std::vector<Diagnostic> ValidateCatalog(const std::vector<ModSchema>& mods);

}  // namespace match::cardschema
