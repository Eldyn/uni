#include "avatar/loadout.hpp"

#include "avatar/catalogue.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file loadout.cpp
 * @brief Validates a saved loadout and builds the guest default look.
 *
 * Rule order is the save's reject order: the shape, then the skin, then each
 * slot choice. The first failure wins, and its detail names the slot or id.
 */

namespace uni::avatar {

namespace {

using nlohmann::json;

/** @brief The loadout slot keys that are choices: parts, then motif and set. */
const std::vector<std::string>& ChoiceSlots() {
    static const std::vector<std::string> kSlots = {
        "eyes", "mouth", "head_top", "mask", "bodywear", "set", "motif"};
    return kSlots;
}

/** @brief The two part slots a guest default always fills. */
const std::vector<std::string>& GuestSlots() {
    static const std::vector<std::string> kSlots = {"eyes", "mouth"};
    return kSlots;
}

bool Contains(const std::vector<std::string>& values,
              const std::string& needle) {
    for (const std::string& value : values) {
        if (value == needle) return true;
    }
    return false;
}

LoadoutError Error(const std::string& code, const std::string& detail) {
    return LoadoutError{code, detail};
}

/**
 * @brief Validate one slot choice for `slot` and the worn `shape_id`.
 * @return The first failure, or `std::nullopt` when the choice is acceptable.
 */
std::optional<LoadoutError> ValidateChoice(
    const std::string& slot, const json& choice, const Catalogue& catalogue,
    const IOwnership& ownership, const std::string& username,
    const std::string& shape_id) {
    if (!choice.is_object()) return Error("bad_json", slot);

    const auto item_node = choice.find("item");
    if (item_node == choice.end() || !item_node->is_string()) {
        return Error("unknown_item", slot);
    }
    const std::string item_id = item_node->get<std::string>();

    const Item* item = catalogue.FindItem(item_id);
    if (item == nullptr) return Error("unknown_item", item_id);
    if (item->slot != slot) return Error("wrong_slot", item_id);
    if (!item->shapes.empty() && !Contains(item->shapes, shape_id)) {
        return Error("shape_unsupported", item_id);
    }
    if (!catalogue.IsStarter(item_id)
        && !ownership.OwnsItem(username, item_id)) {
        return Error("not_owned", item_id);
    }

    std::vector<std::string> colours;
    const auto colours_node = choice.find("colours");
    if (colours_node != choice.end() && !colours_node->is_null()) {
        if (!colours_node->is_array()) return Error("bad_json", slot);
        for (const json& colour : *colours_node) {
            if (!colour.is_string()) return Error("unknown_swatch", slot);
            colours.push_back(colour.get<std::string>());
        }
    }

    if (static_cast<int>(colours.size()) > item->regions) {
        return Error("too_many_colours", item_id);
    }
    for (const std::string& swatch_id : colours) {
        if (!catalogue.HasSwatch(swatch_id)) {
            return Error("unknown_swatch", swatch_id);
        }
        if (!catalogue.IsStarterSwatch(swatch_id)
            && !ownership.OwnsSwatch(username, swatch_id)) {
            return Error("swatch_not_owned", swatch_id);
        }
    }
    return std::nullopt;
}

}  // namespace

bool StarterOwnership::OwnsItem(const std::string&,
                                const std::string& item_id) const {
    return catalogue_.IsStarter(item_id);
}

bool StarterOwnership::OwnsSwatch(const std::string&,
                                  const std::string& swatch_id) const {
    return catalogue_.IsStarterSwatch(swatch_id);
}

std::optional<LoadoutError> ValidateLoadout(const json& loadout,
                                            const Catalogue& catalogue,
                                            const IOwnership& ownership,
                                            const std::string& username) {
    if (!loadout.is_object()) return Error("bad_json", "loadout");

    const auto shape_node = loadout.find("shape");
    if (shape_node == loadout.end() || !shape_node->is_string()) {
        return Error("unknown_shape", "missing");
    }
    const std::string shape_id = shape_node->get<std::string>();
    const Item* shape = catalogue.FindItem(shape_id);
    if (shape == nullptr || shape->slot != "shape") {
        return Error("unknown_shape", shape_id);
    }

    const auto skin_node = loadout.find("skin");
    if (skin_node == loadout.end() || !skin_node->is_string()) {
        return Error("missing_skin", "missing");
    }
    const std::string skin_id = skin_node->get<std::string>();
    if (!catalogue.HasSwatch(skin_id)) return Error("missing_skin", skin_id);
    if (!catalogue.IsStarterSwatch(skin_id)
        && !ownership.OwnsSwatch(username, skin_id)) {
        return Error("swatch_not_owned", skin_id);
    }

    const auto slots_node = loadout.find("slots");
    if (slots_node == loadout.end() || slots_node->is_null()) {
        return std::nullopt;
    }
    if (!slots_node->is_object()) return Error("bad_json", "slots");

    for (const auto& [slot, choice] : slots_node->items()) {
        if (!Contains(ChoiceSlots(), slot)) return Error("wrong_slot", slot);
        const std::optional<LoadoutError> error = ValidateChoice(
            slot, choice, catalogue, ownership, username, shape_id);
        if (error.has_value()) return error;
    }
    return std::nullopt;
}

json GuestLoadout(const Catalogue& catalogue, uint32_t seed) {
    json loadout = json::object();

    const Item* shape = catalogue.SlotDefault("shape");
    loadout["shape"] = shape != nullptr ? shape->id : std::string();

    const std::vector<std::string> swatches = catalogue.StarterSwatchIds();
    loadout["skin"] = swatches.empty()
                          ? std::string()
                          : swatches[seed % swatches.size()];

    json slots = json::object();
    for (const std::string& slot : GuestSlots()) {
        const Item* item = catalogue.SlotDefault(slot);
        if (item == nullptr) continue;
        slots[slot] = json{{"item", item->id}, {"colours", json::array()}};
    }
    loadout["slots"] = std::move(slots);
    return loadout;
}

}  // namespace uni::avatar
