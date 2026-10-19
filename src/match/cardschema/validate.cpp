#include <match/cardschema/validate.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <match/cardschema/internal.hpp>
#include <match/cardschema/resolve.hpp>
#include <match/expr/checker.hpp>
#include <match/expr/diagnostic.hpp>
#include <match/expr/parser.hpp>
#include <match/expr/types.hpp>

namespace match::cardschema {

namespace {

using internal::AddError;
using internal::kMaximumDiagnostics;
using internal::MemberLocal;

// INFO: the pointer a member error is reported at: the member's `attribute` key
// when the loader recorded one, the root pointer for an in-memory member.
std::string MemberPointer(const MemberDef& member) {
    if (!member.attribute_pointer.empty()) {
        return member.attribute_pointer;
    }
    return std::string(match::expr::kRootPointer);
}

// INFO: a property stores no pointer to its own object; a property error is
// reported at the recorded key it concerns, or at the root pointer for an
// in-memory property that carries none.
std::string PropertyTypePointer(const PropertyDef& property) {
    if (!property.type_pointer.empty()) {
        return property.type_pointer;
    }
    return std::string(match::expr::kRootPointer);
}

std::string PropertyAttributePointer(const PropertyDef& property) {
    if (!property.attribute_pointer.empty()) {
        return property.attribute_pointer;
    }
    return std::string(match::expr::kRootPointer);
}

std::optional<match::expr::Type> PropertyTypeOf(const std::string& type) {
    static const std::pair<const char*, match::expr::Type> table[] = {
        {"int", match::expr::Type::kInt},        {"float", match::expr::Type::kFloat},
        {"boolean", match::expr::Type::kBool},   {"string", match::expr::Type::kString},
        {"member", match::expr::Type::kMemberSet},
    };
    for (const auto& [name, expr_type] : table) {
        if (type == name) {
            return expr_type;
        }
    }
    return std::nullopt;
}

// INFO: the keys of a sorted map, in map order, for a did-you-mean candidate list.
template <typename Map>
std::vector<std::string> MapKeys(const Map& map) {
    std::vector<std::string> keys;
    keys.reserve(map.size());
    for (const auto& entry : map) {
        keys.push_back(entry.first);
    }
    return keys;
}

}  // namespace

std::vector<Diagnostic> ValidateCatalog(const std::vector<ModSchema>& mods) {
    std::vector<Diagnostic> diagnostics;

    std::map<std::string, const AttributeDef*> attributes;
    for (const ModSchema& mod : mods) {
        for (const AttributeDef& attribute : mod.attributes) {
            // INFO: the index keeps the first declaration of an identifier; the
            // loader rejects a duplicate, so the first is the only one.
            attributes.emplace(attribute.identifier, &attribute);
        }
    }
    const std::vector<std::string> attribute_names = MapKeys(attributes);

    // INFO: the slot names of a kind are collected once, when the kind enters
    // the index, instead of once per deck entry that names it.
    std::map<std::string, const KindTemplate*> kinds;
    std::map<std::string, std::set<std::string>> kind_slot_names;
    for (const ModSchema& mod : mods) {
        for (const KindTemplate& kind : mod.kinds) {
            // INFO: the index keeps the first declaration of an identifier; the
            // loader rejects a duplicate, so the first is the only one.
            if (!kinds.emplace(kind.identifier, &kind).second) {
                continue;
            }
            std::set<std::string> slot_names;
            for (const SlotBinding& binding : kind.slots) {
                slot_names.insert(binding.name);
            }
            kind_slot_names.emplace(kind.identifier, std::move(slot_names));
        }
    }
    const std::vector<std::string> kind_names = MapKeys(kinds);

    // INFO: members are keyed by (attribute, local name); the first member of a
    // key is remembered so a second one from another mod names both sources. A
    // collision inside one mod is the catalog's own report, so it is skipped
    // here to avoid a second wording for the same problem.
    std::map<std::pair<std::string, std::string>, const MemberDef*> first_by_local;
    for (const ModSchema& mod : mods) {
        for (const MemberDef& member : mod.members) {
            const auto attribute = attributes.find(member.attribute);
            if (attribute == attributes.end()) {
                AddError(diagnostics, member.file, MemberPointer(member),
                         "unknown attribute \"" + member.attribute + "\"",
                         match::expr::SuggestClosest(member.attribute, attribute_names));
            } else if (!attribute->second->extensible &&
                       NamespaceOf(member.identifier) !=
                           NamespaceOf(attribute->second->identifier)) {
                AddError(diagnostics, member.file, MemberPointer(member),
                         "attribute \"" + attribute->second->identifier +
                             "\" is not extensible; member \"" + member.identifier +
                             "\" belongs to another mod");
            }

            const std::string local = MemberLocal(member);
            const auto insertion =
                first_by_local.emplace(std::make_pair(member.attribute, local), &member);
            if (insertion.second) {
                continue;
            }
            const MemberDef* first = insertion.first->second;
            if (NamespaceOf(first->identifier) == NamespaceOf(member.identifier)) {
                continue;
            }
            AddError(diagnostics, member.file, MemberPointer(member),
                     "members \"" + first->identifier + "\" and \"" + member.identifier +
                         "\" share the local name \"" + local +
                         "\"; set identifier_alias on one");
        }
    }

    // INFO: a property identifier is namespaced, so it can only be declared
    // twice inside one mod. The duplicate is reported at the property's `type`
    // key and an unknown `type: member` attribute at its `attribute` key.
    for (const ModSchema& mod : mods) {
        std::set<std::string> seen;
        for (const PropertyDef& property : mod.properties) {
            if (!seen.insert(property.identifier).second) {
                AddError(diagnostics, property.file, PropertyTypePointer(property),
                         "property \"" + property.identifier + "\" is declared twice in mod \"" +
                             mod.mod_id + "\"");
            }
            if (property.type == "member" &&
                attributes.find(property.member_attribute) == attributes.end()) {
                AddError(diagnostics, property.file, PropertyAttributePointer(property),
                         "property \"" + property.identifier + "\" names unknown attribute \"" +
                             property.member_attribute + "\"",
                         match::expr::SuggestClosest(property.member_attribute, attribute_names));
            }
        }
    }

    // INFO: a deck entry names a template and, per selector, a slot the template
    // owns. The unknown-kind and abstract-kind wording matches ExpandDeck's, so
    // the caller's DedupeDiagnostics removes the exact repeat.
    for (const ModSchema& mod : mods) {
        for (const DeckSpec& deck : mod.decks) {
            for (const DeckEntry& entry : deck.cards) {
                const auto found = kinds.find(entry.kind);
                if (found == kinds.end()) {
                    AddError(diagnostics, deck.file, entry.pointer,
                             "unknown kind \"" + entry.kind + "\"",
                             match::expr::SuggestClosest(entry.kind, kind_names));
                    continue;
                }
                const KindTemplate& kind = *found->second;
                if (kind.is_abstract) {
                    AddError(diagnostics, deck.file, entry.pointer,
                             "kind \"" + entry.kind + "\" is abstract");
                    continue;
                }
                const std::set<std::string>& slot_names = kind_slot_names.at(entry.kind);
                const std::vector<std::string> slot_candidates(slot_names.begin(),
                                                               slot_names.end());
                for (const auto& [slot, selector] : entry.members) {
                    if (slot_names.count(slot) != 0) {
                        continue;
                    }
                    const std::string pointer =
                        selector.pointer.empty()
                            ? match::expr::AppendPointerToken(
                                  match::expr::AppendPointerToken(entry.pointer, "members"), slot)
                            : selector.pointer;
                    AddError(diagnostics, deck.file, pointer,
                             "unknown slot \"" + slot + "\" for kind \"" + entry.kind + "\"",
                             match::expr::SuggestClosest(slot, slot_candidates));
                }
            }
        }
    }

    // INFO: the card schema of a `when`: one non-optional `card` variable, the
    // card-scope properties of every mod, and the slot names of every template.
    match::expr::Schema schema;
    schema.variables["card"] = match::expr::VariableInfo{match::expr::Type::kCard, false};
    for (const ModSchema& mod : mods) {
        for (const PropertyDef& property : mod.properties) {
            if (property.scope != "card") {
                continue;
            }
            if (const std::optional<match::expr::Type> type = PropertyTypeOf(property.type)) {
                schema.card_properties[property.identifier] = *type;
            }
        }
        for (const KindTemplate& kind : mod.kinds) {
            for (const SlotBinding& binding : kind.slots) {
                if (!binding.name.empty()) {
                    schema.card_slots[binding.name] = match::expr::Type::kMemberSet;
                }
            }
        }
    }

    const auto check_contribution = [&diagnostics, &schema](const std::string& file,
                                                            const Contribution& contribution) {
        if (!contribution.when) {
            return;
        }
        // INFO: once the report holds the cap plus the truncation marker there is
        // nothing left to add, so the remaining contributions are skipped.
        if (diagnostics.size() > kMaximumDiagnostics) {
            return;
        }
        const std::string pointer = match::expr::AppendPointerToken(
            contribution.pointer.empty() ? std::string(match::expr::kRootPointer)
                                         : contribution.pointer,
            "when");
        // INFO: the loader already parses every `when` and owns its syntax error.
        // Re-checking the source here would emit the same line a second time for
        // a caller that concatenates both reports.
        const match::expr::ParseResult parsed = match::expr::ParseExpression(*contribution.when);
        if (parsed.error) {
            return;
        }
        const match::expr::CheckResult checked = match::expr::CheckParsedExpression(
            *parsed.root, schema, match::expr::Type::kBool, file, pointer,
            match::expr::GuardState{});
        for (const Diagnostic& diagnostic : checked.diagnostics) {
            AddError(diagnostics, diagnostic.file, diagnostic.pointer, diagnostic.message,
                     diagnostic.suggestion);
            if (diagnostics.size() > kMaximumDiagnostics) {
                return;
            }
        }
    };

    for (const ModSchema& mod : mods) {
        for (const KindTemplate& kind : mod.kinds) {
            for (const auto& [slot, contribution] : kind.contribute) {
                check_contribution(kind.file, contribution);
            }
        }
        for (const MemberDef& member : mod.members) {
            for (const auto& [slot, contribution] : member.contribute) {
                check_contribution(member.file, contribution);
            }
        }
        for (const DeckSpec& deck : mod.decks) {
            for (const auto& [slot, contribution] : deck.contribute) {
                check_contribution(deck.file, contribution);
            }
        }
    }

    DedupeDiagnostics(diagnostics);
    return diagnostics;
}

}  // namespace match::cardschema
