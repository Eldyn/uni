#include <match/cardschema/resolve.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <match/expr/diagnostic.hpp>

namespace match::cardschema {

namespace {

// INFO: a chain longer than this cannot be a real inheritance tree and is
// rejected instead of walked, so a hostile `extends` graph cannot bound the
// work from below.
constexpr std::size_t kMaximumExtendsChain = 16;

void AddError(std::vector<Diagnostic>& diagnostics, const std::string& file,
              const std::string& pointer, std::string message, std::string suggestion = {}) {
    diagnostics.push_back(
        Diagnostic{file, pointer, std::move(message), std::move(suggestion)});
}

std::string BandName(Band band) {
    switch (band) {
        case Band::kBase:
            return "base";
        case Band::kAttribute:
            return "attribute";
        case Band::kOverride:
            return "override";
        case Band::kRuntime:
            return "runtime";
    }
    return "base";
}

int BandRank(Band band) { return static_cast<int>(band); }

// INFO: one owner's winning contribution for a slot together with the
// identifier of that owner.
struct Candidate {
    const Contribution* contribution = nullptr;
    std::string source;
};

bool HasSlot(const std::vector<std::string>& slot_order, const std::string& slot) {
    return std::find(slot_order.begin(), slot_order.end(), slot) != slot_order.end();
}

std::string PointerFor(const Contribution& contribution, const std::string& slot) {
    if (!contribution.pointer.empty()) {
        return contribution.pointer;
    }
    return match::expr::AppendPointerToken(match::expr::kRootPointer, slot);
}

// INFO: expands one owner's contribution map against `slot_order`: the `all`
// key covers every slot a slot-specific key does not name. A contribution to a
// slot outside `slot_order` is an error; the `back` slot is left to ResolveBack.
void ExpandContributions(const ContributionMap& contributions,
                         const std::vector<std::string>& slot_order,
                         const std::string& file_label, const std::string& source,
                         std::vector<Diagnostic>& diagnostics,
                         std::map<std::string, Candidate>& out) {
    const auto all = contributions.find("all");
    for (const std::string& slot : slot_order) {
        const auto specific = contributions.find(slot);
        if (specific != contributions.end()) {
            out[slot] = Candidate{&specific->second, source};
        } else if (all != contributions.end()) {
            out[slot] = Candidate{&all->second, source};
        }
    }

    for (const auto& entry : contributions) {
        const std::string& slot = entry.first;
        if (slot == "all" || slot == "back" || HasSlot(slot_order, slot)) {
            continue;
        }
        AddError(diagnostics, file_label, PointerFor(entry.second, slot),
                 "contribution to slot \"" + slot + "\" which is not in slot_order");
    }
}

// INFO: the band group of a slot decides its winner: the highest band wins. A
// group of two or more is a conflict unless exactly one member is explicit, in
// which case that one wins with no error.
const Candidate* SelectBandWinner(const std::vector<const Candidate*>& group,
                                  const std::string& slot, const std::string& file_label,
                                  std::vector<Diagnostic>& diagnostics) {
    const Candidate* explicit_candidate = nullptr;
    std::size_t explicit_count = 0;
    for (const Candidate* candidate : group) {
        if (candidate->contribution->explicit_override) {
            explicit_candidate = candidate;
            ++explicit_count;
        }
    }
    if (group.size() >= 2 && explicit_count == 1) {
        return explicit_candidate;
    }
    if (group.size() >= 2) {
        AddError(diagnostics, file_label, PointerFor(*group[0]->contribution, slot),
                 "slot \"" + slot + "\" has two contributions in band \"" +
                     BandName(group[0]->contribution->band) + "\" (" + group[0]->source +
                     ", " + group[1]->source + "); set \"explicit\": true on one");
    }
    return group[0];
}

}  // namespace

ResolvedFace ResolveTemplateSlots(const std::vector<const KindTemplate*>& chain_child_first,
                                  const std::vector<const MemberDef*>& members,
                                  const std::string& file_label) {
    ResolvedFace face;
    if (chain_child_first.empty()) {
        return face;
    }

    // INFO: the effective slot order is the child's if it sets one, else the
    // nearest parent's.
    const std::vector<std::string>* slot_order = nullptr;
    for (const KindTemplate* kind : chain_child_first) {
        if (!kind->slot_order.empty()) {
            slot_order = &kind->slot_order;
            break;
        }
    }
    face.slot_order = slot_order != nullptr ? *slot_order : std::vector<std::string>{};

    // INFO: the kind chain is collapsed per slot before members join, so a
    // child's contribution replaces a parent's without a conflict.
    std::vector<std::map<std::string, Candidate>> kind_expansions;
    kind_expansions.reserve(chain_child_first.size());
    for (const KindTemplate* kind : chain_child_first) {
        std::map<std::string, Candidate> expansion;
        ExpandContributions(kind->contribute, face.slot_order, file_label, kind->identifier,
                            face.diagnostics, expansion);
        kind_expansions.push_back(std::move(expansion));
    }

    std::vector<std::map<std::string, Candidate>> member_expansions;
    member_expansions.reserve(members.size());
    for (const MemberDef* member : members) {
        std::map<std::string, Candidate> expansion;
        ExpandContributions(member->contribute, face.slot_order, file_label, member->identifier,
                            face.diagnostics, expansion);
        member_expansions.push_back(std::move(expansion));
    }

    for (const std::string& slot : face.slot_order) {
        std::vector<const Candidate*> candidates;
        for (const auto& expansion : kind_expansions) {
            const auto found = expansion.find(slot);
            if (found != expansion.end()) {
                candidates.push_back(&found->second);
                break;
            }
        }
        for (const auto& expansion : member_expansions) {
            const auto found = expansion.find(slot);
            if (found != expansion.end()) {
                candidates.push_back(&found->second);
            }
        }
        if (candidates.empty()) {
            continue;
        }

        std::map<int, std::vector<const Candidate*>> by_band;
        for (const Candidate* candidate : candidates) {
            by_band[BandRank(candidate->contribution->band)].push_back(candidate);
        }

        const Candidate* winner = nullptr;
        for (auto group = by_band.rbegin(); group != by_band.rend(); ++group) {
            winner = SelectBandWinner(group->second, slot, file_label, face.diagnostics);
            break;
        }
        if (winner == nullptr) {
            continue;
        }
        face.slots[slot] = ResolvedSlot{*winner->contribution, winner->source};
    }

    return face;
}

std::vector<const KindTemplate*> BuildExtendsChain(
    const KindTemplate& kind, const std::map<std::string, const KindTemplate*>& by_id,
    std::vector<Diagnostic>& diagnostics) {
    std::vector<const KindTemplate*> chain;
    std::set<std::string> seen;
    const KindTemplate* current = &kind;

    while (current != nullptr) {
        if (!seen.insert(current->identifier).second) {
            std::string path;
            for (const KindTemplate* node : chain) {
                path += node->identifier;
                path += " -> ";
            }
            path += current->identifier;
            AddError(diagnostics, current->file,
                     match::expr::AppendPointerToken(match::expr::kRootPointer, "extends"),
                     "extends cycle: " + path);
            return {};
        }
        chain.push_back(current);
        if (chain.size() > kMaximumExtendsChain) {
            AddError(diagnostics, current->file,
                     match::expr::AppendPointerToken(match::expr::kRootPointer, "extends"),
                     "extends chain is longer than " + std::to_string(kMaximumExtendsChain) +
                         " kinds");
            return {};
        }
        if (current->extends.empty()) {
            break;
        }
        const auto parent = by_id.find(current->extends);
        if (parent == by_id.end()) {
            std::vector<std::string> candidates;
            candidates.reserve(by_id.size());
            for (const auto& entry : by_id) {
                candidates.push_back(entry.first);
            }
            AddError(diagnostics, current->file,
                     match::expr::AppendPointerToken(match::expr::kRootPointer, "extends"),
                     "unknown parent \"" + current->extends + "\"",
                     match::expr::SuggestClosest(current->extends, candidates));
            return {};
        }
        current = parent->second;
    }

    return chain;
}

}  // namespace match::cardschema
