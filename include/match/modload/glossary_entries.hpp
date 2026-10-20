#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "match/modload/artifacts.hpp"

/**
 * @file glossary_entries.hpp
 * @brief The glossary entries of a mod: the key names of the file kind, the
 *        text key pattern, and the list that joins the automatic entries of
 *        the cards with the explicit entries of the glossary files.
 *
 * ONE table. The loader, the validator, the defs builder, and the tests read
 * the names from here. The JSON schema
 * `contract/schemas/glossary.schema.json` repeats the names; a test fails
 * when the schema and this table differ.
 *
 * A glossary entry holds KEYS only. The texts come from the `texts` files of
 * the same mod. Two sources make entries:
 *
 * - AUTOMATIC: each card definition `<mod>:<card>` makes the entry with the
 *   same id. Its label key is the `label_key` field of the card file, or
 *   `<mod>:<card>.label` when the field is absent. Its description key is the
 *   `description_key` field, or `<mod>:<card>.description`.
 * - EXPLICIT: the file `glossary/<name>.json`. An explicit entry replaces the
 *   automatic entry with the same id. It is possible only inside the same
 *   mod: the id of an entry must be in the namespace of the mod that ships
 *   the file, and only the `uni` mod uses the namespace `uni`.
 *
 * Example file (format version 1):
 *
 *     {
 *       "uni:format_version": "1.0.0",
 *       "uni:glossary_entry": {
 *         "uni:description": { "uni:identifier": "my_mod:foo" },
 *         "uni:label_key": "my_mod:foo.label",
 *         "uni:description_key": "my_mod:foo.description"
 *       }
 *     }
 */

namespace match::modload {

namespace glossary_keys {
inline constexpr std::string_view kFormatVersion = "uni:format_version";
inline constexpr std::string_view kGlossaryEntry = "uni:glossary_entry";
inline constexpr std::string_view kDescription = "uni:description";
inline constexpr std::string_view kIdentifier = "uni:identifier";
inline constexpr std::string_view kLabelKey = "uni:label_key";
inline constexpr std::string_view kDescriptionKey = "uni:description_key";

/** @brief The optional fields of a card file (old style, no prefix). */
inline constexpr std::string_view kCardLabelKey = "label_key";
inline constexpr std::string_view kCardDescriptionKey = "description_key";

inline constexpr std::string_view kLabelSuffix = ".label";
inline constexpr std::string_view kDescriptionSuffix = ".description";

/** @brief A text key is at most this long, in bytes. */
inline constexpr std::size_t kMaxKeyBytes = 96;
}  // namespace glossary_keys

/**
 * @brief True for a text key: a namespace of 1 to 32 characters of
 *        `[a-z0-9_]`, a colon, and one or more words of `[a-z0-9_]` that a dot
 *        splits, at most 96 bytes in all.
 *
 * A text, a space, a capital letter, and an empty word do not match.
 */
bool IsValidTextKey(std::string_view key);

enum class GlossarySource { kCard, kExplicit };

/** @brief `card` or `explicit`: the name in the `defs` packet. */
std::string_view GlossarySourceName(GlossarySource source);

struct GlossaryEntryView {
    std::string id;               /**< Full `namespace:name`. */
    std::string label_key;
    std::string description_key;
    GlossarySource source = GlossarySource::kCard;
};

/**
 * @brief The entries of one mod, sorted by id: one automatic entry for each
 *        active card, and the explicit entries. An explicit entry replaces the
 *        automatic entry with the same id.
 *
 * A card whose `requires_mods` are not all in `active_mod_ids` is skipped.
 */
std::vector<GlossaryEntryView> BuildGlossaryEntries(
    const LoadedMod& mod, const std::vector<std::string>& active_mod_ids);

/**
 * @brief The keys that the entries of the mod need in `en.json`: the keys of
 *        the explicit entries, and the keys that an active card file declares
 *        (a derived key is not needed). The keys of a replaced automatic entry
 *        are not listed.
 */
std::set<std::string> UsedGlossaryKeys(
    const LoadedMod& mod, const std::vector<std::string>& active_mod_ids);

}  // namespace match::modload
