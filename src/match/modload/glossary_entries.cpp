#include "match/modload/glossary_entries.hpp"

#include <algorithm>
#include <map>

/**
 * @file glossary_entries.cpp
 * @brief Text key pattern and the glossary entry list of a mod.
 */

namespace match::modload {

namespace {

constexpr std::size_t kMaxNamespaceBytes = 32;

bool IsWordCharacter(char character) {
    return (character >= 'a' && character <= 'z')
           || (character >= '0' && character <= '9') || character == '_';
}

bool IsDottedWords(std::string_view text) {
    if (text.empty()) return false;
    bool word_has_character = false;
    for (const char character : text) {
        if (character == '.') {
            if (!word_has_character) return false;
            word_has_character = false;
        } else if (IsWordCharacter(character)) {
            word_has_character = true;
        } else {
            return false;
        }
    }
    return word_has_character;
}

}  // namespace

bool IsValidTextKey(std::string_view key) {
    if (key.empty() || key.size() > glossary_keys::kMaxKeyBytes) return false;
    const std::size_t colon = key.find(':');
    if (colon == std::string_view::npos || colon == 0
        || colon > kMaxNamespaceBytes) {
        return false;
    }
    const std::string_view name_space = key.substr(0, colon);
    return std::all_of(name_space.begin(), name_space.end(), IsWordCharacter)
           && IsDottedWords(key.substr(colon + 1));
}

std::string_view GlossarySourceName(GlossarySource source) {
    return source == GlossarySource::kCard ? "card" : "explicit";
}

std::vector<GlossaryEntryView> BuildGlossaryEntries(const LoadedMod& mod) {
    std::map<std::string, GlossaryEntryView> by_id;
    for (const CardDef& card : mod.cards) {
        GlossaryEntryView entry;
        entry.id = card.kind_id;
        entry.label_key =
            card.label_key.empty()
                ? card.kind_id + std::string(glossary_keys::kLabelSuffix)
                : card.label_key;
        entry.description_key =
            card.description_key.empty()
                ? card.kind_id
                      + std::string(glossary_keys::kDescriptionSuffix)
                : card.description_key;
        entry.source = GlossarySource::kCard;
        by_id[entry.id] = std::move(entry);
    }
    for (const GlossaryEntryDef& explicit_entry : mod.glossary) {
        GlossaryEntryView entry;
        entry.id = explicit_entry.entry_id;
        entry.label_key = explicit_entry.label_key;
        entry.description_key = explicit_entry.description_key;
        entry.source = GlossarySource::kExplicit;
        by_id[entry.id] = std::move(entry);
    }
    std::vector<GlossaryEntryView> entries;
    entries.reserve(by_id.size());
    for (auto& [id, entry] : by_id) entries.push_back(std::move(entry));
    return entries;
}

std::set<std::string> UsedGlossaryKeys(const LoadedMod& mod) {
    std::set<std::string> used;
    for (const GlossaryEntryView& entry : BuildGlossaryEntries(mod)) {
        if (entry.source == GlossarySource::kExplicit) {
            used.insert(entry.label_key);
            used.insert(entry.description_key);
        }
    }
    for (const CardDef& card : mod.cards) {
        const bool replaced = std::any_of(
            mod.glossary.begin(), mod.glossary.end(),
            [&](const GlossaryEntryDef& entry) {
                return entry.entry_id == card.kind_id;
            });
        if (replaced) continue;
        if (!card.label_key.empty()) used.insert(card.label_key);
        if (!card.description_key.empty()) used.insert(card.description_key);
    }
    return used;
}

}  // namespace match::modload
