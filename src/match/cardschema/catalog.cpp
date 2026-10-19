#include <match/cardschema/catalog.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <match/cardschema/identifier.hpp>
#include <match/expr/diagnostic.hpp>

namespace match::cardschema {

namespace {

// INFO: the catalog reports at most this many diagnostics; a hostile mod set
// cannot flood the report.
constexpr std::size_t kMaximumDiagnostics = 100;

// INFO: an attribute with more members than this is a loader bug or a hostile
// input; the extra members are reported once each instead of indexed.
constexpr std::size_t kMaximumMembersPerAttribute = 1024;

// INFO: a host mod may generate at most this many cards; the product of the
// member counts is checked against it before any card is allocated.
constexpr std::uint64_t kMaximumGeneratedKinds = 4096;

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

// INFO: an `attributes` binding records no pointer of its `attribute` key, so
// the position in the bindings the loader kept is used to place the error.
std::string AttributePointer(std::size_t index) {
    std::string pointer = match::expr::AppendPointerToken(match::expr::kRootPointer, "kind");
    pointer = match::expr::AppendPointerToken(pointer, "attributes");
    pointer = match::expr::AppendPointerToken(pointer, std::to_string(index));
    return match::expr::AppendPointerToken(pointer, "attribute");
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

// INFO: the default title: the template's local name split on "_" and
// capitalized, then every member value capitalized and appended with spaces
// ("Numbered Red 5").
std::string DefaultTitle(const KindTemplate& kind, const std::vector<std::string>& slot_order,
                         const std::map<std::string, std::string>& slot_values) {
    std::string title;
    const auto append = [&title](const std::string& word) {
        if (!title.empty()) {
            title += ' ';
        }
        title += CapitalizeFirst(word);
    };
    for (const std::string& word : SplitWords(LocalNameOf(kind.identifier))) {
        append(word);
    }
    for (const std::string& slot : slot_order) {
        const auto found = slot_values.find(slot);
        if (found != slot_values.end()) {
            append(found->second);
        }
    }
    return title;
}

// INFO: every string value of the face object, recursive, is filled with the
// member locals; a non-string value (a number, a boolean) is copied verbatim.
void FillFaceStrings(json& node, const std::string& pointer,
                     const std::map<std::string, std::string>& slot_values,
                     const std::string& file, std::vector<Diagnostic>& diagnostics) {
    if (node.is_string()) {
        const std::string text = node.get<std::string>();
        const std::optional<std::string> filled =
            FillTemplate(text, slot_values, file, pointer, diagnostics);
        if (filled) {
            node = *filled;
        }
        return;
    }
    if (node.is_object()) {
        for (auto entry = node.begin(); entry != node.end(); ++entry) {
            FillFaceStrings(entry.value(), match::expr::AppendPointerToken(pointer, entry.key()),
                            slot_values, file, diagnostics);
        }
        return;
    }
    if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            FillFaceStrings(node[index],
                            match::expr::AppendPointerToken(pointer, std::to_string(index)),
                            slot_values, file, diagnostics);
        }
    }
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

// INFO: builds one card from a canonical slot assignment. `by_local` resolves a
// canonical member local back to the member that declared it. Returns nullopt
// when a filled format or face string was invalid; the diagnostics are already
// recorded then.
std::optional<GeneratedCard> BuildCard(
    const KindTemplate& kind, const std::string& kind_id,
    const std::vector<std::string>& slot_order,
    const std::map<std::string, std::string>& canonical,
    const std::map<std::string, const MemberDef*>& by_local,
    std::vector<Diagnostic>& diagnostics) {
    GeneratedCard card;
    card.kind_id = kind_id;
    card.host_mod = NamespaceOf(kind.identifier);
    card.template_id = kind.identifier;

    std::map<std::string, const MemberDef*> assignment;
    for (const std::string& slot : slot_order) {
        const auto local = canonical.find(slot);
        if (local == canonical.end()) {
            continue;
        }
        const auto member = by_local.find(local->second);
        if (member == by_local.end()) {
            continue;
        }
        assignment[slot] = member->second;
        card.slot_members[slot] = member->second->identifier;
    }

    std::set<std::string> used_mods;
    for (const auto& entry : assignment) {
        used_mods.insert(NamespaceOf(entry.second->identifier));
    }
    used_mods.erase(card.host_mod);
    card.requires_mods.assign(used_mods.begin(), used_mods.end());

    card.tags = kind.tags;
    std::set<std::string> seen_tags(card.tags.begin(), card.tags.end());
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
    FillFaceStrings(card.face, KindPointer("face"), canonical, kind.file, diagnostics);

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

    // INFO: members are indexed by attribute across every mod in load order and
    // sorted by `order`, then identifier.
    std::map<std::string, std::vector<const MemberDef*>> members_by_attribute;
    for (const ModSchema& mod : mods) {
        for (const MemberDef& member : mod.members) {
            std::vector<const MemberDef*>& members = members_by_attribute[member.attribute];
            if (members.size() >= kMaximumMembersPerAttribute) {
                AddError(result.diagnostics, member.file, std::string(match::expr::kRootPointer),
                         "attribute \"" + member.attribute + "\" has more than " +
                             std::to_string(kMaximumMembersPerAttribute) + " members");
                continue;
            }
            members.push_back(&member);
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

    std::map<std::string, std::size_t> host_counts;
    std::map<std::string, std::size_t> first_source;
    std::vector<std::string> member_descriptions;

    for (const ModSchema& mod : mods) {
        for (const KindTemplate& kind : mod.kinds) {
            if (kind.is_abstract) {
                continue;
            }

            bool known_attributes_only = true;
            for (std::size_t index = 0; index < kind.slots.size(); ++index) {
                const std::string& attribute = kind.slots[index].attribute;
                if (known_attributes.count(attribute) != 0) {
                    continue;
                }
                AddError(result.diagnostics, kind.file, AttributePointer(index),
                         "unknown attribute \"" + attribute + "\"",
                         match::expr::SuggestClosest(attribute, attribute_candidates));
                known_attributes_only = false;
            }
            if (!known_attributes_only) {
                continue;
            }

            const std::vector<std::string> slot_order = EffectiveSlotOrder(kind);
            std::vector<std::vector<const MemberDef*>> member_lists;
            member_lists.reserve(slot_order.size());
            bool empty_product = false;
            for (const std::string& slot : slot_order) {
                const SlotBinding* binding = FindBinding(kind, slot);
                const auto found = binding == nullptr
                                       ? members_by_attribute.end()
                                       : members_by_attribute.find(binding->attribute);
                if (found == members_by_attribute.end() || found->second.empty()) {
                    empty_product = true;
                    member_lists.emplace_back();
                    continue;
                }
                member_lists.push_back(found->second);
            }

            // INFO: the product is checked against the 4096 cap with a multiply
            // that cannot overflow, before any card is allocated.
            std::uint64_t product = 1;
            bool overflow = false;
            for (const std::vector<const MemberDef*>& members : member_lists) {
                if (members.empty()) {
                    product = 0;
                    break;
                }
                if (product > kMaximumGeneratedKinds / members.size()) {
                    overflow = true;
                    break;
                }
                product *= members.size();
            }

            const std::string host_mod = NamespaceOf(kind.identifier);
            const std::size_t host_count = host_counts[host_mod];
            if (overflow || product > kMaximumGeneratedKinds ||
                host_count + product > kMaximumGeneratedKinds) {
                const std::string amount = overflow ? std::string("more than 4096")
                                                    : std::to_string(product);
                AddError(result.diagnostics, kind.file, IdentifierPointer(kind),
                         "template \"" + kind.identifier + "\" expands to " + amount +
                             " cards; host mod \"" + host_mod + "\" exceeds 4096 kinds");
                continue;
            }
            if (empty_product) {
                continue;
            }

            std::vector<std::size_t> indices(member_lists.size(), 0);
            std::set<std::string> seen_canonical;
            while (true) {
                std::map<std::string, std::string> slot_values;
                std::map<std::string, const MemberDef*> by_local;
                for (std::size_t slot_index = 0; slot_index < slot_order.size(); ++slot_index) {
                    const MemberDef* member = member_lists[slot_index][indices[slot_index]];
                    const std::string local = MemberLocal(*member);
                    slot_values[slot_order[slot_index]] = local;
                    by_local[local] = member;
                }

                const std::map<std::string, std::string> canonical =
                    CanonicalizeSymmetric(kind, slot_values);
                std::string canonical_key;
                for (const auto& entry : canonical) {
                    canonical_key += entry.first;
                    canonical_key += '=';
                    canonical_key += entry.second;
                    canonical_key += ';';
                }

                if (seen_canonical.insert(canonical_key).second) {
                    const std::size_t diagnostics_before = result.diagnostics.size();
                    const std::optional<std::string> kind_id =
                        GenerateIdentifier(kind, canonical, result.diagnostics);
                    if (kind_id) {
                        std::optional<GeneratedCard> card =
                            BuildCard(kind, *kind_id, slot_order, canonical, by_local,
                                      result.diagnostics);
                        if (card && result.diagnostics.size() == diagnostics_before) {
                            const auto found = first_source.find(card->kind_id);
                            if (found == first_source.end()) {
                                first_source.emplace(card->kind_id, result.cards.size());
                                member_descriptions.push_back(
                                    DescribeMembers(slot_order, canonical));
                                host_counts[card->host_mod] += 1;
                                result.cards.push_back(std::move(*card));
                            } else {
                                AddError(result.diagnostics, kind.file, IdentifierPointer(kind),
                                         "card \"" + card->kind_id +
                                             "\" is generated by both \"" +
                                             result.cards[found->second].template_id + "\" (" +
                                             member_descriptions[found->second] + ") and \"" +
                                             card->template_id + "\" (" +
                                             DescribeMembers(slot_order, canonical) + ")");
                            }
                        }
                    }
                }

                bool advanced = false;
                for (std::size_t position = indices.size(); position-- > 0;) {
                    if (++indices[position] < member_lists[position].size()) {
                        advanced = true;
                        break;
                    }
                    indices[position] = 0;
                }
                if (indices.empty() || !advanced) {
                    break;
                }
            }
        }
    }

    std::sort(result.cards.begin(), result.cards.end(),
              [](const GeneratedCard& left, const GeneratedCard& right) {
                  return left.kind_id < right.kind_id;
              });
    return result;
}

}  // namespace match::cardschema
