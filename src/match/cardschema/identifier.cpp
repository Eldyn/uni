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
            const auto found = canonical.find(slot);
            if (found != canonical.end()) {
                members.push_back(found->second);
            }
        }
        std::sort(members.begin(), members.end());
        std::size_t next_member_index = 0;
        for (const std::string& slot : group) {
            if (canonical.find(slot) == canonical.end()) {
                continue;
            }
            canonical[slot] = members[next_member_index];
            ++next_member_index;
        }
    }
    return canonical;
}

bool SameCard(const KindTemplate& kind, const std::map<std::string, std::string>& a,
              const std::map<std::string, std::string>& b) {
    return CanonicalizeSymmetric(kind, a) == CanonicalizeSymmetric(kind, b);
}

}  // namespace match::cardschema
