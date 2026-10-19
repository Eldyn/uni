#include <match/cardschema/identifier.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace match::cardschema {

std::map<std::string, std::string> CanonicalizeSymmetric(
    const KindTemplate& kind, const std::map<std::string, std::string>& slot_members) {
    std::map<std::string, std::string> canonical = slot_members;
    for (const std::vector<std::string>& group : kind.symmetric_groups) {
        std::vector<std::string> members;
        members.reserve(group.size());
        for (const std::string& slot : group) {
            const auto found = slot_members.find(slot);
            if (found != slot_members.end()) {
                members.push_back(found->second);
            }
        }
        std::sort(members.begin(), members.end());
        std::size_t next = 0;
        for (const std::string& slot : group) {
            if (slot_members.find(slot) == slot_members.end()) {
                continue;
            }
            canonical[slot] = members[next];
            ++next;
        }
    }
    return canonical;
}

bool SameCard(const KindTemplate& kind, const std::map<std::string, std::string>& a,
              const std::map<std::string, std::string>& b) {
    return CanonicalizeSymmetric(kind, a) == CanonicalizeSymmetric(kind, b);
}

}  // namespace match::cardschema
