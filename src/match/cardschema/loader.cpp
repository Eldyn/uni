#include <match/cardschema/loader.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <match/expr/diagnostic.hpp>

namespace match::cardschema {

namespace {

namespace fs = std::filesystem;

constexpr const char* kFormatVersion = "1.0.0";

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

bool Contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string PointerFor(const std::string& key) {
    return match::expr::AppendPointerToken(match::expr::kRootPointer, key);
}

std::string PathLabel(const std::string& mod_id, const fs::path& relative) {
    return "mods/" + mod_id + "/" + relative.generic_string();
}

void AddError(std::vector<Diagnostic>& diagnostics, const std::string& file,
              const std::string& pointer, std::string message,
              std::string suggestion = {}) {
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
                 "key \"" + std::string(key) + "\" must be one of the allowed values",
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

const std::vector<std::string>& MemberKeys(bool allow_attribute) {
    static const std::vector<std::string> standalone = {
        "attribute", "identifier", "display_name", "tags", "order",
        "contribute", "properties", "identifier_alias"};
    static const std::vector<std::string> inline_keys = {
        "identifier", "display_name", "tags", "order",
        "contribute", "properties", "identifier_alias"};
    return allow_attribute ? standalone : inline_keys;
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

    const auto tags = value.find("tags");
    if (tags != value.end()) {
        const std::string tags_pointer = match::expr::AppendPointerToken(pointer, "tags");
        if (!tags->is_array()) {
            AddError(diagnostics, file, tags_pointer, "key \"tags\" must be an array");
        } else {
            for (std::size_t index = 0; index < tags->size(); ++index) {
                const json& tag = (*tags)[index];
                if (!tag.is_string() || tag.get<std::string>().empty()) {
                    AddError(diagnostics, file,
                             match::expr::AppendPointerToken(tags_pointer,
                                                             std::to_string(index)),
                             "tag must be a non-empty string");
                } else {
                    member.tags.push_back(tag.get<std::string>());
                }
            }
        }
    }

    const auto order = value.find("order");
    if (order != value.end()) {
        const std::string order_pointer = match::expr::AppendPointerToken(pointer, "order");
        if (!order->is_number_integer()) {
            AddError(diagnostics, file, order_pointer, "key \"order\" must be an integer");
        } else if (order->is_number_unsigned()) {
            const unsigned long long raw = order->get<unsigned long long>();
            if (raw > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
                AddError(diagnostics, file, order_pointer, "key \"order\" is out of range");
            } else {
                member.order = static_cast<int>(raw);
            }
        } else {
            const long long raw = order->get<long long>();
            if (raw < std::numeric_limits<int>::min() ||
                raw > std::numeric_limits<int>::max()) {
                AddError(diagnostics, file, order_pointer, "key \"order\" is out of range");
            } else {
                member.order = static_cast<int>(raw);
            }
        }
    }

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

bool ParseAttribute(const json& value, const std::string& mod_id,
                    const std::string& file, const std::string& pointer,
                    std::vector<Diagnostic>& diagnostics, AttributeDef& attribute,
                    std::vector<MemberDef>& members) {
    if (!value.is_object()) {
        AddError(diagnostics, file, pointer, "attribute must be an object");
        return false;
    }
    static const std::vector<std::string> keys = {"identifier", "extensible", "visibility",
                                                  "members"};
    const std::size_t initial_diagnostics = diagnostics.size();
    CheckUnknownKeys(value, keys, file, pointer, diagnostics);

    const std::optional<std::string> identifier =
        ReadString(value, "identifier", file, pointer, diagnostics, true);
    if (identifier) {
        attribute.identifier = *identifier;
        ValidateIdentifier(*identifier, mod_id, file,
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
            for (std::size_t index = 0; index < inline_members->size(); ++index) {
                MemberDef member;
                if (ParseMember((*inline_members)[index], *identifier, false, mod_id, file,
                                match::expr::AppendPointerToken(members_pointer,
                                                                std::to_string(index)),
                                diagnostics, member)) {
                    members.push_back(std::move(member));
                }
            }
        }
    }

    attribute.file = file;
    return diagnostics.size() == initial_diagnostics;
}

void CollectJsonFiles(const fs::path& folder, bool recursive,
                      std::vector<fs::path>& files) {
    std::error_code error;
    if (!fs::is_directory(folder, error)) {
        return;
    }
    if (recursive) {
        fs::recursive_directory_iterator iterator(folder, error);
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error) && iterator->path().extension() == ".json") {
                files.push_back(iterator->path());
            }
            iterator.increment(error);
        }
    } else {
        fs::directory_iterator iterator(folder, error);
        const fs::directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error) && iterator->path().extension() == ".json") {
                files.push_back(iterator->path());
            }
            iterator.increment(error);
        }
    }
}

void LoadFile(const fs::path& path, const std::string& folder, const std::string& label,
              const std::string& mod_id, LoadOutcome& outcome,
              std::map<std::string, std::string>& attribute_files,
              std::map<std::string, std::string>& member_files) {
    std::vector<Diagnostic>& diagnostics = outcome.diagnostics;

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "cannot open file");
        return;
    }

    json document;
    try {
        input >> document;
    } catch (const json::exception& error) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 std::string("invalid JSON: ") + error.what());
        return;
    }

    if (!document.is_object()) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "file must be a JSON object");
        return;
    }

    const auto format_version = document.find("format_version");
    if (format_version == document.end()) {
        AddError(diagnostics, label, PointerFor("format_version"),
                 "missing required key \"format_version\"");
        return;
    }
    if (!format_version->is_string() ||
        format_version->get<std::string>() != kFormatVersion) {
        AddError(diagnostics, label, PointerFor("format_version"),
                 "format_version must be \"1.0.0\"");
        return;
    }

    std::vector<std::string> roots;
    for (const auto& entry : document.items()) {
        if (entry.key() != "format_version") {
            roots.push_back(entry.key());
        }
    }
    if (roots.empty()) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "missing root key");
        return;
    }
    if (roots.size() > 1) {
        AddError(diagnostics, label, std::string(match::expr::kRootPointer),
                 "exactly one root key is allowed besides \"format_version\"");
        return;
    }

    const std::string& root = roots.front();
    if (!Contains(RootKeys(), root)) {
        AddError(diagnostics, label, PointerFor(root),
                 "unknown root key \"" + root + "\"",
                 match::expr::SuggestClosest(root, RootKeys()));
        return;
    }
    if (!Contains(AllowedRootsFor(folder), root)) {
        AddError(diagnostics, label, PointerFor(root),
                 "root key '" + root + "' is not allowed in " + folder + "/");
        return;
    }

    const json& value = document.at(root);
    const std::string pointer = PointerFor(root);
    if (!value.is_object()) {
        AddError(diagnostics, label, pointer, root + " must be an object");
        return;
    }

    if (root == "attribute") {
        AttributeDef attribute;
        std::vector<MemberDef> members;
        if (!ParseAttribute(value, mod_id, label, pointer, diagnostics, attribute, members)) {
            return;
        }
        const auto existing = attribute_files.find(attribute.identifier);
        if (existing != attribute_files.end()) {
            AddError(diagnostics, label,
                     match::expr::AppendPointerToken(pointer, "identifier"),
                     "duplicate attribute identifier \"" + attribute.identifier +
                         "\" in " + label + "; first declared in " + existing->second);
            return;
        }
        attribute_files.emplace(attribute.identifier, label);
        outcome.schema.attributes.push_back(std::move(attribute));
        for (MemberDef& member : members) {
            const auto duplicate = member_files.find(member.identifier);
            if (duplicate != member_files.end()) {
                AddError(diagnostics, label,
                         match::expr::AppendPointerToken(pointer, "members"),
                         "duplicate member identifier \"" + member.identifier + "\" in " +
                             label + "; first declared in " + duplicate->second);
                continue;
            }
            member_files.emplace(member.identifier, label);
            outcome.schema.members.push_back(std::move(member));
        }
        return;
    }

    if (root == "attribute_member") {
        MemberDef member;
        if (!ParseMember(value, "", true, mod_id, label, pointer, diagnostics, member)) {
            return;
        }
        const auto duplicate = member_files.find(member.identifier);
        if (duplicate != member_files.end()) {
            AddError(diagnostics, label,
                     match::expr::AppendPointerToken(pointer, "identifier"),
                     "duplicate member identifier \"" + member.identifier + "\" in " +
                         label + "; first declared in " + duplicate->second);
            return;
        }
        member_files.emplace(member.identifier, label);
        outcome.schema.members.push_back(std::move(member));
    }
}

}  // namespace

LoadOutcome LoadModSchema(const fs::path& mod_directory, const std::string& mod_id) {
    LoadOutcome outcome;
    outcome.schema.mod_id = mod_id;
    outcome.schema.directory = mod_directory.generic_string();

    std::vector<fs::path> files;
    CollectJsonFiles(mod_directory / "attributes", true, files);
    CollectJsonFiles(mod_directory / "kinds", false, files);
    CollectJsonFiles(mod_directory / "decks", false, files);
    CollectJsonFiles(mod_directory / "properties", false, files);
    std::sort(files.begin(), files.end(), [](const fs::path& left, const fs::path& right) {
        return left.generic_string() < right.generic_string();
    });

    std::map<std::string, std::string> attribute_files;
    std::map<std::string, std::string> member_files;

    for (const fs::path& file : files) {
        const fs::path relative = file.lexically_relative(mod_directory);
        if (relative.empty()) {
            continue;
        }
        const std::string folder = relative.begin()->string();
        LoadFile(file, folder, PathLabel(mod_id, relative), mod_id, outcome, attribute_files,
                 member_files);
    }

    std::stable_sort(outcome.schema.members.begin(), outcome.schema.members.end(),
                     [](const MemberDef& left, const MemberDef& right) {
                         if (left.order != right.order) {
                             return left.order < right.order;
                         }
                         return left.identifier < right.identifier;
                     });
    return outcome;
}

}  // namespace match::cardschema
