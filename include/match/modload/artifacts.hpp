#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file artifacts.hpp
 * @brief In-memory representation of everything a mod folder declares.
 *
 * The ModLoader parses folders into this model and nothing else. The semantic
 * validator consumes it to run reference resolution, arity checks, graph
 * structure checks and mutation conflict detection WITHOUT re-reading JSON.
 *
 * Design rule for this file: keep it data-only and lossless. Behavior graphs
 * are preserved as raw JSON (nodes + args) rather than interpreted, because
 * the op vocabulary and condition schema are owned by the validator/engine,
 * not the loader.
 */

namespace match::modload {

/**
 * @struct LoadError
 * @brief Structured loader/semantic error in the shape.
 */
struct LoadError {
    std::string check;      /**< Short machine-readable check id. */
    std::string artifact;   /**< Artifact type, e.g. "mod", "cards", "deck". */
    std::string path;       /**< Filesystem path the error refers to. */
    std::string message;    /**< Human-readable explanation. */
};

/**
 * @enum FaceKind
 * @brief Phase-1 declarative face kinds.
 */
enum class FaceKind {
    kText,
    kImage,
    kEmoji,
    kBlank,
};

/** Serialize a FaceKind to its wire token. */
std::string ToString(FaceKind kind);

/** Parse a face-kind token; nullopt when the token is not a phase-1 kind. */
std::optional<FaceKind> FaceKindFromString(const std::string& token);

/**
 * @struct FaceSpec
 * @brief Declarative card face; `art_version` feeds the client face hash.
 */
struct FaceSpec {
    FaceKind kind = FaceKind::kBlank;
    std::optional<std::string> color;   /**< text kind. */
    std::optional<std::string> label;   /**< text / emoji kinds. */
    std::optional<std::string> art;     /**< image kind: asset bundle id. */
    std::string art_mode = "inset";     /**< inset | replace | overlay. */
    std::string art_fit = "contain";    /**< contain | cover | stretch. */
    std::vector<std::string> keep;      /**< Layers drawn over the art. */
    int art_version = 1;
};

/**
 * @struct BehaviorGraph
 * @brief A flat node list kept as raw JSON.
 *
 * Nodes carry `id`, `op`/`window`/`branch`/`fork`/`schedule`, `args`, and
 * routing keys. Routing shape is validator-owned; the loader only requires
 * the graph to be an object with a `nodes` array.
 */
struct BehaviorGraph {
    nlohmann::json raw;                 /**< Whole graph object, verbatim. */
    nlohmann::json nodes = nlohmann::json::array();
};

/**
 * @struct BehaviorEntry
 * @brief One named trigger entry of a card or rule hook.
 */
struct BehaviorEntry {
    std::string hook;                   /**< Hook name, e.g. "on_play". */
    std::optional<std::string> phase;   /**< "before" | "after". */
    std::optional<nlohmann::json> where; /**< Condition filter. */
    BehaviorGraph graph;
};

/**
 * @struct WindowSpec
 * @brief A card's `window.when_played` declaration.
 */
struct WindowSpec {
    std::string responders;                     /**< Selector, "@others". */
    std::optional<nlohmann::json> respond_with; /**< Eligibility filter. */
    std::string duration;                       /**< "env" | duration token. */
    std::string on_response;                    /**< Response route. */
    std::string default_route;                  /**< Default route. */
    nlohmann::json raw;                         /**< Verbatim declaration. */
};

/**
 * @struct AutoTriggerDef
 * @brief A card's `auto_trigger` declaration.
 *
 * Mirrors the ECS `ecs::AutoTrigger` component: a condition plus the graph
 * to run when it matches. `must_apply: true` fires the graph without player
 * agency. Kept as raw JSON like the other graph carriers; `graph`
 * holds the whole object (its `nodes` array is validator-read).
 */
struct AutoTriggerDef {
    nlohmann::json condition;  /**< Condition object. */
    nlohmann::json graph;      /**< Behavior graph object. */
    bool must_apply = false;   /**< true = fires without player agency. */
};

/**
 * @struct CardDef
 * @brief One entry of a mod's `cards.json`.
 */
struct CardDef {
    std::string id;                     /**< Local id `[a-z0-9_]+`. */
    std::string namespace_id;           /**< Owning mod id. */
    std::string kind_id;                /**< Full `namespace:id`. */
    std::string title;
    FaceSpec face;
    std::vector<std::string> tags;
    std::vector<BehaviorEntry> behaviors;
    std::optional<WindowSpec> window;
    std::optional<AutoTriggerDef> auto_trigger; /**< Must-apply. */
    nlohmann::json raw;                 /**< Verbatim definition. */
};

/**
 * @struct StatusDef
 * @brief A status declaration from `rules.json` `"statuses"`.
 */
struct StatusDef {
    std::string id;
    std::string namespace_id;
    std::string status_id;              /**< Full `namespace:id`. */
    std::string title;
    std::string stack_policy;           /**< replace|accumulate|... */
    bool hidden = false;
    nlohmann::json raw;
};

/**
 * @struct RuleDef
 * @brief One entry of a mod's `rules.json`.
 */
struct RuleDef {
    std::string id;
    std::string namespace_id;
    std::string rule_id;                /**< Full `namespace:id`. */
    std::string title;
    std::string description;
    std::vector<BehaviorEntry> hooks;
    nlohmann::json raw;
};

/**
 * @struct MutationDef
 * @brief One entry of a mod's `mutations.json`.
 */
struct MutationDef {
    std::string id;
    std::string namespace_id;
    std::string mutation_id;            /**< Full `namespace:id`. */
    std::string target;                 /**< Referenced kind/hook id. */
    std::string mode;                   /**< replace|wrap|veto|filter. */
    std::optional<std::string> position; /**< wrap: "before" | "after". */
    std::optional<nlohmann::json> where; /**< veto: deny condition. */
    BehaviorGraph replacement;
    nlohmann::json raw;
};

/**
 * @struct DeckDef
 * @brief A lobby-settings snapshot from `decks/*.json`.
 */
struct DeckDef {
    std::string id;
    std::string namespace_id;
    std::string deck_id;                /**< Full `namespace:id`. */
    std::string name;
    std::vector<std::string> mods;      /**< Mod list this deck activates. */
    std::vector<std::pair<std::string, int>> cards; /**< Kind -> count. */
    nlohmann::json settings = nlohmann::json::object(); /**< Typed bag. */
    nlohmann::json raw;
};

/**
 * @enum AssetTier
 * @brief Quality tier of an asset variant; richest allowed wins.
 */
enum class AssetTier {
    kHigh,
    kMedium,
    kLow,
};

/** Serialize an AssetTier to its wire token (`high`/`medium`/`low`). */
std::string ToString(AssetTier tier);

/** Parse an asset-tier token; nullopt when the token is not a known tier. */
std::optional<AssetTier> AssetTierFromString(const std::string& token);

/**
 * @struct AssetVariant
 * @brief One tier of one asset slot.
 *
 * A file variant carries `file` (path relative to the bundle folder); a glyph
 * variant carries `value`. Exactly one is set (validator-enforced).
 */
struct AssetVariant {
    AssetTier tier = AssetTier::kLow;
    std::optional<std::string> file;   /**< File variant. */
    std::optional<std::string> value;  /**< Glyph variant. */
    nlohmann::json raw;
};

/**
 * @struct AssetSlot
 * @brief One named slot of an asset bundle.
 */
struct AssetSlot {
    std::string name;
    std::vector<AssetVariant> variants;
};

/**
 * @struct AssetBundleDef
 * @brief One `assets/<bundle>/index.json`.
 *
 * The folder name is irrelevant; `id` is authoritative. `card` optionally
 * binds the bundle to a `cards/<id>.json` entry for cross-validation.
 */
struct AssetBundleDef {
    std::string id;
    std::string namespace_id;
    std::string bundle_id;              /**< Full `namespace:id`. */
    std::optional<std::string> card;    /**< Optional card binding. */
    std::string license;
    std::string author;
    std::vector<AssetSlot> slots;       /**< Sorted by slot name. */
    std::string folder;                 /**< Bundle folder path on disk. */
    nlohmann::json raw;
};

/**
 * @struct SettingDecl
 * @brief A named setting declared by a mod manifest.
 */
struct SettingDecl {
    std::string id;
    std::string type;                   /**< int|number|bool|string. */
    nlohmann::json default_value;
    std::optional<nlohmann::json> range;
    /**< When true the loader omits this mod from production builds. */
    bool dev_only = false;
    std::string description;
    nlohmann::json raw;
};

/**
 * @struct ModManifest
 * @brief Parsed `mod.json`.
 */
struct ModManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string api;
    std::string description;
    std::string author;
    std::vector<SettingDecl> settings;
    nlohmann::json prompts = nlohmann::json::array();
    nlohmann::json signals = nlohmann::json::array();
    nlohmann::json raw;
};

/**
 * @struct LoadWarning
 * @brief A non-blocking content warning in the shape.
 *
 * Same fields as LoadError; kept distinct so the verification log can separate
 * "this mod did not load" from "this mod loaded with caveats".
 */
struct LoadWarning {
    std::string check;
    std::string artifact;
    std::string path;
    std::string message;
};

/**
 * @struct ModReport
 * @brief Per-mod verification report.
 *
 * One report exists for every mod folder the scan discovered, whether or not
 * it loaded. `ok` is false when any error blocked the mod.
 */
struct ModReport {
    std::string mod_id;
    std::string folder;
    bool ok = false;
    std::vector<LoadError> errors;
    std::vector<LoadWarning> warnings;
    std::size_t asset_count = 0;        /**< File variants declared. */
    std::uintmax_t asset_bytes = 0;     /**< Sum of those files' sizes. */
};

/**
 * @struct LoadedMod
 * @brief One fully parsed mod folder.
 */
struct LoadedMod {
    std::string folder;                 /**< Folder name on disk. */
    std::string path;                   /**< Absolute/relative folder path. */
    ModManifest manifest;
    std::vector<CardDef> cards;
    std::vector<RuleDef> rules;
    std::vector<StatusDef> statuses;
    std::vector<MutationDef> mutations;
    std::vector<DeckDef> decks;
    std::vector<AssetBundleDef> assets;
};

/**
 * @struct LoadResult
 * @brief Aggregate result of a folder scan.
 *
 * Per-mod isolation: `mods` holds only the mods that validated, while
 * `reports` carries one `ModReport` per discovered folder (loaded or not) so
 * the verification log can explain every failure. `errors`/`warnings` are flat
 * aggregates across all mods for callers that only need counts. A scan-level
 * failure (an unreadable root) sets `fatal_scan_error` and leaves `mods` empty.
 */
struct LoadResult {
    std::vector<LoadedMod> mods;
    std::vector<LoadError> errors;
    std::vector<LoadWarning> warnings;
    std::vector<ModReport> reports;
    bool fatal_scan_error = false;

    bool ok() const { return errors.empty(); }
    bool fatal() const { return fatal_scan_error; }
};

}  // namespace match::modload
