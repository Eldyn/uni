#pragma once

#include <map>
#include <string>

#include <match/cardschema/model.hpp>

namespace match::cardschema {

// INFO: `slot_members` maps a slot name to its member local name. Within each
// symmetric group of `kind` the member names are sorted alphabetically and
// assigned back to the group's slots in declared order; every other slot is
// left as it came in.
std::map<std::string, std::string> CanonicalizeSymmetric(
    const KindTemplate& kind, const std::map<std::string, std::string>& slot_members);

// INFO: true when two assignments describe one card (the same canonical form).
bool SameCard(const KindTemplate& kind, const std::map<std::string, std::string>& a,
              const std::map<std::string, std::string>& b);

}  // namespace match::cardschema
