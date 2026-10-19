#include <match/cardschema/catalog.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <match/cardschema/identifier.hpp>
#include <match/cardschema/resolve.hpp>
#include <match/expr/diagnostic.hpp>

namespace match::cardschema {

namespace {

// INFO: the catalog reports at most this many diagnostics; a hostile mod set
// cannot flood the report.
constexpr std::size_t kMaximumDiagnostics = 100;

// INFO: an attribute with more members than this is a loader bug or a hostile
// input; the whole attribute is dropped and reported once.
constexpr std::size_t kMaximumMembersPerAttribute = 1024;

// INFO: a host mod may generate at most this many cards; the collapsed product
// of the member counts is checked against it before any card is allocated.
constexpr std::uint64_t kMaximumGeneratedKinds = 4096;

// INFO: the count of a rejected template; it is above every cap, so it consumes
// the host budget for the templates that follow.
constexpr std::uint64_t kOverBudgetCount = kMaximumGeneratedKinds + 1;

void AddError(std::vector<Diagnostic>& diagnostics, const std::string& file,
              const std::string& pointer, std::string message, std::string suggestion = {}) {
    if (diagnostics.size() >= kMaximumDiagnostics) {
        return;
    }
    diagnostics.push_back(
        Diagnostic{file, pointer, std::move(message), std::move(suggestion)});
}

// INFO: the name a member contributes to an identifier and a title: its alias
// when it sets one, otherwise the local part of its identifier.
std::string MemberLocal(const MemberDef& member) {
    if (!member.identifier_alias.empty()) {
        return member.identifier_alias;
    }
    return LocalNameOf(member.identifier);
}

std::string CapitalizeFirst(std::string value) {
    if (!value.empty() && value.front() >= 'a' && value.front() <= 'z') {
        value.front() = static_cast<char>(value.front() - 'a' + 'A');
    }
    return value;
}

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (const char character : text) {
        if (character == '_') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
            continue;
        }
        word += character;
    }
    if (!word.empty()) {
        words.push_back(word);
    }
    return words;
}

std::string KindPointer(const char* key) {
    return match::expr::AppendPointerToken(
        match::expr::AppendPointerToken(match::expr::kRootPointer, "kind"), key);
}

// INFO: the pointer of the field that produced a generated id: the explicit
// format when the kind sets one, the default-format key otherwise.
std::string IdentifierPointer(const KindTemplate& kind) {
    return KindPointer(kind.identifier_format.empty() ? "identifier" : "identifier_format");
}

// INFO: the fallback pointer of an `attributes` binding for a kind a caller
// built in memory: the position of the binding in `kind.slots`. A loaded kind
// records the real file pointer instead, because the loader skips rejected
// bindings and the position can drift from the file's array index.
std::string AttributePointer(std::size_t index) {
    std::string pointer = match::expr::AppendPointerToken(match::expr::kRootPointer, "kind");
    pointer = match::expr::AppendPointerToken(pointer, "attributes");
    pointer = match::expr::AppendPointerToken(pointer, std::to_string(index));
    return match::expr::AppendPointerToken(pointer, "attribute");
}

std::string AttributePointerFor(const KindTemplate& kind, std::size_t index) {
    const SlotBinding& binding = kind.slots[index];
    if (!binding.attribute_pointer.empty()) {
        return binding.attribute_pointer;
    }
    return AttributePointer(index);
}

const SlotBinding* FindBinding(const KindTemplate& kind, const std::string& name) {
    for (const SlotBinding& binding : kind.slots) {
        if (binding.name == name) {
            return &binding;
        }
    }
    return nullptr;
}

// INFO: the slots the expansion crosses: the declared `slot_order` first, then
// every other declared binding in file order, so a slot the kind does not order
// is not dropped from the cross product.
std::vector<std::string> EffectiveSlotOrder(const KindTemplate& kind) {
    std::vector<std::string> order;
    const auto append = [&order, &kind](const std::string& name) {
        if (name.empty() || std::find(order.begin(), order.end(), name) != order.end()) {
            return;
        }
        for (const SlotBinding& binding : kind.slots) {
            if (binding.name == name) {
                order.push_back(name);
                return;
            }
        }
    };
    for (const std::string& name : kind.slot_order) {
        append(name);
    }
    for (const SlotBinding& binding : kind.slots) {
        append(binding.name);
    }
    return order;
}

void AppendTitleWords(std::string& title, const std::string& text) {
    for (const std::string& word : SplitWords(text)) {
        if (!title.empty()) {
            title += ' ';
        }
        title += CapitalizeFirst(word);
    }
}

// INFO: the default title: the template's local name split on "_" and
// capitalized, then every member value split on "_" the same way and appended
// ("Numbered Red Draw Two").
std::string DefaultTitle(const KindTemplate& kind, const std::vector<std::string>& slot_order,
                         const std::map<std::string, std::string>& slot_values) {
    std::string title;
    AppendTitleWords(title, LocalNameOf(kind.identifier));
    for (const std::string& slot : slot_order) {
        const auto found = slot_values.find(slot);
        if (found != slot_values.end()) {
            AppendTitleWords(title, found->second);
        }
    }
    return title;
}

// INFO: every string value of the face object, recursive, is filled with the
// member locals; a non-string value (a number, a boolean) is copied verbatim.
// Returns false as soon as one string cannot be filled, so a caller discards the
// card instead of emitting a half-filled face.
bool FillFaceStrings(json& node, const std::string& pointer,
                     const std::map<std::string, std::string>& slot_values,
                     const std::string& file, std::vector<Diagnostic>& diagnostics) {
    if (node.is_string()) {
        const std::string text = node.get<std::string>();
        const std::optional<std::string> filled =
            FillTemplate(text, slot_values, file, pointer, diagnostics);
        if (!filled) {
            return false;
        }
        node = *filled;
        return true;
    }
    if (node.is_object()) {
        for (auto entry = node.begin(); entry != node.end(); ++entry) {
            const std::string child_pointer =
                match::expr::AppendPointerToken(pointer, entry.key());
            if (!FillFaceStrings(entry.value(), child_pointer, slot_values, file, diagnostics)) {
                return false;
            }
        }
        return true;
    }
    if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            if (!FillFaceStrings(node[index],
                                 match::expr::AppendPointerToken(pointer, std::to_string(index)),
                                 slot_values, file, diagnostics)) {
                return false;
            }
        }
    }
    return true;
}

// INFO: the placeholder grammar of every string of a face object; the pointer of
// a bad format is the JSON pointer of the string that holds it.
bool CheckTreeFormatSyntax(const json& node, const std::string& pointer, const std::string& file,
                           std::vector<Diagnostic>& diagnostics) {
    if (node.is_string()) {
        if (const std::optional<std::string> error =
                ValidateFormatSyntax(node.get<std::string>())) {
            AddError(diagnostics, file, pointer, *error);
            return false;
        }
        return true;
    }
    if (node.is_object()) {
        for (auto entry = node.begin(); entry != node.end(); ++entry) {
            if (!CheckTreeFormatSyntax(entry.value(),
                                       match::expr::AppendPointerToken(pointer, entry.key()), file,
                                       diagnostics)) {
                return false;
            }
        }
        return true;
    }
    if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            if (!CheckTreeFormatSyntax(node[index],
                                       match::expr::AppendPointerToken(pointer,
                                                                       std::to_string(index)),
                                       file, diagnostics)) {
                return false;
            }
        }
    }
    return true;
}

// INFO: the syntax of every format a template owns, checked once per template so
// a broken format is reported once instead of once per generated card; a
// template with a bad format is skipped before any cross-product entry is built.
bool CheckFormatSyntax(const KindTemplate& kind, std::vector<Diagnostic>& diagnostics) {
    bool well_formed = true;
    if (!kind.identifier_format.empty()) {
        if (const std::optional<std::string> error = ValidateFormatSyntax(kind.identifier_format)) {
            AddError(diagnostics, kind.file, KindPointer("identifier_format"), *error);
            well_formed = false;
        }
    }
    if (!kind.title_format.empty()) {
        if (const std::optional<std::string> error = ValidateFormatSyntax(kind.title_format)) {
            AddError(diagnostics, kind.file, KindPointer("title_format"), *error);
            well_formed = false;
        }
    }
    if (!CheckTreeFormatSyntax(kind.face, KindPointer("face"), kind.file, diagnostics)) {
        well_formed = false;
    }
    return well_formed;
}

// INFO: the product of two counts, or nullopt when the true product does not fit
// in 64 bits. The caller compares the product against the 4096 cap, so an
// overflowing product is reported as "more than 4096".
std::optional<std::uint64_t> MultiplyCounts(std::uint64_t left, std::uint64_t right) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::nullopt;
    }
    return left * right;
}

std::optional<std::uint64_t> MultiplyOptional(std::optional<std::uint64_t> left,
                                              std::optional<std::uint64_t> right) {
    if (!left || !right) {
        return std::nullopt;
    }
    return MultiplyCounts(*left, *right);
}

// INFO: the number of distinct assignments a symmetric group yields after the
// collapse: C(member_count + slot_count - 1, slot_count), the number of
// multisets of `slot_count` member names drawn from `member_count` members.
std::optional<std::uint64_t> SymmetricCollapseCount(std::uint64_t member_count,
                                                    std::uint64_t slot_count) {
    std::optional<std::uint64_t> result = 1;
    for (std::uint64_t step = 1; step <= slot_count; ++step) {
        if (!result) {
            break;
        }
        result = MultiplyCounts(*result, member_count + step - 1);
        if (result) {
            *result /= step;
        }
    }
    return result;
}

// INFO: the number of distinct cards the cross product yields after the
// symmetric collapse: a group of `slot_count` slots over one attribute of
// `member_count` members contributes C(member_count + slot_count - 1, slot_count)
// and every other slot contributes its member count. nullopt means the product
// overflows 64 bits, which is above the cap in any case. Checked before any card
// is allocated.
std::optional<std::uint64_t> CollapsedProduct(
    const KindTemplate& kind, const std::vector<std::string>& slot_order,
    const std::map<std::string, std::size_t>& member_counts) {
    std::set<std::string> grouped_slots;
    std::optional<std::uint64_t> total = std::uint64_t{1};
    for (const std::vector<std::string>& group : kind.symmetric_groups) {
        std::vector<std::string> present;
        for (const std::string& slot : group) {
            if (member_counts.count(slot) != 0) {
                present.push_back(slot);
            }
        }
        if (present.empty()) {
            continue;
        }
        const SlotBinding* first_binding = FindBinding(kind, present.front());
        const bool one_attribute = first_binding != nullptr &&
                                   std::all_of(present.begin(), present.end(),
                                               [&kind, first_binding](const std::string& slot) {
                                                   const SlotBinding* binding =
                                                       FindBinding(kind, slot);
                                                   return binding != nullptr &&
                                                          binding->attribute ==
                                                              first_binding->attribute;
                                               });
        std::optional<std::uint64_t> factor = std::uint64_t{1};
        if (one_attribute) {
            factor = SymmetricCollapseCount(
                static_cast<std::uint64_t>(member_counts.at(present.front())), present.size());
        } else {
            for (const std::string& slot : present) {
                factor = MultiplyOptional(
                    factor, static_cast<std::uint64_t>(member_counts.at(slot)));
            }
        }
        total = MultiplyOptional(total, factor);
        for (const std::string& slot : present) {
            grouped_slots.insert(slot);
        }
    }
    for (const std::string& slot : slot_order) {
        if (grouped_slots.count(slot) != 0) {
            continue;
        }
        const auto found = member_counts.find(slot);
        total = MultiplyOptional(
            total, found == member_counts.end() ? std::optional<std::uint64_t>{0}
                                                : std::optional<std::uint64_t>{found->second});
    }
    return total;
}

std::string DescribeMembers(const std::vector<std::string>& slot_order,
                            const std::map<std::string, std::string>& slot_values) {
    std::string text;
    for (const std::string& slot : slot_order) {
        const auto found = slot_values.find(slot);
        if (found == slot_values.end()) {
            continue;
        }
        if (!text.empty()) {
            text += ", ";
        }
        text += slot + "=" + found->second;
    }
    return text;
}

// INFO: the member a slot holds after canonicalization: the member of the
// slot's attribute whose local name is the canonical value. nullopt when the
// slot or the member is unknown to this kind.
const MemberDef* ResolveSlotMember(
    const KindTemplate& kind, const std::string& slot,
    const std::map<std::string, std::string>& canonical,
    const std::map<std::pair<std::string, std::string>, const MemberDef*>& members_by_attribute) {
    const auto local = canonical.find(slot);
    if (local == canonical.end()) {
        return nullptr;
    }
    const SlotBinding* binding = FindBinding(kind, slot);
    if (binding == nullptr) {
        return nullptr;
    }
    const auto member =
        members_by_attribute.find(std::make_pair(binding->attribute, local->second));
    return member == members_by_attribute.end() ? nullptr : member->second;
}

// INFO: visits every element of the cross product of `member_lists` in row-major
// order, the last list varying fastest; a template with no slots visits once with
// an empty assignment. The caller bounds the work: the collapsed product is
// checked against the cap before this loop runs.
template <typename Visitor>
void ForEachAssignment(const std::vector<std::vector<const MemberDef*>>& member_lists,
                       Visitor&& visit) {
    if (member_lists.empty()) {
        visit(std::vector<const MemberDef*>{});
        return;
    }
    std::vector<std::size_t> indices(member_lists.size(), 0);
    while (true) {
        std::vector<const MemberDef*> assignment;
        assignment.reserve(member_lists.size());
        for (std::size_t index = 0; index < member_lists.size(); ++index) {
            assignment.push_back(member_lists[index][indices[index]]);
        }
        visit(assignment);

        bool advanced = false;
        for (std::size_t position = indices.size(); position-- > 0;) {
            if (++indices[position] < member_lists[position].size()) {
                advanced = true;
                break;
            }
            indices[position] = 0;
        }
        if (!advanced) {
            break;
        }
    }
}

// INFO: builds one card from a canonical slot assignment. Returns nullopt when a
// filled format or face string was invalid; the diagnostics are already recorded
// then. The caller relies on this flag, not on the diagnostics vector, because
// that vector stops growing at the diagnostic cap.
std::optional<GeneratedCard> BuildCard(
    const KindTemplate& kind, const std::string& kind_id,
    const std::vector<std::string>& slot_order,
    const std::map<std::string, std::string>& canonical,
    const std::map<std::pair<std::string, std::string>, const MemberDef*>& members_by_attribute,
    std::vector<Diagnostic>& diagnostics) {
    GeneratedCard card;
    card.kind_id = kind_id;
    card.host_mod = NamespaceOf(kind.identifier);
    card.template_id = kind.identifier;

    std::map<std::string, const MemberDef*> assignment;
    for (const std::string& slot : slot_order) {
        const MemberDef* member = ResolveSlotMember(kind, slot, canonical, members_by_attribute);
        if (member == nullptr) {
            continue;
        }
        assignment[slot] = member;
        card.slot_members[slot] = member->identifier;
    }

    std::set<std::string> used_mods;
    for (const auto& entry : assignment) {
        used_mods.insert(NamespaceOf(entry.second->identifier));
    }
    used_mods.erase(card.host_mod);
    card.requires_mods.assign(used_mods.begin(), used_mods.end());

    std::set<std::string> seen_tags;
    for (const std::string& tag : kind.tags) {
        if (seen_tags.insert(tag).second) {
            card.tags.push_back(tag);
        }
    }
    for (const std::string& slot : slot_order) {
        const auto found = assignment.find(slot);
        if (found == assignment.end()) {
            continue;
        }
        for (const std::string& tag : found->second->tags) {
            if (seen_tags.insert(tag).second) {
                card.tags.push_back(tag);
            }
        }
    }

    if (kind.title_format.empty()) {
        card.title = DefaultTitle(kind, slot_order, canonical);
    } else {
        const std::optional<std::string> title =
            FillTitle(kind.title_format, canonical, kind.file, KindPointer("title_format"),
                      diagnostics);
        if (!title) {
            return std::nullopt;
        }
        card.title = *title;
    }

    card.face = kind.face;
    if (!FillFaceStrings(card.face, KindPointer("face"), canonical, kind.file, diagnostics)) {
        return std::nullopt;
    }

    json entry = json::object();
    entry["id"] = LocalNameOf(kind_id);
    entry["title"] = card.title;
    entry["face"] = card.face;
    entry["tags"] = card.tags;
    if (!kind.behavior.is_null()) {
        entry["behavior"] = kind.behavior;
    }
    if (!kind.window.is_null()) {
        entry["window"] = kind.window;
    }
    if (!kind.auto_trigger.is_null()) {
        entry["auto_trigger"] = kind.auto_trigger;
    }
    if (!kind.label_key.empty()) {
        entry["label_key"] = kind.label_key;
    }
    if (!kind.description_key.empty()) {
        entry["description_key"] = kind.description_key;
    }
    card.legacy_entry = std::move(entry);

    return card;
}

}  // namespace

CatalogResult ExpandCards(const std::vector<ModSchema>& mods) {
    CatalogResult result;

    // INFO: the member total of an attribute is counted over every mod first, so
    // an attribute over the cap is dropped whole and the report does not depend
    // on which member crossed the cap.
    std::map<std::string, std::size_t> attribute_totals;
    for (const ModSchema& mod : mods) {
        for (const MemberDef& member : mod.members) {
            ++attribute_totals[member.attribute];
        }
    }

    // INFO: members are indexed by attribute across every mod in load order and
    // sorted by `order`, then identifier. A slot resolves its member through the
    // pair (attribute, local name), so two members of different attributes that
    // share a local name do not overwrite each other. Two members of ONE
    // attribute that share a local name are a collision and are reported naming
    // both identifiers; the check is local here, not CheckIdentifierCollisions,
    // because that helper compares generated card ids rather than member names.
    std::map<std::string, std::vector<const MemberDef*>> members_by_attribute;
    std::map<std::pair<std::string, std::string>, const MemberDef*> first_member_by_local;
    std::set<std::string> over_cap_attributes;
    for (const ModSchema& mod : mods) {
        for (const MemberDef& member : mod.members) {
            const std::size_t total = attribute_totals[member.attribute];
            if (total > kMaximumMembersPerAttribute) {
                // INFO: one diagnostic names the attribute and its count, at the
                // first member's file; the attribute is left out of the index so
                // no partial cross product is built from it.
                if (over_cap_attributes.insert(member.attribute).second) {
                    AddError(result.diagnostics, member.file,
                             std::string(match::expr::kRootPointer),
                             "attribute \"" + member.attribute + "\" has " +
                                 std::to_string(total) + " members; the maximum is " +
                                 std::to_string(kMaximumMembersPerAttribute));
                }
                continue;
            }
            const std::string local = MemberLocal(member);
            const auto insertion =
                first_member_by_local.emplace(std::make_pair(member.attribute, local), &member);
            if (!insertion.second) {
                AddError(result.diagnostics, member.file, std::string(match::expr::kRootPointer),
                         "member \"" + member.identifier + "\" has the same local name \"" +
                             local + "\" as \"" + insertion.first->second->identifier + "\" in " +
                             insertion.first->second->file);
            }
            members_by_attribute[member.attribute].push_back(&member);
        }
    }
    for (auto& entry : members_by_attribute) {
        std::sort(entry.second.begin(), entry.second.end(),
                  [](const MemberDef* left, const MemberDef* right) {
                      if (left->order != right->order) {
                          return left->order < right->order;
                      }
                      return left->identifier < right->identifier;
                  });
    }

    std::set<std::string> known_attributes;
    for (const ModSchema& mod : mods) {
        for (const AttributeDef& attribute : mod.attributes) {
            known_attributes.insert(attribute.identifier);
        }
    }
    const std::vector<std::string> attribute_candidates(known_attributes.begin(),
                                                        known_attributes.end());

    std::map<std::string, const KindTemplate*> kinds_by_id;
    for (const ModSchema& mod : mods) {
        for (const KindTemplate& kind : mod.kinds) {
            kinds_by_id.emplace(kind.identifier, &kind);
        }
    }

    std::map<std::string, std::size_t> host_counts;
    std::map<std::string, std::size_t> first_source;
    std::vector<std::string> member_descriptions;
    std::vector<std::string> member_files;

    for (const ModSchema& mod : mods) {
        for (const KindTemplate& kind : mod.kinds) {
            if (kind.is_abstract) {
                continue;
            }

            // INFO: `extends` is validated for every concrete template, so a
            // missing parent or a cycle is reported; the chain is not applied to
            // the expansion (see the header comment of catalog.hpp).
            if (BuildExtendsChain(kind, kinds_by_id, result.diagnostics).empty()) {
                continue;
            }

            bool all_attributes_known = true;
            for (std::size_t index = 0; index < kind.slots.size(); ++index) {
                const std::string& attribute = kind.slots[index].attribute;
                if (known_attributes.count(attribute) != 0) {
                    continue;
                }
                AddError(result.diagnostics, kind.file, AttributePointerFor(kind, index),
                         "unknown attribute \"" + attribute + "\"",
                         match::expr::SuggestClosest(attribute, attribute_candidates));
                all_attributes_known = false;
            }
            if (!all_attributes_known) {
                continue;
            }

            if (!CheckFormatSyntax(kind, result.diagnostics)) {
                continue;
            }

            const std::vector<std::string> slot_order = EffectiveSlotOrder(kind);
            std::vector<std::vector<const MemberDef*>> member_lists;
            std::map<std::string, std::size_t> member_counts;
            member_lists.reserve(slot_order.size());
            for (const std::string& slot : slot_order) {
                const SlotBinding* binding = FindBinding(kind, slot);
                const auto found = binding == nullptr
                                       ? members_by_attribute.end()
                                       : members_by_attribute.find(binding->attribute);
                if (found == members_by_attribute.end() || found->second.empty()) {
                    member_lists.emplace_back();
                    member_counts[slot] = 0;
                    continue;
                }
                member_lists.push_back(found->second);
                member_counts[slot] = found->second.size();
            }

            const std::string host_mod = NamespaceOf(kind.identifier);
            const std::optional<std::uint64_t> collapsed =
                CollapsedProduct(kind, slot_order, member_counts);

            // INFO: the collapsed size can be zero when a slot has no member; a
            // template with no card cannot exceed the cap, so it is skipped here.
            if (collapsed && *collapsed == 0) {
                continue;
            }

            const std::size_t host_count = host_counts[host_mod];
            bool over_cap = !collapsed;
            std::uint64_t collapsed_count = 0;
            if (collapsed) {
                collapsed_count = *collapsed;
                if (collapsed_count > kMaximumGeneratedKinds ||
                    host_count > kMaximumGeneratedKinds - collapsed_count) {
                    over_cap = true;
                }
            }
            if (over_cap) {
                const std::string amount =
                    collapsed ? std::to_string(collapsed_count)
                              : "more than " + std::to_string(kMaximumGeneratedKinds);
                AddError(result.diagnostics, kind.file, IdentifierPointer(kind),
                         "template \"" + kind.identifier + "\" expands to " + amount +
                             " cards; host mod \"" + host_mod + "\" exceeds " +
                             std::to_string(kMaximumGeneratedKinds) + " kinds");
                // INFO: a rejected template still consumes the host budget, so
                // the host total counts every declared template and a template
                // that pushes the host over the cap cannot be "refunded" by
                // rejecting it.
                host_counts[host_mod] += collapsed ? collapsed_count : kOverBudgetCount;
                continue;
            }

            std::set<std::string> seen_canonical;
            ForEachAssignment(member_lists, [&](const std::vector<const MemberDef*>& members) {
                std::map<std::string, std::string> slot_values;
                for (std::size_t index = 0; index < slot_order.size(); ++index) {
                    slot_values[slot_order[index]] = MemberLocal(*members[index]);
                }

                const std::map<std::string, std::string> canonical =
                    CanonicalizeSymmetric(kind, slot_values);

                // INFO: the dedupe key names the member each canonical slot
                // resolves to, not the local name alone, so two members of one
                // local name in different attributes do not collapse into one
                // another.
                std::string canonical_key;
                for (const std::string& slot : slot_order) {
                    const MemberDef* member = ResolveSlotMember(kind, slot, canonical,
                                                                first_member_by_local);
                    if (member == nullptr) {
                        continue;
                    }
                    canonical_key += slot;
                    canonical_key += '=';
                    canonical_key += member->attribute;
                    canonical_key += ':';
                    canonical_key += member->identifier;
                    canonical_key += ';';
                }
                if (!seen_canonical.insert(canonical_key).second) {
                    return;
                }

                const std::optional<std::string> kind_id =
                    GenerateIdentifier(kind, canonical, result.diagnostics);
                if (!kind_id) {
                    return;
                }
                std::optional<GeneratedCard> card =
                    BuildCard(kind, *kind_id, slot_order, canonical, first_member_by_local,
                              result.diagnostics);
                if (!card) {
                    return;
                }

                const auto found = first_source.find(card->kind_id);
                if (found == first_source.end()) {
                    first_source.emplace(card->kind_id, result.cards.size());
                    member_descriptions.push_back(DescribeMembers(slot_order, canonical));
                    member_files.push_back(kind.file);
                    host_counts[card->host_mod] += 1;
                    result.cards.push_back(std::move(*card));
                    return;
                }
                // INFO: this pass keys ids in a map, like CheckIdentifierCollisions,
                // but the brief requires the diagnostic to name both templates,
                // their member values and the file of the first one, which that
                // helper's message does not carry; it also spans every mod.
                AddError(result.diagnostics, kind.file, IdentifierPointer(kind),
                         "card \"" + card->kind_id + "\" is generated by both \"" +
                             result.cards[found->second].template_id + "\" (" +
                             member_descriptions[found->second] + ") in " +
                             member_files[found->second] + " and \"" + card->template_id + "\" (" +
                             DescribeMembers(slot_order, canonical) + ")");
            });
        }
    }

    DedupeDiagnostics(result.diagnostics);
    std::sort(result.cards.begin(), result.cards.end(),
              [](const GeneratedCard& left, const GeneratedCard& right) {
                  return left.kind_id < right.kind_id;
              });
    return result;
}

}  // namespace match::cardschema
