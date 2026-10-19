#pragma once

#include <map>
#include <string>
#include <vector>

#include <match/cardschema/model.hpp>

namespace match::cardschema {

// INFO: the contribution that won a slot and the identifier of its owner: a
// kind ("uni:draw2") or a member ("uni:red").
struct ResolvedSlot {
    Contribution winner;
    std::string source;
};

struct ResolvedFace {
    std::vector<std::string> slot_order;
    std::map<std::string, ResolvedSlot> slots;
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

// INFO: walks `extends` from `kind`. Returns an empty chain and one diagnostic
// on a cycle or a missing parent; the chain holds `kind` itself first.
std::vector<const KindTemplate*> BuildExtendsChain(
    const KindTemplate& kind, const std::map<std::string, const KindTemplate*>& by_id,
    std::vector<Diagnostic>& diagnostics);

}  // namespace match::cardschema
