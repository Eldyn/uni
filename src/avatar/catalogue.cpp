#include "avatar/catalogue.hpp"

#include "common/env.hpp"
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @file catalogue.cpp
 * @brief Parses and validates `avatar/catalogue.json`, and holds it for the
 *        process lifetime.
 */

namespace uni::avatar {

namespace {

using nlohmann::json;

/** @brief Repo-relative default, overridable with `UNI_AVATAR_CATALOGUE`. */
constexpr const char* kDefaultPath = "avatar/catalogue.json";

/** @brief Tint regions an item may have in total. */
constexpr int kMaxRegions = 6;
/** @brief Regions one mask file carries: the R, G and B channels. */
constexpr int kRegionsPerFile = 3;
/** @brief Extra mask files an asset may list (regions 4-6 are file two). */
constexpr std::size_t kMaxExtraMasks = 1;

/** @brief Reject `message` the way every parse-time failure is reported. */
[[noreturn]] void Reject(const std::string& message) {
    throw std::runtime_error("avatar catalogue: " + message);
}

const std::vector<std::string>& AvatarSlots() {
    static const std::vector<std::string> kSlots = {
        "shape", "eyes", "mouth", "head_top", "mask", "bodywear", "set", "motif"};
    return kSlots;
}

/** @brief Part slots whose "still" assets need a declared socket. */
const std::vector<std::string>& PartSlots() {
    static const std::vector<std::string> kSlots = {
        "eyes", "mouth", "head_top", "mask", "bodywear"};
    return kSlots;
}

/** @brief Slots every catalogue must cover with a free item. */
const std::vector<std::string>& StarterSlots() {
    static const std::vector<std::string> kSlots = {"shape", "eyes", "mouth"};
    return kSlots;
}

const std::vector<std::string>& Sockets() {
    static const std::vector<std::string> kSockets = {
        "eyes", "mouth", "head_top", "body_center"};
    return kSockets;
}

const std::vector<std::string>& Rarities() {
    static const std::vector<std::string> kRarities = {
        "common", "uncommon", "rare", "epic"};
    return kRarities;
}

bool Contains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

/** @brief `<namespace>:<name>`, both parts non-empty (`uni:eyes_dot`). */
bool IsNamespacedId(const std::string& id) {
    const std::size_t colon = id.find(':');
    return colon != std::string::npos && colon > 0 && colon + 1 < id.size();
}

/** @brief `#rrggbb`, case-insensitive digits. */
bool IsHexColor(const std::string& hex) {
    if (hex.size() != 7 || hex.front() != '#') return false;
    for (std::size_t i = 1; i < hex.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(hex[i]))) return false;
    }
    return true;
}

const json& RequireField(const json& node, const char* key,
                         const std::string& where) {
    const auto it = node.find(key);
    if (it == node.end() || it->is_null()) {
        Reject(where + " is missing '" + key + "'");
    }
    return *it;
}

std::string StringField(const json& node, const char* key,
                        const std::string& where) {
    const json& value = RequireField(node, key, where);
    if (!value.is_string()) Reject(where + " '" + key + "' must be a string");
    return value.get<std::string>();
}

std::string OptionalStringField(const json& node, const char* key,
                                const std::string& where) {
    const auto it = node.find(key);
    if (it == node.end() || it->is_null()) return std::string();
    if (!it->is_string()) Reject(where + " '" + key + "' must be a string");
    return it->get<std::string>();
}

bool BoolField(const json& node, const char* key, const std::string& where) {
    const auto it = node.find(key);
    if (it == node.end() || it->is_null()) return false;
    if (!it->is_boolean()) Reject(where + " '" + key + "' must be a boolean");
    return it->get<bool>();
}

/** @brief Quantity fields: an integer that is not negative. */
int NonNegativeIntField(const json& node, const char* key,
                        const std::string& where) {
    const json& value = RequireField(node, key, where);
    if (!value.is_number_integer()) {
        Reject(where + " '" + key + "' must be an integer");
    }
    if (value.get<std::int64_t>() < 0) {
        Reject(where + " '" + key + "' must not be negative");
    }
    return value.get<int>();
}

/** @brief Validates `asset` and returns its `kind` ("strip" or "still"). */
std::string ParseAsset(const json& asset, const std::string& where) {
    if (!asset.is_object()) Reject(where + ".asset must be an object");
    const std::string kind = StringField(asset, "kind", where + ".asset");
    (void)StringField(asset, "path", where + ".asset");
    if (kind == "strip") return kind;
    if (kind != "still") {
        Reject(where + ".asset kind '" + kind + "' must be strip or still");
    }
    const json& bob = RequireField(asset, "bob", where + ".asset");
    if (!bob.is_array() || bob.size() != 5) {
        Reject(where + ".asset 'bob' must hold exactly 5 [dx, dy] frames");
    }
    for (const json& frame : bob) {
        if (!frame.is_array() || frame.size() != 2 || !frame[0].is_number()
            || !frame[1].is_number()) {
            Reject(where + ".asset 'bob' entries must be [dx, dy] pairs");
        }
    }
    return kind;
}

/**
 * @brief The number of extra mask files in an asset's optional `masks` list.
 *
 * Rejects a `masks` value that is not an array of strings, or one that lists
 * more than `kMaxExtraMasks` files. Does not check that the files exist.
 */
std::size_t ParseMasks(const json& asset, const std::string& where) {
    const auto masks = asset.find("masks");
    if (masks == asset.end() || masks->is_null()) return 0;
    if (!masks->is_array()) {
        Reject(where + ".asset 'masks' must be an array");
    }
    if (masks->size() > kMaxExtraMasks) {
        Reject(where + ".asset 'masks' holds at most "
               + std::to_string(kMaxExtraMasks) + " file");
    }
    for (const json& mask : *masks) {
        if (!mask.is_string()) {
            Reject(where + ".asset 'masks' must hold strings");
        }
    }
    return masks->size();
}

/** @brief Every shape must name all four sockets with an [x, y] offset. */
void ParseShapeSockets(const json& doc) {
    const json& tables = RequireField(doc, "shape_sockets", "catalogue");
    if (!tables.is_object()) Reject("'shape_sockets' must be an object");
    for (const auto& [shape_id, table] : tables.items()) {
        if (!table.is_object()) {
            Reject("shape_sockets." + shape_id + " must be an object");
        }
        for (const std::string& socket : Sockets()) {
            const json& offset =
                RequireField(table, socket.c_str(), "shape_sockets." + shape_id);
            if (!offset.is_array() || offset.size() != 2 || !offset[0].is_number()
                || !offset[1].is_number()) {
                Reject("shape_sockets." + shape_id + "." + socket
                       + " must be [x, y]");
            }
        }
    }
}

std::vector<Palette> ParsePalettes(const json& doc) {
    const json& palettes = RequireField(doc, "palettes", "catalogue");
    if (!palettes.is_array()) Reject("'palettes' must be an array");

    std::unordered_set<std::string> seen_swatches;
    std::vector<Palette> parsed_palettes;
    for (std::size_t i = 0; i < palettes.size(); ++i) {
        const json& node = palettes[i];
        const std::string where = "palettes[" + std::to_string(i) + "]";
        if (!node.is_object()) Reject(where + " must be an object");

        Palette palette;
        palette.id = StringField(node, "id", where);
        (void)StringField(node, "name", where);
        palette.price = NonNegativeIntField(node, "price", where);
        palette.starter = BoolField(node, "starter", where);

        const json& swatches = RequireField(node, "swatches", where);
        if (!swatches.is_array()) Reject(where + ".swatches must be an array");
        for (std::size_t j = 0; j < swatches.size(); ++j) {
            const json& node_swatch = swatches[j];
            const std::string swatch_where =
                where + ".swatches[" + std::to_string(j) + "]";
            if (!node_swatch.is_object()) {
                Reject(swatch_where + " must be an object");
            }
            Swatch swatch;
            swatch.id = StringField(node_swatch, "id", swatch_where);
            swatch.hex = StringField(node_swatch, "hex", swatch_where);
            if (!IsHexColor(swatch.hex)) {
                Reject(swatch_where + ".hex '" + swatch.hex + "' must be #rrggbb");
            }
            const auto price_node = node_swatch.find("price");
            if (price_node != node_swatch.end() && !price_node->is_null()) {
                if (!price_node->is_number_integer()) {
                    Reject(swatch_where + " 'price' must be an integer");
                }
                if (price_node->get<std::int64_t>() < 0) {
                    Reject(swatch_where + " 'price' must not be negative");
                }
                swatch.price = price_node->get<int>();
            }
            swatch.rarity = "common";
            const auto rarity_node = node_swatch.find("rarity");
            if (rarity_node != node_swatch.end() && !rarity_node->is_null()) {
                if (!rarity_node->is_string()) {
                    Reject(swatch_where + " 'rarity' must be a string");
                }
                if (!Contains(Rarities(), rarity_node->get<std::string>())) {
                    Reject(swatch_where + " 'rarity' is not a rarity");
                }
                swatch.rarity = rarity_node->get<std::string>();
            }
            swatch.hidden = BoolField(node_swatch, "hidden", swatch_where);
            swatch.unlisted = BoolField(node_swatch, "unlisted", swatch_where);
            if (!seen_swatches.insert(swatch.id).second) {
                Reject("duplicate swatch id '" + swatch.id + "'");
            }
            palette.swatches.push_back(std::move(swatch));
        }
        parsed_palettes.push_back(std::move(palette));
    }
    return parsed_palettes;
}

/**
 * @brief The optional `default_colors` list of an item node.
 *
 * Entry N is the default of the N-th stored color. A shape takes none (its
 * color is the skin); a motif stores one color less than it has regions.
 * Whether each entry is a hex or a known swatch is checked later, once the
 * palettes are parsed (`ValidateDefaultColours`).
 */
std::vector<std::string> ParseDefaultColours(const json& node, const Item& item,
                                             const std::string& where) {
    std::vector<std::string> parsed;
    const auto colours = node.find("default_colors");
    if (colours == node.end() || colours->is_null()) return parsed;
    if (!colours->is_array()) Reject(where + ".default_colors must be an array");

    int limit = item.regions;
    if (item.slot == "shape") limit = 0;
    if (item.slot == "motif") limit = std::max(0, item.regions - 1);
    if (item.slot == "shape"
        || static_cast<int>(colours->size()) > limit) {
        Reject(where + ".default_colors holds at most "
               + std::to_string(limit) + " entries");
    }
    for (const json& entry : *colours) {
        if (!entry.is_string()) {
            Reject(where + ".default_colors must hold strings");
        }
        parsed.push_back(entry.get<std::string>());
    }
    return parsed;
}

/** @brief Every default color is a `#rrggbb` hex or a palette swatch id. */
void ValidateDefaultColours(const std::vector<Item>& items,
                            const std::vector<Palette>& palettes) {
    std::unordered_set<std::string> swatch_ids;
    for (const Palette& palette : palettes) {
        for (const Swatch& swatch : palette.swatches) {
            swatch_ids.insert(swatch.id);
        }
    }
    for (const Item& item : items) {
        for (const std::string& entry : item.default_colors) {
            if (!IsHexColor(entry) && !swatch_ids.contains(entry)) {
                Reject("item '" + item.id + "' default color '" + entry
                       + "' is neither #rrggbb nor a swatch id");
            }
        }
    }
}

std::vector<Item> ParseItems(const json& doc) {
    const json& items = RequireField(doc, "items", "catalogue");
    if (!items.is_array()) Reject("'items' must be an array");

    std::unordered_set<std::string> seen_ids;
    std::unordered_set<std::string> starter_slots;
    std::vector<Item> parsed_items;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const json& node = items[i];
        const std::string where = "items[" + std::to_string(i) + "]";
        if (!node.is_object()) Reject(where + " must be an object");

        Item item;
        item.id = StringField(node, "id", where);
        if (!IsNamespacedId(item.id)) {
            Reject(where + ".id '" + item.id
                   + "' must be namespaced, e.g. uni:eyes_dot");
        }
        if (!seen_ids.insert(item.id).second) {
            Reject("duplicate item id '" + item.id + "'");
        }

        item.slot = StringField(node, "slot", where);
        if (!Contains(AvatarSlots(), item.slot)) {
            Reject(where + ".slot '" + item.slot + "' is not a slot");
        }
        (void)StringField(node, "name", where);

        const json& asset = RequireField(node, "asset", where);
        const std::string kind = ParseAsset(asset, where);
        const std::size_t extra_masks = ParseMasks(asset, where);

        const json& regions = RequireField(node, "regions", where);
        if (!regions.is_number_integer()) {
            Reject(where + ".regions must be an integer");
        }
        item.regions = regions.get<int>();
        if (item.regions < 0 || item.regions > kMaxRegions) {
            Reject(where + ".regions must be 0.." + std::to_string(kMaxRegions));
        }
        const int region_capacity =
            kRegionsPerFile * (1 + static_cast<int>(extra_masks));
        if (item.regions > region_capacity) {
            Reject(where + ".regions " + std::to_string(item.regions)
                   + " needs more 'masks' files (capacity "
                   + std::to_string(region_capacity) + ")");
        }

        const auto tint_node = node.find("tint");
        if (tint_node != node.end() && !tint_node->is_null()) {
            if (!tint_node->is_string()) {
                Reject(where + " 'tint' must be a string");
            }
            item.tint = tint_node->get<std::string>();
        }
        if (item.tint != "mask" && item.tint != "multiply") {
            Reject(where + ".tint '" + item.tint + "' must be mask or multiply");
        }
        if (item.tint == "multiply") {
            if (item.regions != 1) {
                Reject(where + ".tint multiply needs exactly 1 region");
            }
            if (extra_masks != 0) {
                Reject(where + ".tint multiply takes no 'masks' files");
            }
        }
        item.default_colors = ParseDefaultColours(node, item, where);

        const std::string rarity = StringField(node, "rarity", where);
        if (!Contains(Rarities(), rarity)) {
            Reject(where + ".rarity '" + rarity + "' is not a rarity");
        }
        item.rarity = rarity;
        item.price = NonNegativeIntField(node, "price", where);

        const auto socket_node = node.find("socket");
        const bool has_socket = socket_node != node.end()
                                && !socket_node->is_null();
        if (has_socket) {
            if (!socket_node->is_string()) {
                Reject(where + " 'socket' must be a string");
            }
            const std::string socket = socket_node->get<std::string>();
            if (!Contains(Sockets(), socket)) {
                Reject(where + ".socket '" + socket + "' is not a socket");
            }
        }
        if (kind == "still" && Contains(PartSlots(), item.slot) && !has_socket) {
            Reject("still item '" + item.id + "' needs a socket");
        }

        const auto shapes = node.find("shapes");
        if (shapes != node.end() && !shapes->is_null()) {
            if (!shapes->is_array()) Reject(where + ".shapes must be an array");
            for (const json& shape : *shapes) {
                if (!shape.is_string()) {
                    Reject(where + ".shapes must hold strings");
                }
                item.shapes.push_back(shape.get<std::string>());
            }
        }

        item.starter = BoolField(node, "starter", where);
        item.hidden = BoolField(node, "hidden", where);
        item.unlisted = BoolField(node, "unlisted", where);
        item.available_from = OptionalStringField(node, "available_from", where);
        item.available_until = OptionalStringField(node, "available_until", where);
        if (!item.available_from.empty() && !item.available_until.empty()
            && item.available_until < item.available_from) {
            Reject("item '" + item.id
                   + "' available_until is before available_from");
        }

        if (item.starter) starter_slots.insert(item.slot);
        parsed_items.push_back(std::move(item));
    }

    for (const std::string& slot : StarterSlots()) {
        if (!starter_slots.contains(slot)) {
            Reject("slot '" + slot + "' needs a starter item");
        }
    }
    return parsed_items;
}

std::vector<std::string> ParseFeatured(
    const json& doc, const std::unordered_set<std::string>& item_ids) {
    const json& featured = RequireField(doc, "featured", "catalogue");
    if (!featured.is_array()) Reject("'featured' must be an array");

    std::vector<std::string> parsed;
    for (std::size_t i = 0; i < featured.size(); ++i) {
        if (!featured[i].is_string()) {
            Reject("featured[" + std::to_string(i) + "] must be a string");
        }
        const std::string id = featured[i].get<std::string>();
        if (!item_ids.contains(id)) {
            Reject("featured id '" + id + "' is not an item");
        }
        parsed.push_back(id);
    }
    return parsed;
}

/** @brief The process-wide catalogue plus whether `LoadGlobal` filled it. */
struct GlobalStorage {
    Catalogue catalogue;
    bool loaded = false;
};

GlobalStorage& Global() {
    static GlobalStorage storage;
    return storage;
}

/**
 * @brief `now_unix` as `YYYY-MM-DDTHH:MM:SSZ`, the form the catalogue uses.
 *
 * The date window is compared as text, the same way parse-time validation
 * rejects an inverted window, so both sides share one format.
 */
std::string ToIso8601Utc(std::int64_t now_unix) {
    const std::time_t seconds = static_cast<std::time_t>(now_unix);
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    char buffer[21];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

}  // namespace

Catalogue Catalogue::ParseDoc(const json& doc) {
    if (!doc.is_object()) Reject("document must be an object");

    const json& version = RequireField(doc, "version", "catalogue");
    if (!version.is_number_integer() || version.get<int>() != 1) {
        Reject("'version' must be 1");
    }
    ParseShapeSockets(doc);

    Catalogue catalogue;
    catalogue.items_ = ParseItems(doc);
    catalogue.palettes_ = ParsePalettes(doc);
    std::unordered_set<std::string> catalogue_ids;
    for (const Item& item : catalogue.items_) catalogue_ids.insert(item.id);
    for (const Palette& palette : catalogue.palettes_) {
        if (!catalogue_ids.insert(palette.id).second) {
            Reject("duplicate catalogue id '" + palette.id + "'");
        }
        for (const Swatch& swatch : palette.swatches) {
            if (!catalogue_ids.insert(swatch.id).second) {
                Reject("duplicate catalogue id '" + swatch.id + "'");
            }
        }
    }
    ValidateDefaultColours(catalogue.items_, catalogue.palettes_);

    std::unordered_set<std::string> item_ids;
    for (const Item& item : catalogue.items_) item_ids.insert(item.id);
    catalogue.featured_ = ParseFeatured(doc, item_ids);
    return catalogue;
}

Catalogue Catalogue::Parse(const nlohmann::json& doc) {
    try {
        return ParseDoc(doc);
    } catch (const json::exception& error) {
        Reject(std::string("malformed document: ") + error.what());
    }
}

Catalogue Catalogue::LoadFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("avatar catalogue: cannot open '" + path + "'");
    }
    try {
        return Parse(json::parse(input));
    } catch (const json::exception& error) {
        throw std::runtime_error("avatar catalogue: '" + path
                                 + "': " + error.what());
    }
}

const Catalogue& Catalogue::Instance() {
    if (!Global().loaded) {
        throw std::runtime_error(
            "avatar catalogue: Instance() called before LoadGlobal()");
    }
    return Global().catalogue;
}

void Catalogue::LoadGlobal(const std::string& path) {
    const std::string resolved =
        path.empty() ? Env::Get("UNI_AVATAR_CATALOGUE", kDefaultPath) : path;
    try {
        Global().catalogue = LoadFile(resolved);
        Global().loaded = true;
    } catch (const std::exception& error) {
        Logger::Error(std::string("[Avatar] catalogue load failed: ")
                      + error.what());
        throw;
    }
    Logger::Info("[Avatar] catalogue loaded from '" + resolved + "'");
}

const Item* Catalogue::FindItem(const std::string& id) const {
    for (const Item& item : items_) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

bool Catalogue::IsStarter(const std::string& id) const {
    const Item* item = FindItem(id);
    return item != nullptr && item->starter;
}

bool Catalogue::IsStarterSwatch(const std::string& swatch_id) const {
    for (const Palette& palette : palettes_) {
        if (!palette.starter) continue;
        for (const Swatch& swatch : palette.swatches) {
            if (swatch.id == swatch_id) return true;
        }
    }
    return false;
}

bool Catalogue::HasSwatch(const std::string& swatch_id) const {
    for (const Palette& palette : palettes_) {
        for (const Swatch& swatch : palette.swatches) {
            if (swatch.id == swatch_id) return true;
        }
    }
    return false;
}

bool Catalogue::IsBuyable(const std::string& item_id,
                          std::int64_t now_unix) const {
    const Item* item = FindItem(item_id);
    if (item == nullptr) {
        for (const Palette& palette : palettes_) {
            for (const Swatch& swatch : palette.swatches) {
                if (swatch.id != item_id) continue;
                return !palette.starter && !swatch.hidden && !swatch.unlisted
                       && swatch.price.has_value();
            }
        }
        return false;
    }
    if (item->starter || item->hidden || item->unlisted) return false;
    const std::string now = ToIso8601Utc(now_unix);
    if (!item->available_from.empty() && now < item->available_from) {
        return false;
    }
    if (!item->available_until.empty() && now > item->available_until) {
        return false;
    }
    return true;
}

std::optional<int> Catalogue::PriceOf(const std::string& id) const {
    const Item* item = FindItem(id);
    if (item != nullptr) return item->price;
    for (const Palette& palette : palettes_) {
        if (palette.id == id) return palette.price;
        for (const Swatch& swatch : palette.swatches) {
            if (swatch.id == id) return swatch.price;
        }
    }
    return std::nullopt;
}

std::vector<std::string> Catalogue::StarterSwatchIds() const {
    std::vector<std::string> ids;
    for (const Palette& palette : palettes_) {
        if (!palette.starter) continue;
        for (const Swatch& swatch : palette.swatches) ids.push_back(swatch.id);
    }
    return ids;
}

const Item* Catalogue::SlotDefault(const std::string& slot) const {
    for (const Item& item : items_) {
        if (item.starter && item.slot == slot) return &item;
    }
    return nullptr;
}

}  // namespace uni::avatar
