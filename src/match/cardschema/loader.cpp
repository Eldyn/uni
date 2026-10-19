#include <match/cardschema/loader.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <match/expr/diagnostic.hpp>
#include <match/expr/parser.hpp>
#include <match/expr/schema_json.hpp>

namespace match::cardschema {

namespace {

namespace fs = std::filesystem;

constexpr const char* kFormatVersion = "1.0.0";

// INFO: the loader reads at most this many bytes per file; a larger file is
// rejected before it is read, and the read is capped one byte past the limit so
// a file that grows between the size check and the read cannot slip through.
constexpr std::uintmax_t kMaximumCardFileBytes = 1048576;

// INFO: a hopeless file can produce thousands of errors; after this many the
// loader emits one final marker instead, so a bad mod cannot flood the report.
constexpr std::size_t kMaximumDiagnostics = 100;

const std::vector<std::string>& RootKeys() {
    static const std::vector<std::string> keys = {"attribute", "attribute_member", "kind",
                                                  "deck", "property"};
    return keys;
}

std::vector<std::string> AllowedRootsFor(const std::string& folder) {
    if (folder == "attributes") {
        return {"attribute", "attribute_member"};
    }
    if (folder == "kinds") {
        return {"kind"};
    }
    if (folder == "decks") {
        return {"deck"};
    }
    if (folder == "properties") {
        return {"property"};
    }
    return {};
}

std::vector<std::string> RootKeyCandidates(const std::string& folder) {
    std::vector<std::string> candidates = {"format_version"};
    for (const std::string& key : AllowedRootsFor(folder)) {
        candidates.push_back(key);
    }
    return candidates;
}

bool Contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string PointerFor(const std::string& key) {
    return match::expr::AppendPointerToken(match::expr::kRootPointer, key);
}

std::string PathLabel(const std::string& mod_id, const fs::path& relative) {
    return "mods/" + mod_id + "/" + relative.generic_string();
}

std::string RelativeLabel(const std::string& mod_id, const fs::path& mod_directory,
                          const fs::path& path) {
    return PathLabel(mod_id, path.lexically_relative(mod_directory));
}

void AddError(std::vector<Diagnostic>& diagnostics, const std::string& file,
              const std::string& pointer, std::string message,
              std::string suggestion = {}) {
    if (diagnostics.size() >= kMaximumDiagnostics) {
        if (diagnostics.size() == kMaximumDiagnostics) {
            diagnostics.push_back(Diagnostic{file, pointer, "more errors not shown", {}});
        }
        return;
    }
    diagnostics.push_back(
        Diagnostic{file, pointer, std::move(message), std::move(suggestion)});
}

void CheckUnknownKeys(const json& object, const std::vector<std::string>& known,
                      const std::string& file, const std::string& pointer,
                      std::vector<Diagnostic>& diagnostics) {
    for (const auto& entry : object.items()) {
        if (Contains(known, entry.key())) {
            continue;
        }
        AddError(diagnostics, file,
                 match::expr::AppendPointerToken(pointer, entry.key()),
                 "unknown key \"" + entry.key() + "\"",
                 match::expr::SuggestClosest(entry.key(), known));
    }
}

std::optional<std::string> ReadString(const json& object, const char* key,
                                      const std::string& file, const std::string& pointer,
                                      std::vector<Diagnostic>& diagnostics,
                                      bool required) {
    const auto entry = object.find(key);
    const std::string key_pointer = match::expr::AppendPointerToken(pointer, key);
    if (entry == object.end()) {
        if (required) {
            AddError(diagnostics, file, key_pointer,
                     "missing required key \"" + std::string(key) + "\"");
        }
        return std::nullopt;
    }
    if (!entry->is_string()) {
        AddError(diagnostics, file, key_pointer,
                 "key \"" + std::string(key) + "\" must be a string");
        return std::nullopt;
    }
    return entry->get<std::string>();
}

std::string QuotedValues(const std::vector<std::string>& values) {
    std::string text;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            text += ", ";
        }
        text += "\"" + values[index] + "\"";
    }
    return text;
}

// INFO: range bounds are whole numbers in practice, so `%g` keeps `[0, 5]`
// readable instead of `[0.000000, 5.000000]`.
std::string FormatNumber(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

std::optional<std::string> ReadEnum(const json& object, const char* key,
                                    const std::vector<std::string>& candidates,
                                    const std::string& file, const std::string& pointer,
                                    std::vector<Diagnostic>& diagnostics, bool required = false,
                                    const char* fallback_suggestion = nullptr) {
    const std::optional<std::string> value =
        ReadString(object, key, file, pointer, diagnostics, required);
    if (!value) {
        return std::nullopt;
    }
    if (!Contains(candidates, *value)) {
        std::string suggestion = match::expr::SuggestClosest(*value, candidates);
        // INFO: a short word can sit past the edit-distance threshold of every
        // candidate (e.g. "crad" and "card"); a single-value enum names its one
        // plausible answer instead of printing no suggestion.
        if (suggestion.empty() && fallback_suggestion != nullptr) {
            suggestion = fallback_suggestion;
        }
        AddError(diagnostics, file, match::expr::AppendPointerToken(pointer, key),
                 "key \"" + std::string(key) + "\" must be one of " +
                     QuotedValues(candidates),
                 suggestion);
        return std::nullopt;
    }
    return value;
}

bool ValidateIdentifier(const std::string& identifier, const std::string& mod_id,
                        const std::string& file, const std::string& pointer,
                        const char* what, std::vector<Diagnostic>& diagnostics) {
    if (!IsValidIdentifier(identifier)) {
        AddError(diagnostics, file, pointer,
                 std::string(what) + " \"" + identifier +
                     "\" is not a valid identifier; a local name uses [a-z0-9_] "
                     "(1 to 32 characters)");
        return false;
    }
    if (NamespaceOf(identifier) != mod_id) {
        AddError(diagnostics, file, pointer,
                 std::string(what) + " \"" + identifier + "\" must use the namespace \"" +
                     mod_id + "\"");
        return false;
    }
    return true;
}

// INFO: an id declared by a file must keep the file greppable: the local name of
// the declared identifier equals the file stem, so `attributes/color.json`
// declares `uni:color` and `attributes/color/red.json` declares `uni:red`.
void CheckIdentifierMatchesFile(const std::string& identifier, const std::string& stem,
                                const std::string& file, const std::string& id_pointer,
                                std::vector<Diagnostic>& diagnostics) {
    if (LocalNameOf(identifier) == stem) {
        return;
    }
    AddError(diagnostics, file, id_pointer,
             "identifier \"" + identifier + "\" does not match the file name \"" + stem +
                 ".json\"");
}

const std::vector<std::string>& MemberKeys(bool allow_attribute) {
    static const std::vector<std::string> standalone = {
        "attribute", "identifier", "display_name", "tags", "order",
        "contribute", "properties", "identifier_alias"};
    static const std::vector<std::string> inline_keys = {
        "identifier", "display_name", "tags", "order",
        "contribute", "properties", "identifier_alias"};
    return allow_attribute ? standalone : inline_keys;
}

const std::vector<std::string>& KindKeys() {
    static const std::vector<std::string> keys = {
        "identifier", "abstract", "extends", "tags", "attributes", "symmetric",
        "slot_order", "identifier_format", "title_format", "face", "behavior",
        "window", "auto_trigger", "label_key", "description_key", "properties",
        "contribute"};
    return keys;
}

const std::vector<std::string>& SlotKeys() {
    static const std::vector<std::string> keys = {"attribute", "name", "surface"};
    return keys;
}

void ReadStringArray(const json& object, const char* key, const char* element_error,
                     const char* duplicate_prefix, const std::string& file,
                     const std::string& pointer, std::vector<Diagnostic>& diagnostics,
                     std::vector<std::string>& out) {
    const auto entry = object.find(key);
    if (entry == object.end()) {
        return;
    }
    const std::string array_pointer = match::expr::AppendPointerToken(pointer, key);
    if (!entry->is_array()) {
        AddError(diagnostics, file, array_pointer,
                 "key \"" + std::string(key) + "\" must be an array");
        return;
    }
    std::set<std::string> seen;
    for (std::size_t index = 0; index < entry->size(); ++index) {
        const json& item = (*entry)[index];
        const std::string element_pointer =
            match::expr::AppendPointerToken(array_pointer, std::to_string(index));
        if (!item.is_string() || item.get<std::string>().empty()) {
            AddError(diagnostics, file, element_pointer, element_error);
            continue;
        }
        const std::string text = item.get<std::string>();
        if (!seen.insert(text).second) {
            AddError(diagnostics, file, element_pointer,
                     std::string(duplicate_prefix) + " \"" + text + "\"");
            continue;
        }
        out.push_back(text);
    }
}

void ParseKindSlots(const json& value, const std::string& file, const std::string& pointer,
                    std::vector<Diagnostic>& diagnostics, KindTemplate& kind,
                    std::set<std::string>& declared_slot_names) {
    const auto attributes = value.find("attributes");
    if (attributes == value.end()) {
        return;
    }
    const std::string attributes_pointer =
        match::expr::AppendPointerToken(pointer, "attributes");
    if (!attributes->is_array()) {
        AddError(diagnostics, file, attributes_pointer, "key \"attributes\" must be an array");
        return;
    }

    std::set<std::string> names;
    for (std::size_t index = 0; index < attributes->size(); ++index) {
        const json& binding = (*attributes)[index];
        const std::string binding_pointer =
            match::expr::AppendPointerToken(attributes_pointer, std::to_string(index));
        if (!binding.is_object()) {
            AddError(diagnostics, file, binding_pointer,
                     "attribute binding must be an object");
            continue;
        }
        CheckUnknownKeys(binding, SlotKeys(), file, binding_pointer, diagnostics);

        const std::optional<std::string> attribute =
            ReadString(binding, "attribute", file, binding_pointer, diagnostics, true);
        const std::optional<std::string> declared_name =
            ReadString(binding, "name", file, binding_pointer, diagnostics, false);

        // INFO: the intended slot name is recorded even when a later check
        // rejects the binding, so a symmetric group that names it does not
        // report a second, cascading "unknown slot" error for a slot the author
        // did declare.
        if (declared_name) {
            declared_slot_names.insert(*declared_name);
        } else if (attribute) {
            declared_slot_names.insert(LocalNameOf(*attribute));
        }

        if (!attribute) {
            continue;
        }
        if (!IsValidIdentifier(*attribute)) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(binding_pointer, "attribute"),
                     "attribute \"" + *attribute + "\" is not a valid identifier");
            continue;
        }

        const std::string slot_name = declared_name ? *declared_name : LocalNameOf(*attribute);
        if (declared_name && !IsValidLocalName(*declared_name)) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(binding_pointer, "name"),
                     "slot name \"" + *declared_name +
                         "\" must use [a-z0-9_] (1 to 32 characters)");
            continue;
        }
        if (!names.insert(slot_name).second) {
            AddError(diagnostics, file, binding_pointer,
                     "duplicate slot name \"" + slot_name + "\"");
            continue;
        }

        SlotBinding slot;
        slot.attribute = *attribute;
        slot.name = slot_name;
        if (const std::optional<std::string> surface =
                ReadString(binding, "surface", file, binding_pointer, diagnostics, false)) {
            slot.surface = *surface;
        }
        kind.slots.push_back(std::move(slot));
    }
}

void ParseKindSymmetric(const json& value, const std::string& file, const std::string& pointer,
                        std::vector<Diagnostic>& diagnostics, KindTemplate& kind) {
    const auto symmetric = value.find("symmetric");
    if (symmetric == value.end()) {
        return;
    }
    const std::string symmetric_pointer =
        match::expr::AppendPointerToken(pointer, "symmetric");
    if (!symmetric->is_array()) {
        AddError(diagnostics, file, symmetric_pointer, "key \"symmetric\" must be an array");
        return;
    }
    std::set<std::string> grouped_slots;
    for (std::size_t group_index = 0; group_index < symmetric->size(); ++group_index) {
        const json& group = (*symmetric)[group_index];
        const std::string group_pointer =
            match::expr::AppendPointerToken(symmetric_pointer, std::to_string(group_index));
        if (!group.is_array()) {
            AddError(diagnostics, file, group_pointer,
                     "symmetric entry must be an array");
            continue;
        }
        std::vector<std::string> names;
        std::set<std::string> names_in_group;
        bool group_valid = true;
        for (std::size_t name_index = 0; name_index < group.size(); ++name_index) {
            const json& name = group[name_index];
            const std::string name_pointer =
                match::expr::AppendPointerToken(group_pointer, std::to_string(name_index));
            if (!name.is_string() || name.get<std::string>().empty()) {
                AddError(diagnostics, file, name_pointer,
                         "symmetric group entry must be a non-empty string");
                group_valid = false;
                continue;
            }
            const std::string text = name.get<std::string>();
            if (!names_in_group.insert(text).second) {
                AddError(diagnostics, file, name_pointer,
                         "duplicate slot name \"" + text + "\" in symmetric group");
                group_valid = false;
                continue;
            }
            if (grouped_slots.count(text) != 0) {
                AddError(diagnostics, file, name_pointer,
                         "slot \"" + text + "\" appears in more than one symmetric group");
                group_valid = false;
                continue;
            }
            names.push_back(text);
        }
        // INFO: a group with a bad entry is dropped; pushing it would trigger a
        // second, misleading "at least 2 slots" error for the same group.
        if (!group_valid) {
            continue;
        }
        if (names.size() < 2) {
            AddError(diagnostics, file, group_pointer,
                     "symmetric group must name at least 2 slots");
            continue;
        }
        for (const std::string& name : names) {
            grouped_slots.insert(name);
        }
        kind.symmetric_groups.push_back(std::move(names));
    }
}

void CheckSymmetricSlots(const std::string& file, const std::string& pointer,
                         std::vector<Diagnostic>& diagnostics, const KindTemplate& kind,
                         const std::set<std::string>& declared_slot_names) {
    const std::string symmetric_pointer =
        match::expr::AppendPointerToken(pointer, "symmetric");
    for (std::size_t group_index = 0; group_index < kind.symmetric_groups.size();
         ++group_index) {
        const std::vector<std::string>& group = kind.symmetric_groups[group_index];
        for (std::size_t name_index = 0; name_index < group.size(); ++name_index) {
            if (declared_slot_names.count(group[name_index]) != 0) {
                continue;
            }
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(
                         match::expr::AppendPointerToken(symmetric_pointer,
                                                         std::to_string(group_index)),
                         std::to_string(name_index)),
                     "symmetric group names unknown slot \"" + group[name_index] + "\"");
        }
    }
}

bool ParseKind(const json& value, const std::string& mod_id, const std::string& file,
               const std::string& pointer, std::vector<Diagnostic>& diagnostics,
               KindTemplate& kind) {
    if (!value.is_object()) {
        AddError(diagnostics, file, pointer, "kind must be an object");
        return false;
    }
    const std::size_t initial_diagnostics = diagnostics.size();
    CheckUnknownKeys(value, KindKeys(), file, pointer, diagnostics);

    bool usable = false;
    const std::optional<std::string> identifier =
        ReadString(value, "identifier", file, pointer, diagnostics, true);
    if (identifier) {
        kind.identifier = *identifier;
        usable = ValidateIdentifier(*identifier, mod_id, file,
                                    match::expr::AppendPointerToken(pointer, "identifier"),
                                    "kind identifier", diagnostics);
    }

    const auto abstract = value.find("abstract");
    if (abstract != value.end()) {
        if (!abstract->is_boolean()) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "abstract"),
                     "key \"abstract\" must be a boolean");
        } else {
            kind.is_abstract = abstract->get<bool>();
        }
    }

    if (const std::optional<std::string> parent =
            ReadString(value, "extends", file, pointer, diagnostics, false)) {
        kind.extends = *parent;
        if (!IsValidIdentifier(*parent)) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "extends"),
                     "extends \"" + *parent +
                         "\" is not a valid identifier; a local name uses [a-z0-9_] "
                         "(1 to 32 characters)");
        } else if (*parent == kind.identifier) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "extends"),
                     "a kind cannot extend itself");
        }
    }

    ReadStringArray(value, "tags", "tag must be a non-empty string", "duplicate tag", file,
                    pointer, diagnostics, kind.tags);
    std::set<std::string> declared_slot_names;
    ParseKindSlots(value, file, pointer, diagnostics, kind, declared_slot_names);
    ParseKindSymmetric(value, file, pointer, diagnostics, kind);
    CheckSymmetricSlots(file, pointer, diagnostics, kind, declared_slot_names);

    ReadStringArray(value, "slot_order", "slot_order entry must be a non-empty string",
                    "duplicate slot_order entry", file, pointer, diagnostics, kind.slot_order);

    // TODO(task 10): check {slot} placeholder syntax of identifier_format and title_format
    if (const std::optional<std::string> format =
            ReadString(value, "identifier_format", file, pointer, diagnostics, false)) {
        kind.identifier_format = *format;
    }
    if (const std::optional<std::string> format =
            ReadString(value, "title_format", file, pointer, diagnostics, false)) {
        kind.title_format = *format;
    }
    if (const std::optional<std::string> label_key =
            ReadString(value, "label_key", file, pointer, diagnostics, false)) {
        kind.label_key = *label_key;
    }
    if (const std::optional<std::string> description_key =
            ReadString(value, "description_key", file, pointer, diagnostics, false)) {
        kind.description_key = *description_key;
    }

    const auto face = value.find("face");
    if (face != value.end()) {
        const std::string face_pointer = match::expr::AppendPointerToken(pointer, "face");
        if (!face->is_object()) {
            AddError(diagnostics, file, face_pointer, "key \"face\" must be an object");
        } else {
            kind.face = *face;
        }
    }

    if (const auto entry = value.find("behavior"); entry != value.end()) {
        kind.behavior = *entry;
    }
    if (const auto entry = value.find("window"); entry != value.end()) {
        kind.window = *entry;
    }
    if (const auto entry = value.find("auto_trigger"); entry != value.end()) {
        kind.auto_trigger = *entry;
    }

    const auto properties = value.find("properties");
    if (properties != value.end()) {
        if (!properties->is_object()) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "properties"),
                     "key \"properties\" must be an object");
        } else {
            kind.properties = *properties;
        }
    }

    const auto contribute = value.find("contribute");
    if (contribute != value.end()) {
        if (!contribute->is_object()) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "contribute"),
                     "key \"contribute\" must be an object");
        } else {
            kind.contribute_raw = *contribute;
        }
    }

    kind.file = file;
    return usable && diagnostics.size() == initial_diagnostics;
}

const std::vector<std::string>& DeckKeys() {
    static const std::vector<std::string> keys = {"identifier", "name", "max_cards", "mods",
                                                  "contribute", "cards"};
    return keys;
}

const std::vector<std::string>& DeckEntryKeys() {
    static const std::vector<std::string> keys = {"kind", "copies", "members", "combine"};
    return keys;
}

const std::vector<std::string>& CombineModes() {
    static const std::vector<std::string> modes = {
        combine_mode::kProduct, combine_mode::kUniquePairs, combine_mode::kZip};
    return modes;
}

// INFO: a member selector is one of the three forms in spec 5.4: the string
// "all", an object with "tag", or an object with "identifiers". Any other shape
// is a diagnostic naming the selector so the author can see the form list.
void ParseMemberSelector(const json& value, const std::string& file, const std::string& pointer,
                         std::vector<Diagnostic>& diagnostics, MemberSelector& selector) {
    static const std::vector<std::string> selector_keys = {selector_mode::kTag,
                                                           selector_mode::kIdentifiers};
    static const std::string kSelectorMessage =
        "member selector must be \"all\", {\"tag\": ...}, or {\"identifiers\": [...]}";

    selector.pointer = pointer;

    if (value.is_string()) {
        if (value.get<std::string>() == selector_mode::kAll) {
            selector.mode = selector_mode::kAll;
            return;
        }
        AddError(diagnostics, file, pointer, kSelectorMessage);
        return;
    }
    if (!value.is_object()) {
        AddError(diagnostics, file, pointer, kSelectorMessage);
        return;
    }

    // INFO: an unknown key is reported at its own pointer first, so a typo such
    // as "tagg" is not hidden behind the generic selector message.
    CheckUnknownKeys(value, selector_keys, file, pointer, diagnostics);

    const bool has_tag = value.find(selector_mode::kTag) != value.end();
    const bool has_identifiers = value.find(selector_mode::kIdentifiers) != value.end();
    if (has_tag == has_identifiers) {
        AddError(diagnostics, file, pointer, kSelectorMessage);
        return;
    }

    if (has_tag) {
        const std::optional<std::string> tag =
            ReadString(value, selector_mode::kTag, file, pointer, diagnostics, true);
        if (!tag) {
            return;
        }
        const std::string tag_pointer =
            match::expr::AppendPointerToken(pointer, selector_mode::kTag);
        if (tag->empty()) {
            AddError(diagnostics, file, tag_pointer,
                     "selector tag must be a non-empty string");
            return;
        }
        if (!IsValidIdentifier(*tag) && !IsValidLocalName(*tag)) {
            AddError(diagnostics, file, tag_pointer,
                     "selector tag \"" + *tag +
                         "\" is not a valid identifier; a local name uses [a-z0-9_] "
                         "(1 to 32 characters)");
            return;
        }
        selector.mode = selector_mode::kTag;
        selector.tag = *tag;
        return;
    }

    const std::string identifiers_pointer =
        match::expr::AppendPointerToken(pointer, selector_mode::kIdentifiers);
    const auto identifiers = value.find(selector_mode::kIdentifiers);
    if (!identifiers->is_array()) {
        AddError(diagnostics, file, identifiers_pointer,
                 "key \"identifiers\" must be an array");
        return;
    }
    if (identifiers->empty()) {
        AddError(diagnostics, file, identifiers_pointer, "identifiers must not be empty");
        return;
    }
    std::set<std::string> seen;
    for (std::size_t index = 0; index < identifiers->size(); ++index) {
        const json& item = (*identifiers)[index];
        const std::string item_pointer =
            match::expr::AppendPointerToken(identifiers_pointer, std::to_string(index));
        if (!item.is_string() || item.get<std::string>().empty()) {
            AddError(diagnostics, file, item_pointer, "identifier must be a non-empty string");
            continue;
        }
        const std::string text = item.get<std::string>();
        if (!IsValidIdentifier(text)) {
            AddError(diagnostics, file, item_pointer,
                     "identifier \"" + text +
                         "\" is not a valid identifier; a local name uses [a-z0-9_] "
                         "(1 to 32 characters)");
            continue;
        }
        if (!seen.insert(text).second) {
            AddError(diagnostics, file, item_pointer, "duplicate identifier \"" + text + "\"");
            continue;
        }
        selector.identifiers.push_back(text);
    }
    selector.mode = selector_mode::kIdentifiers;
}

void ParseDeckMembers(const json& value, const std::string& file, const std::string& pointer,
                      std::vector<Diagnostic>& diagnostics, DeckEntry& entry) {
    const auto members = value.find("members");
    if (members == value.end()) {
        return;
    }
    const std::string members_pointer = match::expr::AppendPointerToken(pointer, "members");
    if (!members->is_object()) {
        AddError(diagnostics, file, members_pointer, "key \"members\" must be an object");
        return;
    }
    for (const auto& item : members->items()) {
        const std::string member_pointer =
            match::expr::AppendPointerToken(members_pointer, item.key());
        if (!IsValidLocalName(item.key())) {
            AddError(diagnostics, file, member_pointer,
                     "slot name \"" + item.key() +
                         "\" must use [a-z0-9_] (1 to 32 characters)");
            continue;
        }
        MemberSelector selector;
        ParseMemberSelector(item.value(), file, member_pointer, diagnostics, selector);
        entry.members[item.key()] = std::move(selector);
    }
}

void ParseDeckCopies(const json& value, const std::string& file, const std::string& pointer,
                     std::vector<Diagnostic>& diagnostics, DeckEntry& entry) {
    const auto copies = value.find("copies");
    const std::string copies_pointer = match::expr::AppendPointerToken(pointer, "copies");
    if (copies == value.end()) {
        AddError(diagnostics, file, copies_pointer, "missing required key \"copies\"");
        return;
    }
    if (copies->is_string()) {
        const std::string text = copies->get<std::string>();
        if (text.empty()) {
            AddError(diagnostics, file, copies_pointer,
                     "key \"copies\" must be a non-empty string");
            return;
        }
        // INFO: only the syntax is checked here; the schema-aware check that
        // knows the settings names and types runs in Task 12.
        const match::expr::ParseResult parsed = match::expr::ParseExpression(text);
        if (parsed.error) {
            AddError(diagnostics, file, copies_pointer,
                     match::expr::FormatParseError(*parsed.error));
            return;
        }
        const std::size_t node_count = match::expr::CountNodes(*parsed.root);
        if (node_count > match::expr::kMaximumExpressionNodes) {
            AddError(diagnostics, file, copies_pointer,
                     "expression has " + std::to_string(node_count) +
                         " nodes; the limit is " +
                         std::to_string(match::expr::kMaximumExpressionNodes));
            return;
        }
        // TODO(task 12): check the expression with match::expr::CheckExpression.
        entry.copies = *copies;
        return;
    }
    if (copies->is_number_integer()) {
        const bool below_minimum =
            !copies->is_number_unsigned() && copies->get<long long>() < 0;
        const bool above_maximum =
            copies->is_number_unsigned()
                ? copies->get<unsigned long long>() >
                      static_cast<unsigned long long>(kMaximumDeckCopies)
                : copies->get<long long>() > kMaximumDeckCopies;
        if (below_minimum || above_maximum) {
            AddError(diagnostics, file, copies_pointer,
                     "key \"copies\" must be between 0 and " +
                         std::to_string(kMaximumDeckCopies));
            return;
        }
        entry.copies = *copies;
        return;
    }
    AddError(diagnostics, file, copies_pointer,
             "key \"copies\" must be an integer or a non-empty string");
}

void ParseDeckCombine(const json& value, const std::string& file, const std::string& pointer,
                      std::vector<Diagnostic>& diagnostics, DeckEntry& entry) {
    const auto combine = value.find("combine");
    if (combine == value.end()) {
        return;
    }
    const std::string combine_pointer = match::expr::AppendPointerToken(pointer, "combine");
    if (!combine->is_string()) {
        AddError(diagnostics, file, combine_pointer, "key \"combine\" must be a string");
        return;
    }
    const std::string text = combine->get<std::string>();
    if (!Contains(CombineModes(), text)) {
        std::string suggestion = match::expr::SuggestClosest(text, CombineModes());
        if (suggestion.empty()) {
            suggestion = combine_mode::kProduct;
        }
        AddError(diagnostics, file, combine_pointer,
                 "key \"combine\" must be one of " + QuotedValues(CombineModes()), suggestion);
        return;
    }
    entry.combine = text;
}

bool ParseDeckEntry(const json& value, const std::string& file, const std::string& pointer,
                    std::vector<Diagnostic>& diagnostics, DeckEntry& entry) {
    if (!value.is_object()) {
        AddError(diagnostics, file, pointer, "deck entry must be an object");
        return false;
    }
    const std::size_t initial_diagnostics = diagnostics.size();
    CheckUnknownKeys(value, DeckEntryKeys(), file, pointer, diagnostics);

    const std::optional<std::string> kind =
        ReadString(value, "kind", file, pointer, diagnostics, true);
    if (kind) {
        entry.kind = *kind;
        if (!IsValidIdentifier(*kind)) {
            AddError(diagnostics, file, match::expr::AppendPointerToken(pointer, "kind"),
                     "kind \"" + *kind +
                         "\" is not a valid identifier; a local name uses [a-z0-9_] "
                         "(1 to 32 characters)");
        }
    }

    ParseDeckCopies(value, file, pointer, diagnostics, entry);
    ParseDeckMembers(value, file, pointer, diagnostics, entry);
    ParseDeckCombine(value, file, pointer, diagnostics, entry);
    entry.pointer = pointer;
    return diagnostics.size() == initial_diagnostics;
}

void ParseDeckMaxCards(const json& value, const std::string& file, const std::string& pointer,
                       std::vector<Diagnostic>& diagnostics, DeckSpec& deck) {
    const auto max_cards = value.find("max_cards");
    if (max_cards == value.end()) {
        return;
    }
    const std::string max_cards_pointer =
        match::expr::AppendPointerToken(pointer, "max_cards");
    if (!max_cards->is_number_integer()) {
        AddError(diagnostics, file, max_cards_pointer, "key \"max_cards\" must be an integer");
        return;
    }
    const long long number = max_cards->is_number_unsigned()
                                 ? static_cast<long long>(max_cards->get<unsigned long long>())
                                 : max_cards->get<long long>();
    if (number < kMinimumDeckCards || number > kMaximumDeckCards) {
        AddError(diagnostics, file, max_cards_pointer,
                 "key \"max_cards\" must be between " + std::to_string(kMinimumDeckCards) +
                     " and " + std::to_string(kMaximumDeckCards));
        return;
    }
    deck.max_cards = static_cast<int>(number);
}

bool ParseDeck(const json& value, const std::string& mod_id, const std::string& file,
               const std::string& pointer, std::vector<Diagnostic>& diagnostics, DeckSpec& deck) {
    if (!value.is_object()) {
        AddError(diagnostics, file, pointer, "deck must be an object");
        return false;
    }
    const std::size_t initial_diagnostics = diagnostics.size();
    CheckUnknownKeys(value, DeckKeys(), file, pointer, diagnostics);

    bool usable = false;
    const std::optional<std::string> identifier =
        ReadString(value, "identifier", file, pointer, diagnostics, true);
    if (identifier) {
        deck.identifier = *identifier;
        usable = ValidateIdentifier(*identifier, mod_id, file,
                                    match::expr::AppendPointerToken(pointer, "identifier"),
                                    "deck identifier", diagnostics);
    }

    if (const std::optional<std::string> name =
            ReadString(value, "name", file, pointer, diagnostics, false)) {
        deck.name = *name;
    }

    ParseDeckMaxCards(value, file, pointer, diagnostics, deck);

    ReadStringArray(value, "mods", "mod entry must be a non-empty string",
                    "duplicate mod entry", file, pointer, diagnostics, deck.mods);

    const auto contribute = value.find("contribute");
    if (contribute != value.end()) {
        const std::string contribute_pointer =
            match::expr::AppendPointerToken(pointer, "contribute");
        if (!contribute->is_object()) {
            AddError(diagnostics, file, contribute_pointer,
                     "key \"contribute\" must be an object");
        }
        // TODO(task 8): parse the deck contribution into DeckSpec::contribute.
    }

    const auto cards = value.find("cards");
    if (cards != value.end()) {
        const std::string cards_pointer = match::expr::AppendPointerToken(pointer, "cards");
        if (!cards->is_array()) {
            AddError(diagnostics, file, cards_pointer, "key \"cards\" must be an array");
        } else {
            for (std::size_t index = 0; index < cards->size(); ++index) {
                DeckEntry entry;
                const std::string entry_pointer =
                    match::expr::AppendPointerToken(cards_pointer, std::to_string(index));
                if (ParseDeckEntry((*cards)[index], file, entry_pointer, diagnostics, entry)) {
                    deck.cards.push_back(std::move(entry));
                }
            }
        }
    }

    deck.file = file;
    return usable && diagnostics.size() == initial_diagnostics;
}

const std::vector<std::string>& PropertyKeys() {
    static const std::vector<std::string> keys = {"identifier", "scope",     "type",
                                                  "default",    "range",     "visibility",
                                                  "sync_to_clients", "writable_by",
                                                  "values",     "attribute"};
    return keys;
}

const std::vector<std::string>& PropertyScopes() {
    static const std::vector<std::string> scopes = {"card", "seat", "match"};
    return scopes;
}

const std::vector<std::string>& PropertyTypes() {
    static const std::vector<std::string> types = {"int", "float", "boolean", "string",
                                                   "member"};
    return types;
}

bool PropertyDefaultFitsType(const json& value, const std::string& type) {
    if (type == "int") {
        return value.is_number_integer();
    }
    if (type == "float") {
        return value.is_number();
    }
    if (type == "boolean") {
        return value.is_boolean();
    }
    if (type == "string" || type == "member") {
        return value.is_string();
    }
    return true;
}

void ParsePropertyRange(const json& value, const std::string& file, const std::string& pointer,
                        std::vector<Diagnostic>& diagnostics, PropertyDef& property) {
    const auto range = value.find("range");
    if (range == value.end()) {
        return;
    }
    const std::string range_pointer = match::expr::AppendPointerToken(pointer, "range");
    if (property.type != "int" && property.type != "float") {
        AddError(diagnostics, file, range_pointer,
                 "key \"range\" is only allowed when \"type\" is \"int\" or \"float\"");
        return;
    }
    if (!range->is_array() || range->size() != 2 || !(*range)[0].is_number() ||
        !(*range)[1].is_number()) {
        AddError(diagnostics, file, range_pointer,
                 "key \"range\" must be an array of two numbers");
        return;
    }
    const double low = (*range)[0].get<double>();
    const double high = (*range)[1].get<double>();
    if (low > high) {
        AddError(diagnostics, file, range_pointer, "key \"range\" must have low <= high");
        return;
    }
    property.range = std::make_pair(low, high);
}

void ParsePropertyDefault(const json& value, const std::string& file, const std::string& pointer,
                          std::vector<Diagnostic>& diagnostics, PropertyDef& property) {
    const auto entry = value.find("default");
    if (entry == value.end()) {
        return;
    }
    const std::string default_pointer = match::expr::AppendPointerToken(pointer, "default");
    property.default_value = *entry;
    if (!PropertyDefaultFitsType(*entry, property.type)) {
        AddError(diagnostics, file, default_pointer,
                 "key \"default\" must be of type \"" + property.type + "\"");
        return;
    }
    if (property.range && entry->is_number()) {
        const double number = entry->get<double>();
        if (number < property.range->first || number > property.range->second) {
            AddError(diagnostics, file, default_pointer,
                     "key \"default\" is outside the range [" +
                         FormatNumber(property.range->first) + ", " +
                         FormatNumber(property.range->second) + "]");
        }
    }
}

void ParsePropertyWritableBy(const json& value, const std::string& file,
                             const std::string& pointer, std::vector<Diagnostic>& diagnostics,
                             PropertyDef& property) {
    const auto entry = value.find("writable_by");
    if (entry == value.end()) {
        return;
    }
    const std::string writable_pointer =
        match::expr::AppendPointerToken(pointer, "writable_by");
    if (entry->is_string()) {
        if (entry->get<std::string>() == "owner") {
            return;
        }
        AddError(diagnostics, file, writable_pointer,
                 "key \"writable_by\" must be \"owner\" or an array of mod ids");
        return;
    }
    if (!entry->is_array()) {
        AddError(diagnostics, file, writable_pointer,
                 "key \"writable_by\" must be \"owner\" or an array of mod ids");
        return;
    }
    std::set<std::string> seen;
    for (std::size_t index = 0; index < entry->size(); ++index) {
        const json& item = (*entry)[index];
        const std::string item_pointer =
            match::expr::AppendPointerToken(writable_pointer, std::to_string(index));
        if (!item.is_string() || item.get<std::string>().empty()) {
            AddError(diagnostics, file, item_pointer, "mod id must be a non-empty string");
            continue;
        }
        const std::string text = item.get<std::string>();
        if (!IsValidLocalName(text)) {
            AddError(diagnostics, file, item_pointer,
                     "mod id \"" + text + "\" must use [a-z0-9_] (1 to 32 characters)");
            continue;
        }
        if (!seen.insert(text).second) {
            AddError(diagnostics, file, item_pointer, "duplicate mod id \"" + text + "\"");
            continue;
        }
        property.writable_by.push_back(text);
    }
}

void ParsePropertyValues(const json& value, const std::string& file, const std::string& pointer,
                         std::vector<Diagnostic>& diagnostics, PropertyDef& property) {
    const auto entry = value.find("values");
    if (entry == value.end()) {
        return;
    }
    const std::string values_pointer = match::expr::AppendPointerToken(pointer, "values");
    if (property.type != "string") {
        AddError(diagnostics, file, values_pointer,
                 "key \"values\" is only allowed when \"type\" is \"string\"");
        return;
    }
    if (!entry->is_array()) {
        AddError(diagnostics, file, values_pointer, "key \"values\" must be an array");
        return;
    }
    std::set<std::string> seen;
    for (std::size_t index = 0; index < entry->size(); ++index) {
        const json& item = (*entry)[index];
        const std::string item_pointer =
            match::expr::AppendPointerToken(values_pointer, std::to_string(index));
        if (!item.is_string() || item.get<std::string>().empty()) {
            AddError(diagnostics, file, item_pointer, "value must be a non-empty string");
            continue;
        }
        const std::string text = item.get<std::string>();
        if (!seen.insert(text).second) {
            AddError(diagnostics, file, item_pointer, "duplicate value \"" + text + "\"");
            continue;
        }
        property.values.push_back(text);
    }
}

bool ParseProperty(const json& value, const std::string& mod_id, const std::string& file,
                   const std::string& pointer, std::vector<Diagnostic>& diagnostics,
                   PropertyDef& property) {
    if (!value.is_object()) {
        AddError(diagnostics, file, pointer, "property must be an object");
        return false;
    }
    const std::size_t initial_diagnostics = diagnostics.size();
    CheckUnknownKeys(value, PropertyKeys(), file, pointer, diagnostics);

    bool usable = false;
    const std::optional<std::string> identifier =
        ReadString(value, "identifier", file, pointer, diagnostics, true);
    if (identifier) {
        property.identifier = *identifier;
        usable = ValidateIdentifier(*identifier, mod_id, file,
                                    match::expr::AppendPointerToken(pointer, "identifier"),
                                    "property identifier", diagnostics);
    }

    if (const std::optional<std::string> scope =
            ReadEnum(value, "scope", PropertyScopes(), file, pointer, diagnostics, true,
                     "card")) {
        property.scope = *scope;
    }
    if (const std::optional<std::string> type =
            ReadEnum(value, "type", PropertyTypes(), file, pointer, diagnostics, true)) {
        property.type = *type;
    }

    ParsePropertyRange(value, file, pointer, diagnostics, property);
    ParsePropertyDefault(value, file, pointer, diagnostics, property);

    if (const std::optional<std::string> visibility =
            ReadEnum(value, "visibility", {"public", "owner", "hidden"}, file, pointer,
                     diagnostics)) {
        property.visibility = *visibility;
    }

    const auto sync_to_clients = value.find("sync_to_clients");
    if (sync_to_clients != value.end()) {
        if (!sync_to_clients->is_boolean()) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "sync_to_clients"),
                     "key \"sync_to_clients\" must be a boolean");
        } else {
            property.sync_to_clients = sync_to_clients->get<bool>();
        }
    }

    ParsePropertyWritableBy(value, file, pointer, diagnostics, property);
    ParsePropertyValues(value, file, pointer, diagnostics, property);

    const std::optional<std::string> attribute =
        ReadString(value, "attribute", file, pointer, diagnostics, property.type == "member");
    if (attribute) {
        if (ValidateIdentifier(*attribute, mod_id, file,
                               match::expr::AppendPointerToken(pointer, "attribute"),
                               "property attribute", diagnostics)) {
            property.member_attribute = *attribute;
        }
    }

    property.file = file;
    return usable && diagnostics.size() == initial_diagnostics;
}

void ReadTags(const json& value, const std::string& file, const std::string& pointer,
              std::vector<Diagnostic>& diagnostics, MemberDef& member) {
    const auto tags = value.find("tags");
    if (tags == value.end()) {
        return;
    }
    const std::string tags_pointer = match::expr::AppendPointerToken(pointer, "tags");
    if (!tags->is_array()) {
        AddError(diagnostics, file, tags_pointer, "key \"tags\" must be an array");
        return;
    }
    for (std::size_t index = 0; index < tags->size(); ++index) {
        const json& tag = (*tags)[index];
        if (!tag.is_string() || tag.get<std::string>().empty()) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(tags_pointer, std::to_string(index)),
                     "tag must be a non-empty string");
        } else {
            member.tags.push_back(tag.get<std::string>());
        }
    }
}

void ReadOrder(const json& value, const std::string& file, const std::string& pointer,
               std::vector<Diagnostic>& diagnostics, MemberDef& member) {
    const auto order = value.find("order");
    if (order == value.end()) {
        return;
    }
    const std::string order_pointer = match::expr::AppendPointerToken(pointer, "order");
    if (!order->is_number_integer()) {
        AddError(diagnostics, file, order_pointer, "key \"order\" must be an integer");
        return;
    }
    if (order->is_number_unsigned()) {
        const unsigned long long order_value = order->get<unsigned long long>();
        if (order_value > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            AddError(diagnostics, file, order_pointer, "key \"order\" is out of range");
            return;
        }
        member.order = static_cast<int>(order_value);
        return;
    }
    const long long order_value = order->get<long long>();
    if (order_value < std::numeric_limits<int>::min() ||
        order_value > std::numeric_limits<int>::max()) {
        AddError(diagnostics, file, order_pointer, "key \"order\" is out of range");
        return;
    }
    member.order = static_cast<int>(order_value);
}

bool ParseMember(const json& value, const std::string& default_attribute,
                 bool allow_attribute, const std::string& mod_id,
                 const std::string& file, const std::string& pointer,
                 std::vector<Diagnostic>& diagnostics, MemberDef& member) {
    if (!value.is_object()) {
        AddError(diagnostics, file, pointer, "attribute_member must be an object");
        return false;
    }
    const std::size_t initial_diagnostics = diagnostics.size();
    CheckUnknownKeys(value, MemberKeys(allow_attribute), file, pointer, diagnostics);

    std::string attribute = default_attribute;
    if (allow_attribute) {
        const std::optional<std::string> declared =
            ReadString(value, "attribute", file, pointer, diagnostics, true);
        if (declared) {
            attribute = *declared;
            if (!IsValidIdentifier(*declared)) {
                AddError(diagnostics, file,
                         match::expr::AppendPointerToken(pointer, "attribute"),
                         "attribute \"" + *declared + "\" is not a valid identifier");
            }
        }
    }

    const std::optional<std::string> identifier =
        ReadString(value, "identifier", file, pointer, diagnostics, true);
    if (identifier) {
        member.identifier = *identifier;
        ValidateIdentifier(*identifier, mod_id, file,
                           match::expr::AppendPointerToken(pointer, "identifier"),
                           "member identifier", diagnostics);
    }

    if (const std::optional<std::string> display_name =
            ReadString(value, "display_name", file, pointer, diagnostics, false)) {
        member.display_name = *display_name;
    }

    ReadTags(value, file, pointer, diagnostics, member);
    ReadOrder(value, file, pointer, diagnostics, member);

    const auto contribute = value.find("contribute");
    if (contribute != value.end()) {
        member.contribute_raw = *contribute;
    }

    const auto properties = value.find("properties");
    if (properties != value.end()) {
        if (!properties->is_object()) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "properties"),
                     "key \"properties\" must be an object");
        } else {
            member.properties = *properties;
        }
    }

    if (const std::optional<std::string> alias =
            ReadString(value, "identifier_alias", file, pointer, diagnostics, false)) {
        if (!IsValidLocalName(*alias)) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "identifier_alias"),
                     "identifier_alias \"" + *alias +
                         "\" must use [a-z0-9_] (1 to 32 characters)");
        } else {
            member.identifier_alias = *alias;
        }
    }

    member.attribute = attribute;
    member.file = file;
    return diagnostics.size() == initial_diagnostics;
}

void RegisterMember(MemberDef member, const std::string& file,
                    const std::string& duplicate_pointer,
                    std::vector<Diagnostic>& diagnostics,
                    std::map<std::string, std::string>& member_files,
                    std::vector<MemberDef>& members) {
    const auto existing = member_files.find(member.identifier);
    if (existing != member_files.end()) {
        AddError(diagnostics, file, duplicate_pointer,
                 "duplicate member identifier \"" + member.identifier + "\" in " + file +
                     "; first declared in " + existing->second);
        return;
    }
    member_files.emplace(member.identifier, file);
    members.push_back(std::move(member));
}

struct PendingMember {
    MemberDef member;
    std::string pointer;
};

bool ParseAttribute(const json& value, const std::string& mod_id, const std::string& file,
                    const std::string& pointer, std::vector<Diagnostic>& diagnostics,
                    std::vector<PendingMember>& pending_members, AttributeDef& attribute) {
    if (!value.is_object()) {
        AddError(diagnostics, file, pointer, "attribute must be an object");
        return false;
    }
    static const std::vector<std::string> keys = {"identifier", "extensible", "visibility",
                                                  "members"};
    CheckUnknownKeys(value, keys, file, pointer, diagnostics);

    bool usable = false;
    const std::optional<std::string> identifier =
        ReadString(value, "identifier", file, pointer, diagnostics, true);
    if (identifier) {
        attribute.identifier = *identifier;
        usable = ValidateIdentifier(*identifier, mod_id, file,
                                    match::expr::AppendPointerToken(pointer, "identifier"),
                                    "attribute identifier", diagnostics);
    }

    const auto extensible = value.find("extensible");
    if (extensible != value.end()) {
        if (!extensible->is_boolean()) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(pointer, "extensible"),
                     "key \"extensible\" must be a boolean");
        } else {
            attribute.extensible = extensible->get<bool>();
        }
    }

    if (const std::optional<std::string> visibility =
            ReadEnum(value, "visibility", {"public", "owner", "hidden"}, file, pointer,
                     diagnostics)) {
        attribute.visibility = *visibility;
    }

    const auto inline_members = value.find("members");
    if (inline_members != value.end()) {
        const std::string members_pointer =
            match::expr::AppendPointerToken(pointer, "members");
        if (!inline_members->is_array()) {
            AddError(diagnostics, file, members_pointer, "key \"members\" must be an array");
        } else if (identifier && IsValidIdentifier(*identifier)) {
            // INFO: a bad inline member is skipped, but the attribute it belongs
            // to is still registered so later cross-reference checks do not
            // cascade "unknown attribute" errors. The parsed members are held
            // back until the caller accepts the attribute itself.
            for (std::size_t index = 0; index < inline_members->size(); ++index) {
                MemberDef member;
                const std::string member_pointer =
                    match::expr::AppendPointerToken(members_pointer, std::to_string(index));
                if (!ParseMember((*inline_members)[index], *identifier, false, mod_id, file,
                                 member_pointer, diagnostics, member)) {
                    continue;
                }
                pending_members.push_back(PendingMember{std::move(member), member_pointer});
            }
        }
    }

    attribute.file = file;
    return usable;
}

// INFO: reads a card file into a capped string. The size is checked first so an
// oversized file is never read whole; the read itself stops one byte past the
// limit, so a file that grows between the two calls is still caught.
std::optional<std::string> ReadCardFileText(const fs::path& path, const std::string& label,
                                            std::vector<Diagnostic>& diagnostics) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "cannot read file: " + error.message());
        return std::nullopt;
    }
    if (size > kMaximumCardFileBytes) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "file is too large (more than 1048576 bytes)");
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "cannot open file");
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(size) + 1, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    if (text.size() > kMaximumCardFileBytes) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "file is too large (more than 1048576 bytes)");
        return std::nullopt;
    }
    return text;
}

}  // namespace

namespace internal {

std::optional<json> ReadCardDocument(const fs::path& path, const std::string& label,
                                     std::vector<Diagnostic>& diagnostics) {
    const std::optional<std::string> text = ReadCardFileText(path, label, diagnostics);
    if (!text) {
        return std::nullopt;
    }

    match::expr::SchemaScan scan;
    const std::optional<json> parsed = match::expr::ParseSchemaJson(*text, scan);
    for (const match::expr::DuplicateKey& duplicate : scan.duplicates) {
        AddError(diagnostics, label, duplicate.pointer,
                 "duplicate key \"" + duplicate.key + "\"");
    }
    if (scan.duplicates_truncated) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "more duplicate keys not shown");
    }
    if (!parsed.has_value()) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer), "invalid JSON");
        return std::nullopt;
    }
    if (scan.nested_too_deep) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "schema is nested too deeply (more than " +
                     std::to_string(match::expr::kMaximumSchemaDepth) + " levels)");
        return std::nullopt;
    }
    if (!parsed->is_object()) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "file must be a JSON object");
        return std::nullopt;
    }
    return parsed;
}

}  // namespace internal

namespace {

void LoadFileImpl(const fs::path& path, const std::string& folder, const std::string& label,
                  const std::string& mod_id, LoadOutcome& outcome,
                  std::map<std::string, std::string>& attribute_files,
                  std::map<std::string, std::string>& member_files,
                  std::map<std::string, std::string>& kind_files,
                  std::map<std::string, std::string>& deck_files,
                  std::map<std::string, std::string>& property_files) {
    std::vector<Diagnostic>& diagnostics = outcome.diagnostics;

    const std::optional<json> document = internal::ReadCardDocument(path, label, diagnostics);
    if (!document) {
        return;
    }

    bool format_ok = false;
    const auto format_version = document->find("format_version");
    if (format_version == document->end()) {
        AddError(diagnostics, label, PointerFor("format_version"),
                 "missing required key \"format_version\"");
    } else if (!format_version->is_string() ||
               format_version->get<std::string>() != kFormatVersion) {
        AddError(diagnostics, label, PointerFor("format_version"),
                 "format_version must be \"1.0.0\"");
    } else {
        format_ok = true;
    }

    std::vector<std::string> roots;
    for (const auto& entry : document->items()) {
        if (entry.key() != "format_version") {
            roots.push_back(entry.key());
        }
    }

    std::vector<std::string> folder_roots;
    bool every_root_valid = true;
    for (const std::string& root : roots) {
        if (!Contains(RootKeys(), root)) {
            every_root_valid = false;
            AddError(diagnostics, label, PointerFor(root), "unknown key \"" + root + "\"",
                     match::expr::SuggestClosest(root, RootKeyCandidates(folder)));
        } else if (!Contains(AllowedRootsFor(folder), root)) {
            every_root_valid = false;
            AddError(diagnostics, label, PointerFor(root),
                     "root key '" + root + "' is not allowed in " + folder + "/");
        } else {
            folder_roots.push_back(root);
        }
    }

    if (roots.empty()) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer), "missing root key");
    } else if (every_root_valid && folder_roots.size() != 1) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "exactly one root key is allowed besides \"format_version\"");
    }

    if (!format_ok || folder_roots.size() != 1) {
        return;
    }

    const std::string& root = folder_roots.front();
    const std::string pointer = PointerFor(root);
    const json& value = document->at(root);

    if (root == "attribute") {
        AttributeDef attribute;
        std::vector<PendingMember> pending_members;
        if (!ParseAttribute(value, mod_id, label, pointer, diagnostics, pending_members,
                            attribute)) {
            return;
        }
        const std::string identifier_pointer =
            match::expr::AppendPointerToken(pointer, "identifier");
        CheckIdentifierMatchesFile(attribute.identifier, path.stem().string(), label,
                                   identifier_pointer, diagnostics);
        const auto existing = attribute_files.find(attribute.identifier);
        if (existing != attribute_files.end()) {
            AddError(diagnostics, label, identifier_pointer,
                     "duplicate attribute identifier \"" + attribute.identifier + "\" in " +
                         label + "; first declared in " + existing->second);
            return;
        }
        attribute_files.emplace(attribute.identifier, label);
        outcome.schema.attributes.push_back(std::move(attribute));
        for (PendingMember& pending : pending_members) {
            RegisterMember(std::move(pending.member), label, pending.pointer, diagnostics,
                           member_files, outcome.schema.members);
        }
        return;
    }

    if (root == "attribute_member") {
        MemberDef member;
        if (!ParseMember(value, "", true, mod_id, label, pointer, diagnostics, member)) {
            return;
        }
        const std::string identifier_pointer =
            match::expr::AppendPointerToken(pointer, "identifier");
        CheckIdentifierMatchesFile(member.identifier, path.stem().string(), label,
                                   identifier_pointer, diagnostics);
        RegisterMember(std::move(member), label, identifier_pointer, diagnostics, member_files,
                       outcome.schema.members);
        return;
    }

    if (root == "kind") {
        KindTemplate kind;
        if (!ParseKind(value, mod_id, label, pointer, diagnostics, kind)) {
            return;
        }
        const std::string identifier_pointer =
            match::expr::AppendPointerToken(pointer, "identifier");
        CheckIdentifierMatchesFile(kind.identifier, path.stem().string(), label,
                                   identifier_pointer, diagnostics);
        const auto existing = kind_files.find(kind.identifier);
        if (existing != kind_files.end()) {
            AddError(diagnostics, label, identifier_pointer,
                     "duplicate kind identifier \"" + kind.identifier + "\" in " + label +
                         "; first declared in " + existing->second);
            return;
        }
        kind_files.emplace(kind.identifier, label);
        outcome.schema.kinds.push_back(std::move(kind));
        return;
    }

    if (root == "deck") {
        DeckSpec deck;
        if (!ParseDeck(value, mod_id, label, pointer, diagnostics, deck)) {
            return;
        }
        const std::string identifier_pointer =
            match::expr::AppendPointerToken(pointer, "identifier");
        CheckIdentifierMatchesFile(deck.identifier, path.stem().string(), label,
                                   identifier_pointer, diagnostics);
        const auto existing = deck_files.find(deck.identifier);
        if (existing != deck_files.end()) {
            AddError(diagnostics, label, identifier_pointer,
                     "duplicate deck identifier \"" + deck.identifier + "\" in " + label +
                         "; first declared in " + existing->second);
            return;
        }
        deck_files.emplace(deck.identifier, label);
        outcome.schema.decks.push_back(std::move(deck));
        return;
    }

    if (root == "property") {
        PropertyDef property;
        if (!ParseProperty(value, mod_id, label, pointer, diagnostics, property)) {
            return;
        }
        const std::string identifier_pointer =
            match::expr::AppendPointerToken(pointer, "identifier");
        CheckIdentifierMatchesFile(property.identifier, path.stem().string(), label,
                                   identifier_pointer, diagnostics);
        const auto existing = property_files.find(property.identifier);
        if (existing != property_files.end()) {
            AddError(diagnostics, label, identifier_pointer,
                     "duplicate property identifier \"" + property.identifier + "\" in " +
                         label + "; first declared in " + existing->second);
            return;
        }
        property_files.emplace(property.identifier, label);
        outcome.schema.properties.push_back(std::move(property));
    }
}

void LoadFile(const fs::path& path, const std::string& folder, const std::string& label,
              const std::string& mod_id, LoadOutcome& outcome,
              std::map<std::string, std::string>& attribute_files,
              std::map<std::string, std::string>& member_files,
              std::map<std::string, std::string>& kind_files,
              std::map<std::string, std::string>& deck_files,
              std::map<std::string, std::string>& property_files) {
    try {
        LoadFileImpl(path, folder, label, mod_id, outcome, attribute_files, member_files,
                     kind_files, deck_files, property_files);
    } catch (const std::exception& error) {
        AddError(outcome.diagnostics, label, std::string(match::expr::kRootPointer),
                 std::string("internal error: ") + error.what());
    }
}

void ReportDirectoryError(const std::string& label, const std::error_code& error,
                          std::vector<Diagnostic>& diagnostics) {
    AddError(diagnostics, label, std::string(match::expr::kRootPointer),
             "cannot read directory \"" + label + "\": " + error.message());
}

void CollectJsonFiles(const fs::path& mod_directory, const std::string& mod_id,
                      const std::string& folder, bool recursive, std::vector<fs::path>& files,
                      std::vector<Diagnostic>& diagnostics) {
    const fs::path folder_path = mod_directory / folder;

    std::error_code error;
    const fs::file_status folder_status = fs::symlink_status(folder_path, error);
    if (error == std::errc::no_such_file_or_directory) {
        return;
    }
    if (error) {
        ReportDirectoryError(RelativeLabel(mod_id, mod_directory, folder_path), error,
                             diagnostics);
        return;
    }
    if (!fs::exists(folder_status)) {
        return;
    }
    if (fs::is_symlink(folder_status)) {
        AddError(diagnostics, RelativeLabel(mod_id, mod_directory, folder_path),
                 std::string(match::expr::kRootPointer), "symbolic links are not allowed");
        return;
    }
    if (!fs::is_directory(folder_status)) {
        AddError(diagnostics, RelativeLabel(mod_id, mod_directory, folder_path),
                 std::string(match::expr::kRootPointer),
                 "cannot read directory \"" + folder + "\": not a directory");
        return;
    }

    const auto consider = [&](const fs::path& path) {
        std::error_code status_error;
        const fs::file_status status = fs::symlink_status(path, status_error);
        const std::string label = RelativeLabel(mod_id, mod_directory, path);
        if (status_error == std::errc::no_such_file_or_directory) {
            return;
        }
        if (status_error) {
            ReportDirectoryError(label, status_error, diagnostics);
            return;
        }
        if (fs::is_symlink(status)) {
            AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                     "symbolic links are not allowed");
            return;
        }
        if (fs::is_directory(status)) {
            if (!recursive) {
                AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                         "subdirectories are not allowed in " + folder + "/");
            }
            return;
        }
        if (!fs::is_regular_file(status) || path.extension() != ".json") {
            AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                     "only .json files are allowed in " + folder + "/");
            return;
        }
        files.push_back(path);
    };

    if (recursive) {
        fs::recursive_directory_iterator iterator(folder_path, error);
        if (error) {
            ReportDirectoryError(RelativeLabel(mod_id, mod_directory, folder_path), error,
                                 diagnostics);
            return;
        }
        const fs::recursive_directory_iterator end;
        while (iterator != end) {
            consider(iterator->path());
            iterator.increment(error);
            if (error) {
                ReportDirectoryError(RelativeLabel(mod_id, mod_directory, folder_path), error,
                                     diagnostics);
                break;
            }
        }
    } else {
        fs::directory_iterator iterator(folder_path, error);
        if (error) {
            ReportDirectoryError(RelativeLabel(mod_id, mod_directory, folder_path), error,
                                 diagnostics);
            return;
        }
        const fs::directory_iterator end;
        while (iterator != end) {
            consider(iterator->path());
            iterator.increment(error);
            if (error) {
                ReportDirectoryError(RelativeLabel(mod_id, mod_directory, folder_path), error,
                                     diagnostics);
                break;
            }
        }
    }
}

}  // namespace

LoadOutcome LoadModSchema(const fs::path& mod_directory, const std::string& mod_id) {
    LoadOutcome outcome;
    try {
        outcome.schema.mod_id = mod_id;
        outcome.schema.directory = mod_directory.generic_string();

        if (!IsValidLocalName(mod_id)) {
            AddError(outcome.diagnostics, "", std::string(match::expr::kRootPointer),
                     "invalid mod id \"" + mod_id + "\"");
            return outcome;
        }

        std::vector<fs::path> files;
        CollectJsonFiles(mod_directory, mod_id, "attributes", true, files, outcome.diagnostics);
        CollectJsonFiles(mod_directory, mod_id, "kinds", false, files, outcome.diagnostics);
        CollectJsonFiles(mod_directory, mod_id, "decks", false, files, outcome.diagnostics);
        CollectJsonFiles(mod_directory, mod_id, "properties", false, files,
                         outcome.diagnostics);
        std::sort(files.begin(), files.end(),
                  [](const fs::path& left, const fs::path& right) {
                      return left.generic_string() < right.generic_string();
                  });

        std::map<std::string, std::string> attribute_files;
        std::map<std::string, std::string> member_files;
        std::map<std::string, std::string> kind_files;
        std::map<std::string, std::string> deck_files;
        std::map<std::string, std::string> property_files;

        for (const fs::path& file : files) {
            const fs::path relative = file.lexically_relative(mod_directory);
            // INFO: every collected file comes from a subfolder of the mod
            // directory, so an empty relative path is a loader bug, not user
            // input; report it instead of dropping the file silently.
            if (relative.empty()) {
                AddError(outcome.diagnostics, PathLabel(mod_id, file),
                         std::string(match::expr::kRootPointer),
                         "file is not inside the mod directory");
                continue;
            }
            const std::string folder = relative.begin()->string();
            const std::string label = PathLabel(mod_id, relative);
            LoadFile(file, folder, label, mod_id, outcome, attribute_files, member_files,
                     kind_files, deck_files, property_files);
        }

        std::stable_sort(outcome.schema.members.begin(), outcome.schema.members.end(),
                         [](const MemberDef& left, const MemberDef& right) {
                             if (left.order != right.order) {
                                 return left.order < right.order;
                             }
                             return left.identifier < right.identifier;
                         });
    } catch (const std::exception& error) {
        AddError(outcome.diagnostics, "", std::string(match::expr::kRootPointer),
                 std::string("internal error: ") + error.what());
    }
    return outcome;
}

}  // namespace match::cardschema
