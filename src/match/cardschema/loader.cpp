#include <match/cardschema/loader.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

std::optional<std::string> ReadEnum(const json& object, const char* key,
                                    const std::vector<std::string>& candidates,
                                    const std::string& file, const std::string& pointer,
                                    std::vector<Diagnostic>& diagnostics) {
    const std::optional<std::string> value =
        ReadString(object, key, file, pointer, diagnostics, false);
    if (!value) {
        return std::nullopt;
    }
    if (!Contains(candidates, *value)) {
        AddError(diagnostics, file, match::expr::AppendPointerToken(pointer, key),
                 "key \"" + std::string(key) + "\" must be one of " +
                     QuotedValues(candidates),
                 match::expr::SuggestClosest(*value, candidates));
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
                     const std::string& file, const std::string& pointer,
                     std::vector<Diagnostic>& diagnostics, std::vector<std::string>& out) {
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
    for (std::size_t index = 0; index < entry->size(); ++index) {
        const json& item = (*entry)[index];
        if (!item.is_string() || item.get<std::string>().empty()) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(array_pointer, std::to_string(index)),
                     element_error);
            continue;
        }
        out.push_back(item.get<std::string>());
    }
}

void ParseKindSlots(const json& value, const std::string& file, const std::string& pointer,
                    std::vector<Diagnostic>& diagnostics, KindTemplate& kind) {
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
        if (!attribute) {
            continue;
        }
        if (!IsValidIdentifier(*attribute)) {
            AddError(diagnostics, file,
                     match::expr::AppendPointerToken(binding_pointer, "attribute"),
                     "attribute \"" + *attribute + "\" is not a valid identifier");
            continue;
        }

        const std::optional<std::string> declared_name =
            ReadString(binding, "name", file, binding_pointer, diagnostics, false);
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
        for (std::size_t name_index = 0; name_index < group.size(); ++name_index) {
            const json& name = group[name_index];
            if (!name.is_string() || name.get<std::string>().empty()) {
                AddError(diagnostics, file,
                         match::expr::AppendPointerToken(group_pointer,
                                                         std::to_string(name_index)),
                         "symmetric group entry must be a non-empty string");
                continue;
            }
            names.push_back(name.get<std::string>());
        }
        if (names.size() < 2) {
            AddError(diagnostics, file, group_pointer,
                     "symmetric group must name at least 2 slots");
        }
        kind.symmetric_groups.push_back(std::move(names));
    }
}

void CheckSymmetricSlots(const std::string& file, const std::string& pointer,
                         std::vector<Diagnostic>& diagnostics, const KindTemplate& kind) {
    std::set<std::string> declared;
    for (const SlotBinding& slot : kind.slots) {
        declared.insert(slot.name);
    }
    const std::string symmetric_pointer =
        match::expr::AppendPointerToken(pointer, "symmetric");
    for (std::size_t group_index = 0; group_index < kind.symmetric_groups.size();
         ++group_index) {
        const std::vector<std::string>& group = kind.symmetric_groups[group_index];
        for (std::size_t name_index = 0; name_index < group.size(); ++name_index) {
            if (declared.count(group[name_index]) != 0) {
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
        }
    }

    ReadStringArray(value, "tags", "tag must be a non-empty string", file, pointer,
                    diagnostics, kind.tags);
    ParseKindSlots(value, file, pointer, diagnostics, kind);
    ParseKindSymmetric(value, file, pointer, diagnostics, kind);
    CheckSymmetricSlots(file, pointer, diagnostics, kind);

    ReadStringArray(value, "slot_order", "slot_order entry must be a non-empty string", file,
                    pointer, diagnostics, kind.slot_order);

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
    if (contribute != value.end() && !contribute->is_object()) {
        AddError(diagnostics, file,
                 match::expr::AppendPointerToken(pointer, "contribute"),
                 "key \"contribute\" must be an object");
    }

    kind.file = file;
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
                  std::map<std::string, std::string>& kind_files) {
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
    }
}

void LoadFile(const fs::path& path, const std::string& folder, const std::string& label,
              const std::string& mod_id, LoadOutcome& outcome,
              std::map<std::string, std::string>& attribute_files,
              std::map<std::string, std::string>& member_files,
              std::map<std::string, std::string>& kind_files) {
    try {
        LoadFileImpl(path, folder, label, mod_id, outcome, attribute_files, member_files,
                     kind_files);
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
                     kind_files);
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
