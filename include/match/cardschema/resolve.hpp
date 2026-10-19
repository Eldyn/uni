#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <match/cardschema/model.hpp>

namespace match::cardschema {

// INFO: the contribution that won a slot and the identifier of its owner: a
// kind ("uni:draw2") or a member ("uni:red").
struct ResolvedSlot {
    Contribution winner;
    std::string source;
    std::string file;   // file of the owner that wrote the winning contribution
};

struct ResolvedFace {
    std::vector<std::string> slot_order;
    std::map<std::string, ResolvedSlot> slots;
    std::vector<Diagnostic> diagnostics;
};

// INFO: the winning `back` contribution of a deck plus its template chain and
// members, or no slot when no owner contributes a back side. A deck with no
// back contribution and no other candidate is not an error.
struct ResolvedBack {
    std::optional<ResolvedSlot> slot;
    std::vector<Diagnostic> diagnostics;
};

// INFO: `chain_child_first` is the template and its parents, child first; the
// contribution of the nearest kind wins a slot. `members` are the bound members
// in slot order. The functions are pure: they read in-memory structs only.
//
// INFO: a resolve call reports the same not-in-slot_order error once per owner
// that carries it, so a shared parent kind or a shared member is reported again
// by every face that uses it. The caller that resolves many faces dedupes the
// accumulated diagnostics by (file, pointer, message) with DedupeDiagnostics.
ResolvedFace ResolveTemplateSlots(const std::vector<const KindTemplate*>& chain_child_first,
                                  const std::vector<const MemberDef*>& members,
                                  const std::string& file_label);

// INFO: removes a diagnostic whose (file, pointer, message) already appeared,
// keeping the first occurrence and the order of the rest.
void DedupeDiagnostics(std::vector<Diagnostic>& diagnostics);

// INFO: resolves the `back` slot for a deck: the kind chain `back` contribution
// beats the deck's own at the same band, and members contribute at `attribute`
// unless they set a band. The functions are pure and read in-memory structs.
//
// INFO: ResolveBack and CheckSurfaces are wired into the catalog expansion in
// Tasks 11 and 13; there is no production caller yet.
ResolvedBack ResolveBack(const DeckSpec* deck,
                         const std::vector<const KindTemplate*>& chain_child_first,
                         const std::vector<const MemberDef*>& members);

// INFO: checks the surfaces of one kind against the face resolved for it: every
// `SlotBinding.surface` must name a surface declared by a resolved contribution,
// and two surfaces of one slot must not share a (mask, channel) pair.
std::vector<Diagnostic> CheckSurfaces(const KindTemplate& kind, const ResolvedFace& face);

// INFO: walks `extends` from `kind`. Returns an empty chain and one diagnostic
// on a cycle or a missing parent; the chain holds `kind` itself first.
std::vector<const KindTemplate*> BuildExtendsChain(
    const KindTemplate& kind, const std::map<std::string, const KindTemplate*>& by_id,
    std::vector<Diagnostic>& diagnostics);

}  // namespace match::cardschema
