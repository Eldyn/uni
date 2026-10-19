#include <match/cardschema/resolve.hpp>

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <match/expr/diagnostic.hpp>

namespace match::cardschema {

namespace {

// INFO: a chain longer than this cannot be a real inheritance tree and is
// rejected instead of walked, so a hostile `extends` graph cannot bound the
// work from below.
constexpr std::size_t kMaximumExtendsChain = 16;

// INFO: a card holds one bound member per slot name; a longer list is a loader
// bug or a hostile input, so it is rejected before any expansion work.
constexpr std::size_t kMaximumMembersPerCard = 32;

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

// INFO: one owner's winning contribution for a slot together with the
// identifier of that owner and the file the contribution's pointer belongs to.
struct Candidate {
    const Contribution* contribution = nullptr;
    std::string source;
    std::string file;
};

std::string PointerFor(const Contribution& contribution, const std::string& slot) {
    if (!contribution.pointer.empty()) {
        return contribution.pointer;
    }
    return match::expr::AppendPointerToken(match::expr::kRootPointer, slot);
}

std::string JoinSources(const std::vector<const Candidate*>& candidates) {
    std::string text;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0) {
            text += ", ";
        }
        text += candidates[index]->source;
    }
    return text;
}

// INFO: expands one owner's contribution map against `slot_order`: the `all`
// key covers every slot a slot-specific key does not name. A contribution to a
// slot outside `slot_order` is an error; the `back` slot is left to ResolveBack.
// The error names `owner_file`, because a contribution's pointer belongs to the
// file of the owner that wrote it, not to the file passed by the caller.
// `slot_set` holds the same slots as `slot_order`, built once by the caller so
// membership is not a scan per owner contribution.
void ExpandContributions(const ContributionMap& contributions,
                         const std::vector<std::string>& slot_order,
                         const std::set<std::string>& slot_set, const std::string& owner_file,
                         const std::string& source, std::vector<Diagnostic>& diagnostics,
                         std::map<std::string, Candidate>& out) {
    const auto all = contributions.find("all");
    for (const std::string& slot : slot_order) {
        const auto specific = contributions.find(slot);
        if (specific != contributions.end()) {
            out[slot] = Candidate{&specific->second, source, owner_file};
        } else if (all != contributions.end()) {
            out[slot] = Candidate{&all->second, source, owner_file};
        }
    }

    for (const auto& entry : contributions) {
        const std::string& slot = entry.first;
        if (slot == "all" || slot == "back" || slot_set.count(slot) != 0) {
            continue;
        }
        AddError(diagnostics, owner_file, PointerFor(entry.second, slot),
                 "contribution to slot \"" + slot + "\" which is not in slot_order");
    }
}

// INFO: the band group of a slot decides its winner: the highest band wins. A
// group of two or more is a conflict: exactly one explicit member wins, two or
// more explicit members are an error, and none explicit is an error that names
// every owner. A conflict returns no winner.
const Candidate* SelectBandWinner(const std::vector<const Candidate*>& group,
                                  const std::string& slot, const std::string& fallback_file,
                                  std::vector<Diagnostic>& diagnostics) {
    if (group.size() < 2) {
        return group.front();
    }
    std::vector<const Candidate*> explicit_candidates;
    for (const Candidate* candidate : group) {
        if (candidate->contribution->explicit_override) {
            explicit_candidates.push_back(candidate);
        }
    }
    const std::string band = BandName(group.front()->contribution->band);
    const std::string& file = group.front()->file.empty() ? fallback_file : group.front()->file;
    const std::string pointer = PointerFor(*group.front()->contribution, slot);
    if (explicit_candidates.size() == 1) {
        return explicit_candidates.front();
    }
    if (explicit_candidates.size() >= 2) {
        AddError(diagnostics, file, pointer,
                 "slot \"" + slot + "\" has " +
                     std::to_string(explicit_candidates.size()) +
                     " explicit contributions in band \"" + band + "\" (" +
                     JoinSources(explicit_candidates) + ")");
        return nullptr;
    }
    const std::string count =
        group.size() == 2 ? std::string("two") : std::to_string(group.size());
    AddError(diagnostics, file, pointer,
             "slot \"" + slot + "\" has " + count + " contributions in band \"" + band + "\" (" +
                 JoinSources(group) + "); set \"explicit\": true on one");
    return nullptr;
}

bool CheckMemberBudget(const std::vector<const MemberDef*>& members, const std::string& file,
                       std::vector<Diagnostic>& diagnostics) {
    if (members.size() <= kMaximumMembersPerCard) {
        return true;
    }
    AddError(diagnostics, file, std::string(match::expr::kRootPointer),
             "a card may have at most " + std::to_string(kMaximumMembersPerCard) + " members");
    return false;
}

// INFO: the highest band wins; a conflict in it produces a diagnostic and no
// winner. Returns whether a winner was written to `resolved`.
bool SelectSlotWinner(const std::vector<const Candidate*>& candidates, const std::string& slot,
                      const std::string& fallback_file,
                      std::vector<Diagnostic>& diagnostics, ResolvedSlot& resolved) {
    if (candidates.empty()) {
        return false;
    }
    std::map<Band, std::vector<const Candidate*>> by_band;
    for (const Candidate* candidate : candidates) {
        by_band[candidate->contribution->band].push_back(candidate);
    }
    const Candidate* winner =
        SelectBandWinner(by_band.rbegin()->second, slot, fallback_file, diagnostics);
    if (winner == nullptr) {
        return false;
    }
    resolved = ResolvedSlot{*winner->contribution, winner->source, winner->file};
    return true;
}

// INFO: the loader skips a binding it rejects, so a binding's position in
// `kind.slots` can differ from its index in the source `attributes` array. Only
// an in-memory kind without a recorded pointer falls back to the position.
std::string SynthesizedSurfacePointer(std::size_t index) {
    std::string pointer = match::expr::AppendPointerToken(match::expr::kRootPointer, "kind");
    pointer = match::expr::AppendPointerToken(pointer, "attributes");
    pointer = match::expr::AppendPointerToken(pointer, std::to_string(index));
    return match::expr::AppendPointerToken(pointer, "surface");
}

}  // namespace

ResolvedFace ResolveTemplateSlots(const std::vector<const KindTemplate*>& chain_child_first,
                                  const std::vector<const MemberDef*>& members,
                                  const std::string& file_label) {
    ResolvedFace face;
    if (chain_child_first.empty()) {
        return face;
    }
    if (!CheckMemberBudget(members, file_label, face.diagnostics)) {
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

    // INFO: one lookup set for the whole resolve, so a contribution to a slot
    // outside `slot_order` is not a linear scan per owner.
    const std::set<std::string> slot_set(face.slot_order.begin(), face.slot_order.end());

    // INFO: a member listed twice binds once; the first occurrence wins, so the
    // duplicate does not look like a cross-owner conflict.
    std::vector<const MemberDef*> unique_members;
    unique_members.reserve(members.size());
    std::set<std::string> seen_members;
    for (const MemberDef* member : members) {
        if (seen_members.insert(member->identifier).second) {
            unique_members.push_back(member);
        }
    }

    // INFO: the kind chain is collapsed per slot before members join, so a
    // child's contribution replaces a parent's without a conflict.
    std::vector<std::map<std::string, Candidate>> kind_expansions;
    kind_expansions.reserve(chain_child_first.size());
    for (const KindTemplate* kind : chain_child_first) {
        std::map<std::string, Candidate> expansion;
        ExpandContributions(kind->contribute, face.slot_order, slot_set, kind->file,
                            kind->identifier, face.diagnostics, expansion);
        kind_expansions.push_back(std::move(expansion));
    }

    std::vector<std::map<std::string, Candidate>> member_expansions;
    member_expansions.reserve(unique_members.size());
    for (const MemberDef* member : unique_members) {
        std::map<std::string, Candidate> expansion;
        ExpandContributions(member->contribute, face.slot_order, slot_set, member->file,
                            member->identifier, face.diagnostics, expansion);
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

        ResolvedSlot resolved;
        if (SelectSlotWinner(candidates, slot, file_label, face.diagnostics, resolved)) {
            face.slots[slot] = resolved;
        }
    }

    return face;
}

// INFO: the loader always records the real pointer of an `extends` key, which is
// `/kind/extends`; the fallback covers an in-memory kind built by a caller that
// did not set it.
std::string ExtendsPointer(const KindTemplate& kind) {
    if (!kind.extends_pointer.empty()) {
        return kind.extends_pointer;
    }
    return match::expr::AppendPointerToken(
        match::expr::AppendPointerToken(match::expr::kRootPointer, "kind"), "extends");
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
            AddError(diagnostics, current->file, ExtendsPointer(*current),
                     "extends cycle: " + path);
            return {};
        }
        chain.push_back(current);
        if (chain.size() > kMaximumExtendsChain) {
            AddError(diagnostics, current->file, ExtendsPointer(*current),
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
            AddError(diagnostics, current->file, ExtendsPointer(*current),
                     "unknown parent \"" + current->extends + "\"",
                     match::expr::SuggestClosest(current->extends, candidates));
            return {};
        }
        current = parent->second;
    }

    return chain;
}

void DedupeDiagnostics(std::vector<Diagnostic>& diagnostics) {
    std::vector<Diagnostic> unique;
    unique.reserve(diagnostics.size());
    std::set<std::tuple<std::string, std::string, std::string>> seen;
    for (Diagnostic& diagnostic : diagnostics) {
        const bool first = seen
                               .emplace(diagnostic.file, diagnostic.pointer, diagnostic.message)
                               .second;
        if (first) {
            unique.push_back(std::move(diagnostic));
        }
    }
    diagnostics = std::move(unique);
}

ResolvedBack ResolveBack(const DeckSpec* deck,
                         const std::vector<const KindTemplate*>& chain_child_first,
                         const std::vector<const MemberDef*>& members) {
    ResolvedBack result;
    if (!CheckMemberBudget(members, deck != nullptr ? deck->file : std::string{},
                           result.diagnostics)) {
        return result;
    }

    const Contribution* deck_back = nullptr;
    if (deck != nullptr) {
        const auto found = deck->contribute.find("back");
        if (found != deck->contribute.end()) {
            deck_back = &found->second;
        }
    }

    // INFO: the nearest kind that contributes a back side wins the chain; the
    // kind and the deck do not conflict, so at the same band the kind replaces
    // the deck instead of being reported as a two-owner conflict.
    const Contribution* kind_back = nullptr;
    const KindTemplate* kind_owner = nullptr;
    for (const KindTemplate* kind : chain_child_first) {
        const auto found = kind->contribute.find("back");
        if (found != kind->contribute.end()) {
            kind_back = &found->second;
            kind_owner = kind;
            break;
        }
    }

    std::vector<Candidate> candidates;
    if (deck_back != nullptr && kind_back != nullptr) {
        if (kind_back->band >= deck_back->band) {
            candidates.push_back(Candidate{kind_back, kind_owner->identifier, kind_owner->file});
        } else {
            candidates.push_back(Candidate{deck_back, deck->identifier, deck->file});
        }
    } else if (kind_back != nullptr) {
        candidates.push_back(Candidate{kind_back, kind_owner->identifier, kind_owner->file});
    } else if (deck_back != nullptr) {
        candidates.push_back(Candidate{deck_back, deck->identifier, deck->file});
    }

    std::set<std::string> seen_members;
    for (const MemberDef* member : members) {
        if (!seen_members.insert(member->identifier).second) {
            continue;
        }
        const auto found = member->contribute.find("back");
        if (found == member->contribute.end()) {
            continue;
        }
        candidates.push_back(Candidate{&found->second, member->identifier, member->file});
    }

    std::vector<const Candidate*> candidate_pointers;
    candidate_pointers.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        candidate_pointers.push_back(&candidate);
    }

    const std::string fallback_file = deck != nullptr ? deck->file : std::string{};
    ResolvedSlot resolved;
    if (SelectSlotWinner(candidate_pointers, "back", fallback_file, result.diagnostics,
                         resolved)) {
        result.slot = resolved;
    }
    return result;
}

std::vector<Diagnostic> CheckSurfaces(const KindTemplate& kind, const ResolvedFace& face) {
    std::vector<Diagnostic> diagnostics;

    // INFO: the surface names the resolved contributions declare. A slot binding
    // may name any of them, not only a surface of its own slot.
    std::set<std::string> declared;
    for (const auto& slot : face.slots) {
        for (const auto& surface : slot.second.winner.surfaces) {
            declared.insert(surface.first);
        }
    }

    for (std::size_t index = 0; index < kind.slots.size(); ++index) {
        const SlotBinding& binding = kind.slots[index];
        if (binding.surface.empty() || declared.count(binding.surface) != 0) {
            continue;
        }
        const std::vector<std::string> names(declared.begin(), declared.end());
        const std::string pointer = binding.surface_pointer.empty()
                                        ? SynthesizedSurfacePointer(index)
                                        : binding.surface_pointer;
        AddError(diagnostics, kind.file, pointer,
                 "slot \"" + binding.name + "\" names surface \"" + binding.surface + "\"",
                 match::expr::SuggestClosest(binding.surface, names));
    }

    // TODO: only duplicate (mask, channel) pairs inside one slot's winning
    // contribution are detected; a shared channel across slots is a possible
    // later rule.
    for (const auto& slot : face.slots) {
        const Contribution& contribution = slot.second.winner;
        const std::string& file = slot.second.file.empty() ? kind.file : slot.second.file;
        std::map<std::pair<std::string, std::string>, std::string> seen;
        for (const auto& surface : contribution.surfaces) {
            const std::pair<std::string, std::string> pair{surface.second.mask,
                                                           surface.second.channel};
            const auto inserted = seen.emplace(pair, surface.first);
            if (inserted.second) {
                continue;
            }
            AddError(diagnostics, file, PointerFor(contribution, slot.first),
                     "slot \"" + slot.first + "\" surfaces \"" + inserted.first->second +
                         "\" and \"" + surface.first + "\" use the same mask \"" +
                         surface.second.mask + "\" and channel \"" + surface.second.channel +
                         "\"");
        }
    }

    return diagnostics;
}

}  // namespace match::cardschema
