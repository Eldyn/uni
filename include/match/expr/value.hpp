#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <match/expr/types.hpp>

namespace match::expr {

// INFO: a seat, card, the match or the settings, identified by its kind and a
// stable per-kind number; ordered by (kind, id).
struct EntityRef {
    Type kind;
    uint64_t id;

    bool operator==(const EntityRef&) const = default;
    auto operator<=>(const EntityRef&) const = default;
};

// INFO: the value an expression evaluates to. The set<string> is a member set
// of member identifiers; the vector<EntityRef> is a selector result.
using Value = std::variant<bool, int64_t, double, std::string, std::set<std::string>,
                          EntityRef, std::vector<EntityRef>>;

}  // namespace match::expr
