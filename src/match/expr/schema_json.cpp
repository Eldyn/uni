#include <match/expr/schema_json.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <match/expr/diagnostic.hpp>

namespace match::expr {
namespace {

constexpr std::string_view kRootPointer = "/";
constexpr std::string_view kRootKeys[] = {"seat_properties", "card_properties",
                                          "match_properties", "settings",
                                          "card_slots",      "variables"};
constexpr std::string_view kVariableKeys[] = {"type", "optional"};

// INFO: RFC 6901: `~` escapes to `~0` and `/` escapes to `~1`.
std::string EscapePointerToken(std::string_view token) {
    std::string escaped;
    escaped.reserve(token.size());
    for (const char character : token) {
        if (character == '~') {
            escaped += "~0";
        } else if (character == '/') {
            escaped += "~1";
        } else {
            escaped.push_back(character);
        }
    }
    return escaped;
}

std::string AppendPointer(std::string_view pointer, std::string_view token) {
    std::string result(pointer);
    if (result.empty()) {
        result = "/";
    } else if (result.back() != '/') {
        result.push_back('/');
    }
    result += EscapePointerToken(token);
    return result;
}

// INFO: scans JSON text for keys that repeat inside one object. nlohmann keeps
// the last value of a repeated key when it builds a value, so the repeats have
// to be seen while the text is parsed. The scan tracks the RFC 6901 pointer of
// every open container and the keys already seen in each open object; the value
// itself is discarded.
class DuplicateKeyScanner final : public nlohmann::json_sax<json> {
public:
    explicit DuplicateKeyScanner(std::vector<DuplicateKey>& duplicates)
        : duplicates_(duplicates) {}

    bool null() override { return ReadScalar(); }
    bool boolean(bool) override { return ReadScalar(); }
    bool number_integer(number_integer_t) override { return ReadScalar(); }
    bool number_unsigned(number_unsigned_t) override { return ReadScalar(); }
    bool number_float(number_float_t, const string_t&) override { return ReadScalar(); }
    bool string(string_t&) override { return ReadScalar(); }
    bool binary(binary_t&) override { return ReadScalar(); }

    bool start_object(std::size_t) override {
        return OpenContainer(true);
    }

    bool key(string_t& name) override {
        Container& container = containers_.back();
        const bool repeated = !container.keys.insert(name).second;
        if (repeated) {
            duplicates_.push_back(DuplicateKey{CurrentPointer(), name});
        }
        pending_key_ = name;
        has_pending_key_ = true;
        return true;
    }

    bool end_object() override { return CloseContainer(); }

    bool start_array(std::size_t) override {
        return OpenContainer(false);
    }

    bool end_array() override { return CloseContainer(); }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
        return false;
    }

private:
    // INFO: one open object or array; `pushed_token` says whether its own token
    // is on `tokens_`, and `next_index` counts the elements of an array.
    struct Container {
        bool is_object;
        bool pushed_token;
        std::unordered_set<std::string> keys;
        std::size_t next_index;
    };

    bool OpenContainer(bool is_object) {
        const bool pushed_token = BeginValue();
        containers_.push_back(Container{is_object, pushed_token, {}, 0});
        return true;
    }

    bool CloseContainer() {
        const Container& container = containers_.back();
        if (container.pushed_token) {
            tokens_.pop_back();
        }
        containers_.pop_back();
        return true;
    }

    bool ReadScalar() {
        const bool pushed_token = BeginValue();
        if (pushed_token) {
            tokens_.pop_back();
        }
        return true;
    }

    // INFO: pushes the token of the value that starts here: the key an object
    // set, or the next index of an array; the root value has no token.
    bool BeginValue() {
        if (has_pending_key_) {
            tokens_.push_back(pending_key_);
            has_pending_key_ = false;
            return true;
        }
        if (!containers_.empty() && !containers_.back().is_object) {
            tokens_.push_back(std::to_string(containers_.back().next_index++));
            return true;
        }
        return false;
    }

    std::string CurrentPointer() const {
        std::string pointer;
        for (const std::string& token : tokens_) {
            pointer = AppendPointer(pointer, token);
        }
        return pointer.empty() ? std::string(kRootPointer) : pointer;
    }

    std::vector<DuplicateKey>& duplicates_;
    std::vector<Container> containers_;
    std::vector<std::string> tokens_;
    std::string pending_key_;
    bool has_pending_key_ = false;
};

std::string SuggestClosestType(std::string_view name) {
    std::vector<std::string> candidates;
    for (const auto& [text, type] : kTypeNames) {
        candidates.emplace_back(text);
    }
    return SuggestClosest(name, candidates);
}

template <typename AllowedKeys>
bool IsAllowedKey(std::string_view key, const AllowedKeys& allowed) {
    for (const std::string_view candidate : allowed) {
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

class SchemaLoader {
public:
    SchemaLoader(const json& value, std::string_view file_label,
                 const std::vector<DuplicateKey>& duplicates)
        : value_(value), file_label_(file_label), duplicates_(duplicates) {}

    std::optional<Schema> Load() {
        AddDuplicates();
        if (!value_.is_object()) {
            Add(kRootPointer, "the schema must be an object");
            return std::nullopt;
        }
        CheckKeys(value_, kRootPointer, kRootKeys);
        LoadProperties("seat_properties", schema_.seat_properties);
        LoadProperties("card_properties", schema_.card_properties);
        LoadProperties("match_properties", schema_.match_properties);
        LoadProperties("settings", schema_.settings);
        LoadCardSlots();
        LoadVariables();
        if (!diagnostics_.empty()) {
            return std::nullopt;
        }
        return std::move(schema_);
    }

    std::vector<Diagnostic> TakeDiagnostics() { return std::move(diagnostics_); }

private:
    void Add(std::string_view pointer, std::string message, std::string suggestion = {}) {
        diagnostics_.push_back(Diagnostic{std::string(file_label_), std::string(pointer),
                                          std::move(message), std::move(suggestion)});
    }

    // INFO: a repeated key is reported against the object that holds it, since
    // that is where the losing value was written.
    void AddDuplicates() {
        for (const DuplicateKey& duplicate : duplicates_) {
            Add(duplicate.pointer, "duplicate key \"" + duplicate.key + "\"");
        }
    }

    template <typename AllowedKeys>
    void CheckKeys(const json& object, std::string_view pointer, const AllowedKeys& allowed) {
        std::vector<std::string> candidates;
        for (const std::string_view key : allowed) {
            candidates.emplace_back(key);
        }
        for (auto entry = object.begin(); entry != object.end(); ++entry) {
            if (!IsAllowedKey(entry.key(), allowed)) {
                Add(std::string(pointer), "unknown key \"" + entry.key() + "\"",
                    SuggestClosest(entry.key(), candidates));
            }
        }
    }

    void LoadProperties(std::string_view key, std::map<std::string, Type>& out) {
        const auto found = value_.find(std::string(key));
        if (found == value_.end()) {
            return;
        }
        const std::string pointer = AppendPointer(kRootPointer, key);
        if (!found->is_object()) {
            Add(pointer, "must be an object");
            return;
        }
        for (auto entry = found->begin(); entry != found->end(); ++entry) {
            const std::string property_pointer = AppendPointer(pointer, entry.key());
            if (!entry->is_string()) {
                Add(property_pointer, "must be a type name");
                continue;
            }
            const std::string name = entry->get<std::string>();
            const std::optional<Type> type = TypeFromName(name);
            if (!type.has_value()) {
                Add(property_pointer, "unknown type \"" + name + "\"",
                    SuggestClosestType(name));
                continue;
            }
            out[entry.key()] = *type;
        }
    }

    void LoadCardSlots() {
        const auto found = value_.find("card_slots");
        if (found == value_.end()) {
            return;
        }
        if (!found->is_array()) {
            Add(AppendPointer(kRootPointer, "card_slots"), "must be an array of names");
            return;
        }
        for (std::size_t index = 0; index < found->size(); ++index) {
            const std::string pointer =
                AppendPointer(AppendPointer(kRootPointer, "card_slots"),
                              std::to_string(index));
            const json& slot = (*found)[index];
            if (!slot.is_string()) {
                Add(pointer, "must be a name");
                continue;
            }
            schema_.card_slots[slot.get<std::string>()] = Type::kMemberSet;
        }
    }

    void LoadVariables() {
        const auto found = value_.find("variables");
        if (found == value_.end()) {
            return;
        }
        if (!found->is_object()) {
            Add(AppendPointer(kRootPointer, "variables"), "must be an object");
            return;
        }
        for (auto entry = found->begin(); entry != found->end(); ++entry) {
            const std::string pointer = AppendPointer(AppendPointer(kRootPointer, "variables"),
                                                      entry.key());
            if (!entry->is_object()) {
                Add(pointer, "must be an object");
                continue;
            }
            CheckKeys(*entry, pointer, kVariableKeys);
            LoadVariable(entry.key(), *entry, pointer);
        }
    }

    void LoadVariable(const std::string& name, const json& definition,
                      const std::string& pointer) {
        const std::string type_pointer = AppendPointer(pointer, "type");
        const auto type = definition.find("type");
        if (type == definition.end() || !type->is_string()) {
            Add(type_pointer, "needs a type name");
            return;
        }
        const std::string type_name = type->get<std::string>();
        const std::optional<Type> parsed = TypeFromName(type_name);
        if (!parsed.has_value()) {
            Add(type_pointer, "unknown type \"" + type_name + "\"",
                SuggestClosestType(type_name));
            return;
        }
        bool optional = false;
        const auto optional_value = definition.find("optional");
        if (optional_value != definition.end()) {
            if (!optional_value->is_boolean()) {
                Add(AppendPointer(pointer, "optional"), "must be a bool");
                return;
            }
            optional = optional_value->get<bool>();
        }
        schema_.variables[name] = VariableInfo{*parsed, optional};
    }

    const json& value_;
    std::string_view file_label_;
    const std::vector<DuplicateKey>& duplicates_;
    Schema schema_;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace

std::optional<json> ParseSchemaJson(const std::string& text,
                                    std::vector<DuplicateKey>& duplicates) {
    const json value = json::parse(text, nullptr, false);
    if (value.is_discarded()) {
        return std::nullopt;
    }
    // INFO: the parse above proved the text is valid JSON, so the scan for
    // repeated keys cannot reject it and its result is discarded on purpose.
    DuplicateKeyScanner scanner(duplicates);
    static_cast<void>(json::sax_parse(text, &scanner));
    return value;
}

std::optional<Schema> LoadSchema(const json& value, std::string_view file_label,
                                 const std::vector<DuplicateKey>& duplicates,
                                 std::vector<Diagnostic>& diagnostics) {
    SchemaLoader loader(value, file_label, duplicates);
    std::optional<Schema> schema = loader.Load();
    diagnostics = loader.TakeDiagnostics();
    return schema;
}

std::optional<Schema> LoadSchema(const json& value, std::string_view file_label,
                                 std::vector<Diagnostic>& diagnostics) {
    const std::vector<DuplicateKey> no_duplicates;
    return LoadSchema(value, file_label, no_duplicates, diagnostics);
}

std::optional<Schema> LoadSchema(const json& value, std::string& error) {
    std::vector<Diagnostic> diagnostics;
    std::optional<Schema> schema = LoadSchema(value, "", diagnostics);
    if (!schema.has_value() && !diagnostics.empty()) {
        const Diagnostic& first = diagnostics.front();
        error = first.pointer + ": " + first.message;
        if (!first.suggestion.empty()) {
            error += " (did you mean \"" + first.suggestion + "\")";
        }
    }
    return schema;
}

}  // namespace match::expr
