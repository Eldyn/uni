#include <match/expr/schema_json.hpp>

#include <map>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace match::expr {
namespace {

std::optional<Type> TypeFromName(const std::string& name) {
    if (name == "bool") {
        return Type::kBool;
    }
    if (name == "int") {
        return Type::kInt;
    }
    if (name == "float") {
        return Type::kFloat;
    }
    if (name == "string") {
        return Type::kString;
    }
    if (name == "member_set") {
        return Type::kMemberSet;
    }
    if (name == "seat") {
        return Type::kSeat;
    }
    if (name == "seat_set") {
        return Type::kSeatSet;
    }
    if (name == "card") {
        return Type::kCard;
    }
    if (name == "card_set") {
        return Type::kCardSet;
    }
    return std::nullopt;
}

bool LoadProperties(const json& value, const std::string& key,
                    std::map<std::string, Type>& out, std::string& error) {
    const auto found = value.find(key);
    if (found == value.end()) {
        return true;
    }
    if (!found->is_object()) {
        error = key + " must be an object";
        return false;
    }
    for (const auto& [name, type_value] : found->items()) {
        if (!type_value.is_string()) {
            error = key + "." + name + " must be a type name";
            return false;
        }
        const std::optional<Type> type = TypeFromName(type_value.get<std::string>());
        if (!type.has_value()) {
            error = key + "." + name + " has unknown type \"" +
                    type_value.get<std::string>() + "\"";
            return false;
        }
        out[name] = *type;
    }
    return true;
}

bool LoadCardSlots(const json& value, std::map<std::string, Type>& out, std::string& error) {
    const auto found = value.find("card_slots");
    if (found == value.end()) {
        return true;
    }
    if (!found->is_array()) {
        error = "card_slots must be an array of names";
        return false;
    }
    for (const json& slot : *found) {
        if (!slot.is_string()) {
            error = "card_slots must hold names";
            return false;
        }
        out[slot.get<std::string>()] = Type::kMemberSet;
    }
    return true;
}

bool LoadVariables(const json& value, std::map<std::string, VariableInfo>& out,
                   std::string& error) {
    const auto found = value.find("variables");
    if (found == value.end()) {
        return true;
    }
    if (!found->is_object()) {
        error = "variables must be an object";
        return false;
    }
    for (const auto& [name, definition] : found->items()) {
        if (!definition.is_object()) {
            error = "variables." + name + " must be an object";
            return false;
        }
        const auto type = definition.find("type");
        if (type == definition.end() || !type->is_string()) {
            error = "variables." + name + " needs a type name";
            return false;
        }
        const std::optional<Type> parsed = TypeFromName(type->get<std::string>());
        if (!parsed.has_value()) {
            error = "variables." + name + " has unknown type \"" +
                    type->get<std::string>() + "\"";
            return false;
        }
        bool optional = false;
        const auto optional_value = definition.find("optional");
        if (optional_value != definition.end()) {
            if (!optional_value->is_boolean()) {
                error = "variables." + name + ".optional must be a bool";
                return false;
            }
            optional = optional_value->get<bool>();
        }
        out[name] = VariableInfo{*parsed, optional};
    }
    return true;
}

}  // namespace

std::optional<Schema> LoadSchema(const json& value, std::string& error) {
    if (!value.is_object()) {
        error = "the schema must be an object";
        return std::nullopt;
    }
    Schema schema;
    if (!LoadProperties(value, "seat_properties", schema.seat_properties, error)) {
        return std::nullopt;
    }
    if (!LoadProperties(value, "card_properties", schema.card_properties, error)) {
        return std::nullopt;
    }
    if (!LoadProperties(value, "match_properties", schema.match_properties, error)) {
        return std::nullopt;
    }
    if (!LoadProperties(value, "settings", schema.settings, error)) {
        return std::nullopt;
    }
    if (!LoadCardSlots(value, schema.card_slots, error)) {
        return std::nullopt;
    }
    if (!LoadVariables(value, schema.variables, error)) {
        return std::nullopt;
    }
    return schema;
}

}  // namespace match::expr
