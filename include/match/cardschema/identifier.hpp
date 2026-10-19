#pragma once

#include <map>
#include <string>

#include <match/cardschema/model.hpp>

namespace match::cardschema {

// INFO: `slot_members` maps a slot name to its member LOCAL name, never a full
// identifier, so the sort key is the plain byte order of the names the caller
// passes. Within each symmetric group of `kind` the present member names are
// sorted and assigned back to the group's slots in declared order; every other
// slot is left as it came in. A group reads the map as it evolves, so a slot in
// two groups is written by the earlier group before the later one reads it;
// groups should be disjoint and the loader rejects overlapping ones. A partial
// assignment is canonicalized over the slots it holds only, so callers pass
// complete assignments.
std::map<std::string, std::string> CanonicalizeSymmetric(
    const KindTemplate& kind, const std::map<std::string, std::string>& slot_members);

// INFO: true when two assignments describe one card (the same canonical form).
bool SameCard(const KindTemplate& kind, const std::map<std::string, std::string>& a,
              const std::map<std::string, std::string>& b);

}  // namespace match::cardschema
