#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <match/expr/context.hpp>
#include <match/expr/types.hpp>
#include <match/expr/value.hpp>

namespace match::expr::testing {

using json = nlohmann::json;

// INFO: the schema the expression tests share; it is small and hand written so
// a test failure points at the rule under test, not at a fixture.
inline Schema MakeTestSchema() {
    Schema schema;
    schema.seat_properties = {
        {"hand_size", Type::kInt},
        {"seat", Type::kInt},
        {"name", Type::kString},
        {"hand", Type::kCardSet},
        {"axe:stunned", Type::kBool},
    };
    schema.card_properties = {
        {"kind", Type::kString},
    };
    schema.card_slots = {
        {"color", Type::kMemberSet},
        {"value", Type::kMemberSet},
    };
    schema.match_properties = {
        {"turn", Type::kInt},
        {"top", Type::kCard},
        {"tree:growth", Type::kInt},
        {"uni:active_color", Type::kMemberSet},
    };
    schema.settings = {
        {"uni:window_ms", Type::kInt},
    };
    schema.variables = {
        {"played", VariableInfo{Type::kCard, false}},
        {"responder", VariableInfo{Type::kSeat, true}},
        {"choice", VariableInfo{Type::kMemberSet, true}},
        {"rule", VariableInfo{Type::kRule, false}},
    };
    return schema;
}

// INFO: the EvalContext a conformance case builds from its JSON `world`. Seats
// come from `seats` (each with a `name`, a `properties` map and optional
// `tags`); `self` is the acting seat index; `match` and `settings` are property
// maps; `variables` holds the bound values and `random` scripts the RNG. A
// property or variable is decoded with the schema type it fills, so a member
// set is an array of member names, a seat is a seat name, a card is an object
// `{ "name", "properties", "tags" }` and a card set is an array of those. A
// float may be the string "nan", "inf" or "-inf" because JSON has no such
// literals. A property the schema does not declare is decoded from its JSON
// kind alone.
class JsonWorld : public EvalContext {
public:
    static constexpr EntityRef kMatchEntity{Type::kMatch, 0};
    static constexpr EntityRef kSettingsEntity{Type::kSettings, 0};

    JsonWorld(const Schema& schema, const json& world) : schema_(schema) { Load(world); }

    const std::string& error() const { return error_; }

    std::vector<EntityRef> ResolveSelector(std::string_view name) const override {
        if (name == "match") {
            return {kMatchEntity};
        }
        if (name == "settings") {
            return {kSettingsEntity};
        }
        if (seats_.empty()) {
            return {};
        }
        if (name == "self") {
            return {SeatRef(self_)};
        }
        if (name == "next") {
            return {SeatRef((self_ + 1) % seats_.size())};
        }
        if (name == "prev") {
            return {SeatRef((self_ + seats_.size() - 1) % seats_.size())};
        }
        if (name == "all") {
            std::vector<EntityRef> seats;
            for (size_t index = 0; index < seats_.size(); ++index) {
                seats.push_back(SeatRef(index));
            }
            return seats;
        }
        if (name == "others") {
            std::vector<EntityRef> seats;
            for (size_t index = 0; index < seats_.size(); ++index) {
                if (index != self_) {
                    seats.push_back(SeatRef(index));
                }
            }
            return seats;
        }
        if (name == "rand") {
            return PickSeat(false);
        }
        if (name == "rand_all") {
            return PickSeat(true);
        }
        return {};
    }

    std::optional<Value> ReadProperty(EntityRef entity, std::string_view key) const override {
        const std::map<std::string, Value>* properties = FindProperties(entity);
        if (properties == nullptr) {
            return std::nullopt;
        }
        const auto found = properties->find(std::string(key));
        if (found == properties->end()) {
            return std::nullopt;
        }
        return found->second;
    }

    bool WriteProperty(EntityRef entity, std::string_view key, const Value& value) override {
        std::map<std::string, Value>* properties = MutableProperties(entity);
        if (properties == nullptr) {
            return false;
        }
        const auto found = properties->find(std::string(key));
        if (found == properties->end()) {
            return false;
        }
        found->second = value;
        return true;
    }

    std::optional<Value> GetVariable(std::string_view name) const override {
        const auto found = variables_.find(std::string(name));
        if (found == variables_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    bool HasTag(EntityRef entity, std::string_view tag) const override {
        if (entity.kind == Type::kCard && entity.id < cards_.size()) {
            return cards_[entity.id].tags.count(std::string(tag)) > 0;
        }
        return false;
    }

    int64_t Random(int64_t low, int64_t high) override { return NextRandom(low, high); }

    int64_t StatusMagnitude(EntityRef seat, std::string_view status) const override {
        const std::map<std::string, Value>* properties = FindProperties(seat);
        if (properties == nullptr) {
            return 0;
        }
        const auto found = properties->find("status:" + std::string(status));
        if (found == properties->end()) {
            return 0;
        }
        const int64_t* magnitude = std::get_if<int64_t>(&found->second);
        return magnitude == nullptr ? 0 : *magnitude;
    }

    // INFO: decodes a JSON value with the type the checker gave the expression;
    // the runner uses it to read the `expect.value` of a case.
    bool Decode(Type type, const json& value, Value& out, std::string& error) {
        switch (type) {
            case Type::kBool:
                return DecodeBool(value, out, error);
            case Type::kInt:
                return DecodeInt(value, out, error);
            case Type::kFloat:
                return DecodeFloat(value, out, error);
            case Type::kString:
                return DecodeString(value, out, error);
            case Type::kMemberSet:
                return DecodeMemberSet(value, out, error);
            case Type::kSeat:
                return DecodeSeat(value, out, error);
            case Type::kSeatSet:
                return DecodeSeatSet(value, out, error);
            case Type::kCard:
                return DecodeCard(value, out, error);
            case Type::kCardSet:
                return DecodeCardSet(value, out, error);
            default:
                error = "the world cannot hold a value of type " + TypeName(type);
                return false;
        }
    }

    // INFO: decodes a JSON value without a declared type; used for a property
    // the schema does not declare.
    bool DecodeNatural(const json& value, Value& out, std::string& error) {
        if (value.is_boolean()) {
            out = Value(value.get<bool>());
            return true;
        }
        if (value.is_number_integer()) {
            out = Value(value.get<int64_t>());
            return true;
        }
        if (value.is_number_float()) {
            out = Value(value.get<double>());
            return true;
        }
        if (value.is_string()) {
            out = Value(value.get<std::string>());
            return true;
        }
        if (value.is_array()) {
            return DecodeMemberSet(value, out, error);
        }
        error = "cannot decode the value";
        return false;
    }

private:
    struct SeatData {
        std::string name;
        std::map<std::string, Value> properties;
        std::set<std::string> tags;
    };

    struct CardData {
        std::string name;
        std::map<std::string, Value> properties;
        std::set<std::string> tags;
    };

    void Load(const json& world) {
        if (!world.is_object()) {
            error_ = "the world must be an object";
            return;
        }
        if (const auto seats = world.find("seats"); seats != world.end()) {
            if (!LoadSeats(*seats)) {
                return;
            }
        }
        if (const auto self = world.find("self"); self != world.end()) {
            if (!self->is_number_integer()) {
                error_ = "world.self must be a seat index";
                return;
            }
            const int64_t index = self->get<int64_t>();
            if (index < 0 ||
                (!seats_.empty() && static_cast<size_t>(index) >= seats_.size())) {
                error_ = "world.self is out of range";
                return;
            }
            self_ = static_cast<size_t>(index);
        }
        if (const auto match = world.find("match"); match != world.end()) {
            if (!DecodeProperties(*match, Type::kMatch, match_, error_)) {
                return;
            }
        }
        if (const auto settings = world.find("settings"); settings != world.end()) {
            if (!DecodeProperties(*settings, Type::kSettings, settings_, error_)) {
                return;
            }
        }
        if (const auto variables = world.find("variables"); variables != world.end()) {
            if (!LoadVariables(*variables)) {
                return;
            }
        }
        if (const auto random = world.find("random"); random != world.end()) {
            if (!random->is_array()) {
                error_ = "world.random must be an array";
                return;
            }
            for (const json& item : *random) {
                if (!item.is_number_integer()) {
                    error_ = "world.random must hold ints";
                    return;
                }
                random_.push_back(item.get<int64_t>());
            }
        }
    }

    bool LoadSeats(const json& value) {
        if (!value.is_array()) {
            error_ = "world.seats must be an array";
            return false;
        }
        for (const json& seat : value) {
            if (!seat.is_object()) {
                error_ = "a seat must be an object";
                return false;
            }
            SeatData data;
            if (const auto name = seat.find("name"); name != seat.end()) {
                if (!name->is_string()) {
                    error_ = "a seat name must be a string";
                    return false;
                }
                data.name = name->get<std::string>();
            }
            if (const auto properties = seat.find("properties"); properties != seat.end()) {
                if (!DecodeProperties(*properties, Type::kSeat, data.properties, error_)) {
                    return false;
                }
            }
            if (const auto tags = seat.find("tags"); tags != seat.end()) {
                if (!DecodeTags(*tags, data.tags, error_)) {
                    return false;
                }
            }
            seat_names_[data.name] = seats_.size();
            seats_.push_back(std::move(data));
        }
        return true;
    }

    bool LoadVariables(const json& value) {
        if (!value.is_object()) {
            error_ = "world.variables must be an object";
            return false;
        }
        for (const auto& [name, item] : value.items()) {
            const auto variable = schema_.variables.find(name);
            if (variable == schema_.variables.end()) {
                error_ = "world variable $" + name + " is not in the schema";
                return false;
            }
            Value decoded;
            if (!Decode(variable->second.type, item, decoded, error_)) {
                return false;
            }
            variables_[name] = decoded;
        }
        return true;
    }

    bool DecodeProperties(const json& value, Type entity,
                          std::map<std::string, Value>& out, std::string& error) {
        if (!value.is_object()) {
            error = "properties must be an object";
            return false;
        }
        for (const auto& [key, item] : value.items()) {
            Value decoded;
            const std::optional<Type> type = schema_.PropertyType(entity, key);
            if (type.has_value()) {
                if (!Decode(*type, item, decoded, error)) {
                    return false;
                }
            } else if (!DecodeNatural(item, decoded, error)) {
                return false;
            }
            out[key] = decoded;
        }
        return true;
    }

    bool DecodeTags(const json& value, std::set<std::string>& out, std::string& error) const {
        if (!value.is_array()) {
            error = "tags must be an array of strings";
            return false;
        }
        for (const json& item : value) {
            if (!item.is_string()) {
                error = "tags must be an array of strings";
                return false;
            }
            out.insert(item.get<std::string>());
        }
        return true;
    }

    bool DecodeBool(const json& value, Value& out, std::string& error) const {
        if (!value.is_boolean()) {
            error = "expected a bool";
            return false;
        }
        out = Value(value.get<bool>());
        return true;
    }

    bool DecodeInt(const json& value, Value& out, std::string& error) const {
        if (value.is_number_integer()) {
            out = Value(value.get<int64_t>());
            return true;
        }
        // INFO: an expected int outside +-2^53 is written as {"int": "<digits>"}
        // because JSON.parse in the TypeScript client cannot read it losslessly.
        if (value.is_object() && value.size() == 1) {
            const auto encoded = value.find("int");
            if (encoded != value.end() && encoded->is_string()) {
                const std::string text = encoded->get<std::string>();
                int64_t number = 0;
                const auto result =
                    std::from_chars(text.data(), text.data() + text.size(), number);
                if (result.ec == std::errc() &&
                    result.ptr == text.data() + text.size()) {
                    out = Value(number);
                    return true;
                }
            }
        }
        error = "expected an int or {\"int\": \"<decimal digits>\"}";
        return false;
    }

    bool DecodeFloat(const json& value, Value& out, std::string& error) const {
        if (value.is_number_integer()) {
            out = Value(static_cast<double>(value.get<int64_t>()));
            return true;
        }
        if (value.is_number_float()) {
            out = Value(value.get<double>());
            return true;
        }
        if (value.is_string()) {
            const std::string text = value.get<std::string>();
            if (text == "nan") {
                out = Value(std::numeric_limits<double>::quiet_NaN());
                return true;
            }
            if (text == "inf") {
                out = Value(std::numeric_limits<double>::infinity());
                return true;
            }
            if (text == "-inf") {
                out = Value(-std::numeric_limits<double>::infinity());
                return true;
            }
        }
        error = "expected a float";
        return false;
    }

    bool DecodeString(const json& value, Value& out, std::string& error) const {
        if (!value.is_string()) {
            error = "expected a string";
            return false;
        }
        out = Value(value.get<std::string>());
        return true;
    }

    bool DecodeMemberSet(const json& value, Value& out, std::string& error) const {
        if (!value.is_array()) {
            error = "expected an array of member names";
            return false;
        }
        std::set<std::string> members;
        for (const json& item : value) {
            if (!item.is_string()) {
                error = "expected member names";
                return false;
            }
            members.insert(item.get<std::string>());
        }
        out = Value(members);
        return true;
    }

    bool DecodeSeat(const json& value, Value& out, std::string& error) const {
        if (value.is_string()) {
            const auto found = seat_names_.find(value.get<std::string>());
            if (found == seat_names_.end()) {
                error = "unknown seat \"" + value.get<std::string>() + "\"";
                return false;
            }
            out = Value(SeatRef(found->second));
            return true;
        }
        if (value.is_number_integer()) {
            const int64_t index = value.get<int64_t>();
            if (index < 0 || static_cast<size_t>(index) >= seats_.size()) {
                error = "seat index out of range";
                return false;
            }
            out = Value(SeatRef(static_cast<size_t>(index)));
            return true;
        }
        error = "expected a seat name";
        return false;
    }

    bool DecodeSeatSet(const json& value, Value& out, std::string& error) const {
        if (!value.is_array()) {
            error = "expected an array of seat names";
            return false;
        }
        std::vector<EntityRef> seats;
        for (const json& item : value) {
            Value decoded;
            if (!DecodeSeat(item, decoded, error)) {
                return false;
            }
            seats.push_back(std::get<EntityRef>(decoded));
        }
        out = Value(seats);
        return true;
    }

    bool DecodeCard(const json& value, Value& out, std::string& error) {
        if (value.is_string()) {
            out = Value(EntityRef{Type::kCard, InternCard(value.get<std::string>())});
            return true;
        }
        if (!value.is_object()) {
            error = "expected a card object";
            return false;
        }
        const auto name = value.find("name");
        if (name == value.end() || !name->is_string()) {
            error = "a card needs a name";
            return false;
        }
        const size_t id = InternCard(name->get<std::string>());
        if (const auto properties = value.find("properties"); properties != value.end()) {
            if (!DecodeProperties(*properties, Type::kCard, cards_[id].properties, error)) {
                return false;
            }
        }
        if (const auto tags = value.find("tags"); tags != value.end()) {
            if (!DecodeTags(*tags, cards_[id].tags, error)) {
                return false;
            }
        }
        out = Value(EntityRef{Type::kCard, id});
        return true;
    }

    bool DecodeCardSet(const json& value, Value& out, std::string& error) {
        if (!value.is_array()) {
            error = "expected an array of cards";
            return false;
        }
        std::vector<EntityRef> cards;
        for (const json& item : value) {
            Value decoded;
            if (!DecodeCard(item, decoded, error)) {
                return false;
            }
            cards.push_back(std::get<EntityRef>(decoded));
        }
        out = Value(cards);
        return true;
    }

    size_t InternCard(const std::string& name) {
        const auto found = card_ids_.find(name);
        if (found != card_ids_.end()) {
            return found->second;
        }
        const size_t id = cards_.size();
        card_ids_[name] = id;
        CardData data;
        data.name = name;
        cards_.push_back(std::move(data));
        return id;
    }

    EntityRef SeatRef(size_t index) const {
        return EntityRef{Type::kSeat, static_cast<uint64_t>(index)};
    }

    std::vector<EntityRef> PickSeat(bool include_self) const {
        std::vector<size_t> candidates;
        for (size_t index = 0; index < seats_.size(); ++index) {
            if (include_self || index != self_) {
                candidates.push_back(index);
            }
        }
        if (candidates.empty()) {
            return {};
        }
        const int64_t choice = NextRandom(0, static_cast<int64_t>(candidates.size()) - 1);
        return {SeatRef(candidates[static_cast<size_t>(choice)])};
    }

    int64_t NextRandom(int64_t low, int64_t high) const {
        if (random_.empty()) {
            return low;
        }
        const int64_t value = random_.front();
        random_.erase(random_.begin());
        if (value < low || value > high) {
            return low;
        }
        return value;
    }

    const std::map<std::string, Value>* FindProperties(EntityRef entity) const {
        if (entity.kind == Type::kSeat && entity.id < seats_.size()) {
            return &seats_[entity.id].properties;
        }
        if (entity.kind == Type::kCard && entity.id < cards_.size()) {
            return &cards_[entity.id].properties;
        }
        if (entity.kind == Type::kMatch) {
            return &match_;
        }
        if (entity.kind == Type::kSettings) {
            return &settings_;
        }
        return nullptr;
    }

    std::map<std::string, Value>* MutableProperties(EntityRef entity) {
        if (entity.kind == Type::kSeat && entity.id < seats_.size()) {
            return &seats_[entity.id].properties;
        }
        if (entity.kind == Type::kCard && entity.id < cards_.size()) {
            return &cards_[entity.id].properties;
        }
        if (entity.kind == Type::kMatch) {
            return &match_;
        }
        if (entity.kind == Type::kSettings) {
            return &settings_;
        }
        return nullptr;
    }

    const Schema& schema_;
    std::string error_;
    std::vector<SeatData> seats_;
    std::map<std::string, size_t> seat_names_;
    size_t self_ = 0;
    std::vector<CardData> cards_;
    std::map<std::string, size_t> card_ids_;
    std::map<std::string, Value> match_;
    std::map<std::string, Value> settings_;
    std::map<std::string, Value> variables_;
    mutable std::vector<int64_t> random_;
};

}  // namespace match::expr::testing
