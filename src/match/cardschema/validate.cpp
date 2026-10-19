#include <match/cardschema/validate.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <match/cardschema/resolve.hpp>
#include <match/expr/checker.hpp>
#include <match/expr/diagnostic.hpp>
#include <match/expr/types.hpp>

namespace match::cardschema {

namespace {

// INFO: a hostile mod set cannot flood the report; the same cap the catalog uses.
constexpr std::size_t kMaximumDiagnostics = 100;

void AddError(std::vector<Diagnostic>& diagnostics, const std::string& file,
              const std::string& pointer, std::string message, std::string suggestion = {}) {
    if (diagnostics.size() >= kMaximumDiagnostics) {
        return;
    }
    diagnostics.push_back(
        Diagnostic{file, pointer, std::move(message), std::move(suggestion)});
}

// INFO: the name a member takes in an identifier: its alias when it sets one,
// otherwise the local part of its identifier. Two members collide only when the
// names they actually use are equal.
std::string MemberLocal(const MemberDef& member) {
    if (!member.identifier_alias.empty()) {
        return member.identifier_alias;
    }
    return LocalNameOf(member.identifier);
}

std::optional<match::expr::Type> PropertyTypeOf(const std::string& type) {
    if (type == "int") {
        return match::expr::Type::kInt;
    }
    if (type == "float") {
        return match::expr::Type::kFloat;
    }
    if (type == "boolean") {
        return match::expr::Type::kBool;
    }
    if (type == "string") {
        return match::expr::Type::kString;
    }
    if (type == "member") {
        return match::expr::Type::kMemberSet;
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
            attributes.emplace(attribute.identifier, &attribute);
        }
    }
    const std::vector<std::string> attribute_names = MapKeys(attributes);

    std::map<std::string, const KindTemplate*> kinds;
    for (const ModSchema& mod : mods) {
        for (const KindTemplate& kind : mod.kinds) {
            kinds.emplace(kind.identifier, &kind);
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
                AddError(diagnostics, member.file, std::string(match::expr::kRootPointer),
                         "unknown attribute \"" + member.attribute + "\"",
                         match::expr::SuggestClosest(member.attribute, attribute_names));
            } else if (!attribute->second->extensible &&
                       NamespaceOf(member.identifier) !=
                           NamespaceOf(attribute->second->identifier)) {
                AddError(diagnostics, member.file, std::string(match::expr::kRootPointer),
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
            AddError(diagnostics, member.file, std::string(match::expr::kRootPointer),
                     "members \"" + first->identifier + "\" and \"" + member.identifier +
                         "\" share the local name \"" + local +
                         "\"; set identifier_alias on one");
        }
    }

    // INFO: a property identifier is namespaced, so it can only be declared
    // twice inside one mod; that duplicate is reported. A `type: member`
    // property must name an attribute some mod declares.
    for (const ModSchema& mod : mods) {
        std::set<std::string> seen;
        for (const PropertyDef& property : mod.properties) {
            if (!seen.insert(property.identifier).second) {
                AddError(diagnostics, property.file, std::string(match::expr::kRootPointer),
                         "property \"" + property.identifier + "\" is declared twice in mod \"" +
                             mod.mod_id + "\"");
            }
            if (property.type == "member" &&
                attributes.find(property.member_attribute) == attributes.end()) {
                AddError(diagnostics, property.file, std::string(match::expr::kRootPointer),
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
                std::vector<std::string> slot_names;
                slot_names.reserve(kind.slots.size());
                for (const SlotBinding& binding : kind.slots) {
                    slot_names.push_back(binding.name);
                }
                for (const auto& [slot, selector] : entry.members) {
                    const bool known = std::find_if(
                                           kind.slots.begin(), kind.slots.end(),
                                           [&slot](const SlotBinding& binding) {
                                               return binding.name == slot;
                                           }) != kind.slots.end();
                    if (known) {
                        continue;
                    }
                    const std::string pointer =
                        selector.pointer.empty()
                            ? match::expr::AppendPointerToken(
                                  match::expr::AppendPointerToken(entry.pointer, "members"), slot)
                            : selector.pointer;
                    AddError(diagnostics, deck.file, pointer,
                             "unknown slot \"" + slot + "\" for kind \"" + entry.kind + "\"",
                             match::expr::SuggestClosest(slot, slot_names));
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
        const std::string pointer = match::expr::AppendPointerToken(
            contribution.pointer.empty() ? std::string(match::expr::kRootPointer)
                                         : contribution.pointer,
            "when");
        const match::expr::CheckResult checked = match::expr::CheckExpression(
            *contribution.when, schema, match::expr::Type::kBool, file, pointer);
        for (const Diagnostic& diagnostic : checked.diagnostics) {
            if (diagnostics.size() >= kMaximumDiagnostics) {
                return;
            }
            diagnostics.push_back(diagnostic);
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
