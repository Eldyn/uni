#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file catalogue.hpp
 * @brief The avatar catalogue: the repo data behind every cosmetic item.
 *
 * The catalogue is the folder `avatar/catalogue/`, overridable with the
 * `UNI_AVATAR_CATALOGUE` env var (which names a folder). It is the single
 * source the client and the server both read. The server keeps the parsed form
 * in memory for the whole process (`LoadGlobal`), and answers slot-default and
 * starter questions from it. Validation mirrors the client schema
 * (`frontend/src/lib/avatar/catalogueSchema.ts`), so a tree the client rejects
 * never reaches a running server either.
 */

namespace uni::avatar {

/**
 * @brief One catalogue item, restricted to the fields the server reasons about.
 *
 * `available_from` / `available_until` are empty when the entry has no window
 * (ISO-8601 UTC strings when set). `shapes` is empty when the item supports
 * every shape. `tint` is `mask` or `multiply`; `default_colors` is empty when
 * the item has none.
 */
struct Item {
    std::string id;
    std::string slot;
    int regions = 0;
    std::string tint = "mask";
    std::vector<std::string> default_colors;
    std::string rarity;
    int price = 0;
    bool starter = false;
    bool hidden = false;
    bool unlisted = false;
    std::string available_from;
    std::string available_until;
    std::vector<std::string> shapes;
};

/** @brief One color a player can pick: a namespaced id and a `#rrggbb` hex. */
struct Swatch {
    std::string id;
    std::string hex;
    std::optional<int> price;
    std::string rarity = "common";
    bool hidden = false;
    bool unlisted = false;
};

/** @brief A named set of swatches, bought whole (or free when `starter`). */
struct Palette {
    std::string id;
    std::vector<Swatch> swatches;
    int price = 0;
    bool starter = false;
};

/**
 * @brief The parsed catalogue, with the lookups the server needs.
 *
 * Parse-time failures throw `std::runtime_error` with a message naming the
 * offending node. Lookups never throw: an unknown id or slot yields `nullptr`
 * (or `false`), so a removed or unknown item resolves to the slot default
 * instead of breaking a render or a save.
 */
class Catalogue {
  public:
    /**
     * @brief Validate a parsed JSON document into a catalogue.
     * @throws std::runtime_error when any rule fails.
     */
    static Catalogue Parse(const nlohmann::json& doc);

    /**
     * @brief Merge the per-element tree at `directory` into one catalogue
     *        document (see `avatar/README.md` for the nine merge rules).
     *
     * Walks every `*.json` file in sorted path order. Throws
     * `std::runtime_error` on a stray file, a stem that does not match its id,
     * a folder that does not match the slot, a duplicate id or order, or a
     * missing `meta.json`. The output has the same shape that `Parse` reads.
     */
    static nlohmann::json MergeDirectory(const std::string& directory);

    /** @brief Merge the tree at `directory`, then parse and validate it. */
    static Catalogue LoadDirectory(const std::string& directory);

    /** @brief The process-wide catalogue; only valid after `LoadGlobal`. */
    static const Catalogue& Instance();

    /**
     * @brief Load the process-wide catalogue, aborting start-up on failure.
     *
     * An empty `path` reads `UNI_AVATAR_CATALOGUE` (default
     * `avatar/catalogue`). The error is logged before it is rethrown, so
     * the caller lets it abort the boot with a clear message.
     */
    static void LoadGlobal(const std::string& path = "");

    /** @brief The item with `id`, or `nullptr` when it is not in the catalogue. */
    const Item* FindItem(const std::string& id) const;

    /** @brief True when `id` exists and is owned by every account. */
    bool IsStarter(const std::string& id) const;

    /** @brief True when `swatch_id` belongs to a `starter` palette. */
    bool IsStarterSwatch(const std::string& swatch_id) const;

    /** @brief True when `swatch_id` exists in any palette. */
    bool HasSwatch(const std::string& swatch_id) const;

    /**
     * @brief True when `item_id` can be bought at `now_unix`.
     *
     * False for starters, hidden or unlisted items, items outside their
     * `available_from` / `available_until` window, and unknown ids.
     */
    bool IsBuyable(const std::string& item_id, std::int64_t now_unix) const;

    /**
     * @brief The catalogue price of an item or a palette.
     *
     * `std::nullopt` when `id` is neither, so a removed entry has no price.
     */
    std::optional<int> PriceOf(const std::string& id) const;

    /**
     * @brief Ids of every swatch in a `starter` palette, in catalogue order.
     *
     * Used to build the default look (the first id) and the guest default
     * (`seed % size`).
     */
    std::vector<std::string> StarterSwatchIds() const;

    /**
     * @brief The first starter item in `slot`, or `nullptr` when the slot is
     *        optional (or unknown).
     */
    const Item* SlotDefault(const std::string& slot) const;

  private:
    /** @brief `Parse` without the JSON-exception normalisation wrapper. */
    static Catalogue ParseDoc(const nlohmann::json& doc);

    std::vector<Item> items_;
    std::vector<Palette> palettes_;
    std::vector<std::string> featured_;
};

}  // namespace uni::avatar
