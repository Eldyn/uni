#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
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
    std::string label_key;              /**< Empty: derived by the glossary. */
    std::string description_key;        /**< Empty: derived by the glossary. */
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
 * @struct TableEntry
 * @brief One weighted entry of an outcome table.
 *
 * `value` and `roll` are mutually exclusive (validator-enforced); a nested
 * `roll` is a raw roll spec whose total becomes the outcome value.
 * `where` is a raw 10.2 condition object. All three raw payloads stay verbatim
 * like the behavior graphs, because the op/condition vocabulary is owned by the
 * validator/engine, not the loader.
 */
struct TableEntry {
    std::string key;                     /**< Local token `[a-z0-9_]{1,32}`. */
    int64_t weight = 1;                  /**< Integer >= 1. */
    std::optional<int64_t> value;        /**< Outcome value (no `roll`). */
    std::optional<nlohmann::json> roll;  /**< Nested roll spec. */
    std::optional<nlohmann::json> where; /**< 10.2 condition filter. */
    nlohmann::json raw;                  /**< Verbatim entry object. */
};

/**
 * @struct TableDef
 * @brief One `tables/<id>.json` outcome table.
 */
struct TableDef {
    std::string id;                      /**< Local id `[a-z0-9_]+`. */
    std::string namespace_id;            /**< Owning mod id. */
    std::string table_id;                /**< Full `namespace:id`. */
    std::vector<TableEntry> entries;     /**< Sorted by key (deterministic). */
    nlohmann::json raw;                  /**< Verbatim file object. */
};

/**
 * @struct ComponentDef
 * @brief One component of an entity definition.
 *
 * `name` is a namespaced component name; `params` is the component's raw
 * parameter object kept verbatim (the component vocabulary and its families are
 * owned by the validator/engine, not the loader).
 */
struct ComponentDef {
    std::string name;             /**< Namespaced component name. */
    nlohmann::json params;        /**< Verbatim parameter object. */
};

/**
 * @struct EntityDef
 * @brief One `entities/<id>.json` entity type.
 */
struct EntityDef {
    std::string id;                       /**< Local id `[a-z0-9_]+`. */
    std::string namespace_id;             /**< Owning mod id. */
    std::string entity_id;                /**< Full `namespace:id`. */
    std::vector<ComponentDef> components; /**< Components, name-sorted. */
    nlohmann::json raw;                   /**< Verbatim file object. */
};

/**
 * @struct ModifierDef
 * @brief One modifier of a presentation.
 */
struct ModifierDef {
    std::string kind;             /**< Modifier kind (engine-known). */
    nlohmann::json params;        /**< Verbatim modifier object. */
};

/**
 * @struct TriggerDef
 * @brief One install trigger of a presentation.
 */
struct TriggerDef {
    std::optional<std::string> on;  /**< Source; absent = always-on. */
    nlohmann::json where;           /**< Flat payload-field condition. */
    std::optional<double> duration; /**< Seconds; absent = persistent. */
};

/**
 * @struct PresentationDef
 * @brief One `presentations/<id>.json` presentation.
 */
struct PresentationDef {
    std::string id;                       /**< Local id `[a-z0-9_]+`. */
    std::string namespace_id;             /**< Owning mod id. */
    std::string presentation_id;          /**< Full `namespace:id`. */
    std::string target;                   /**< Target grammar (validated). */
    int64_t priority = 0;                 /**< Merge priority; higher wins. */
    std::vector<ModifierDef> modifiers;   /**< Ordered modifier list. */
    std::vector<TriggerDef> triggers;     /**< Install triggers. */
    nlohmann::json raw;                   /**< Verbatim file object. */
};

/**
 * @struct EventFieldDef
 * @brief One payload field of an event definition.
 */
struct EventFieldDef {
    std::string name;      /**< Wire field name. */
    std::string type;      /**< Closed word, see `event_keys::kFieldTypes`. */
    std::string audience;  /**< Closed word; `none` when the file omits it. */
};

/**
 * @struct EventDef
 * @brief One `events/<name>.json` event definition (format version 1).
 */
struct EventDef {
    std::string id;                      /**< Local id `[a-z0-9_]+`. */
    std::string namespace_id;            /**< Owning mod id. */
    std::string event_id;                /**< Full `namespace:id`. */
    std::string retention;               /**< Closed word; `log` by default. */
    std::vector<EventFieldDef> fields;   /**< Fields, name-sorted. */
    std::vector<std::string> context;    /**< Requested streak names. */
    nlohmann::json raw;                  /**< Verbatim file object. */
};

/**
 * @struct StreakTestDef
 * @brief One test of a streak matcher (closed list, see
 *        `streak_definition_keys.hpp`).
 */
struct StreakTestDef {
    std::string test;    /**< equals | in | exists | has_tag | relation. */
    std::string field;   /**< Payload field name; empty when unused. */
    std::string state;   /**< State value name; empty when unused. */
    nlohmann::json value;                       /**< equals. */
    nlohmann::json values = nlohmann::json::array();  /**< in. */
    std::string tag;       /**< has_tag: full `namespace:name` tag. */
    std::string relation;  /**< relation: `self` or `other`. */
    std::string role;      /**< relation: `actor` or `target`. */
    bool negate = false;
    std::string streak;    /**< Bindings only: streak id to read. */
    std::string key;       /**< Bindings only: literal key into the value. */
    std::string key_field; /**< Bindings only: payload field naming the key. */
    bool actor = false;    /**< Bindings only: read the envelope actor. */
};

/**
 * @struct StreakMatcherDef
 * @brief The client event id and the tests that all must hold.
 */
struct StreakMatcherDef {
    std::string event;                   /**< Client event id. */
    std::vector<StreakTestDef> tests;
};

/**
 * @struct StreakDef
 * @brief One `streaks/<name>.json` streak definition (format version 1).
 */
struct StreakDef {
    std::string id;                      /**< Local id `[a-z0-9_]+`. */
    std::string namespace_id;            /**< Owning mod id. */
    std::string streak_id;               /**< Full `namespace:id`. */
    std::string type;                    /**< run | total | last_value. */
    StreakMatcherDef matcher;
    std::string group_by;                /**< Payload field; may be empty. */
    std::string value_field;             /**< last_value only. */
    std::vector<StreakMatcherDef> reset_on;
    nlohmann::json raw;                  /**< Verbatim file object. */
};

/**
 * @struct DerivedSeatMatcherDef
 * @brief A matcher plus the payload field that names the seat it concerns.
 */
struct DerivedSeatMatcherDef {
    StreakMatcherDef matcher;
    std::string seat_field;              /**< Payload field with a seat. */
};

/**
 * @struct DerivedEventDef
 * @brief One `derived/<name>.json` derived event definition (version 1).
 *
 * Two kinds, told apart by the keys that the file sets:
 *
 * - `window`: `open_on` opens a window for a seat and the engine emits the
 *   derived event; `close_on` closes it.
 * - `block`: `block_on` matches an event that concerns the seat of the open
 *   window `window_of`; the engine emits the derived event.
 */
struct DerivedEventDef {
    std::string id;                      /**< Local id `[a-z0-9_]+`. */
    std::string namespace_id;            /**< Owning mod id. */
    std::string derived_id;              /**< Full `namespace:id`. */
    std::string emits;                   /**< Core client event id. */
    std::string kind;                    /**< window | block. */
    std::vector<DerivedSeatMatcherDef> open_on;
    std::vector<DerivedSeatMatcherDef> close_on;
    std::vector<DerivedSeatMatcherDef> block_on;
    std::string window_of;               /**< block: the window def id. */
    nlohmann::json raw;                  /**< Verbatim file object. */
};

/**
 * @struct BindingSoundDef
 * @brief One sound of a binding case.
 */
struct BindingSoundDef {
    std::string sound;                 /**< Core sound id or asset value. */
    std::optional<double> volume;
    std::optional<double> pitch;
};

/**
 * @struct BindingParamDef
 * @brief The source of one play log parameter: exactly one is set.
 */
struct BindingParamDef {
    std::string field;                 /**< Payload field name. */
    std::optional<nlohmann::json> value;  /**< Literal string or number. */
    bool actor = false;                /**< The envelope actor. */
    std::string streak;    /**< Streak id to read. */
    std::string key;       /**< Literal key into an object value. */
    std::string key_field; /**< Payload field that names the key. */
    std::optional<nlohmann::json> default_value;  /**< Used when no value. */
    std::string card_part; /**< "kind" or "color" of a card in `field`. */
    bool cause = false;    /**< Read `field` from the cause event. */
};

/**
 * @struct BindingLogDef
 * @brief The play log line of a binding case.
 */
struct BindingLogDef {
    std::vector<std::string> variants;  /**< Text keys; the client picks one. */
    std::map<std::string, BindingParamDef> params;
};

/**
 * @struct BindingCaseDef
 * @brief One case: the tests that all must hold and what to present.
 */
struct BindingCaseDef {
    std::vector<StreakTestDef> tests;
    std::vector<nlohmann::json> beats;       /**< Each beat is a step array. */
    std::vector<BindingSoundDef> sounds;
    std::optional<BindingLogDef> log;
    std::string glossary;                    /**< Keyword key, may be empty. */
};

/**
 * @struct BindingDef
 * @brief One presentation binding (`presentations/<name>.json`, format
 *        version 1, top level key `uni:binding`).
 */
struct BindingDef {
    std::string id;                      /**< Local id `[a-z0-9_]+`. */
    std::string namespace_id;            /**< Owning mod id. */
    std::string binding_id;              /**< Full `namespace:id`. */
    std::string event;                   /**< Client event id it binds to. */
    std::vector<BindingCaseDef> cases;
    std::string select = "all";  /**< "all" or "first" matching case. */
    nlohmann::json raw;                  /**< Verbatim file object. */
};

/**
 * @brief Full-`namespace:id` -> entity def, sorted.
 *
 * Mirrors `ops::TableRegistry`: a per-match, read-only collection the assembler
 * freezes once so concurrent matches cannot clobber each other's entities.
 */
using EntityRegistry = std::map<std::string, EntityDef, std::less<>>;

/**
 * @brief Full-`namespace:id` -> presentation def, sorted.
 */
using PresentationRegistry =
    std::map<std::string, PresentationDef, std::less<>>;

/**
 * @brief Full-`namespace:id` -> event definition, sorted.
 */
using EventRegistry = std::map<std::string, EventDef, std::less<>>;

/**
 * @brief Full-`namespace:id` -> streak definition, sorted.
 */
using StreakRegistry = std::map<std::string, StreakDef, std::less<>>;

/**
 * @brief Full-`namespace:id` -> derived event definition, sorted.
 */
using DerivedRegistry = std::map<std::string, DerivedEventDef, std::less<>>;

/**
 * @brief Full-`namespace:id` -> presentation binding, sorted.
 */
using BindingRegistry = std::map<std::string, BindingDef, std::less<>>;

/**
 * @struct SettingDecl
 * @brief A named setting declared by a mod manifest.
 */
struct SettingDecl {
    std::string id;
    std::string type;                   /**< int|number|bool|string. */
    nlohmann::json default_value;
    std::optional<nlohmann::json> range;
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
    /**< When true the loader omits this mod from production builds. */
    bool dev_only = false;
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
 * @struct TextsFileDef
 * @brief One `texts/<locale>.json` file: a flat map of key to string.
 *
 * `content` is null when the file is too large to read, or when it is not
 * valid JSON; `read_error` then holds the reason. The validator reports it.
 */
struct TextsFileDef {
    std::string locale;       /**< File name without the extension. */
    std::string path;         /**< Server-side path (never sent). */
    std::uintmax_t size = 0;  /**< Size of the file in bytes. */
    std::string hash;         /**< SHA-256 hex of the bytes of the file. */
    nlohmann::json content;   /**< The parsed object, or null. */
    std::string read_error;   /**< Empty, or why `content` is null. */
};

/**
 * @struct GlossaryEntryDef
 * @brief One `glossary/<name>.json` file: an explicit glossary entry. It holds
 *        translation keys only; the texts come from the `texts` files.
 */
struct GlossaryEntryDef {
    std::string path;                   /**< File path, for reports. */
    std::string namespace_id;           /**< Owning mod id. */
    std::string entry_id;               /**< Full `namespace:name`. */
    std::string label_key;
    std::string description_key;
    nlohmann::json raw;                 /**< Verbatim file content. */
};

/**
 * @struct LoadedMod
 * @brief One fully parsed mod folder.
 */
struct LoadedMod {
    std::string folder;                 /**< Folder name on disk. */
    std::string path;                   /**< Absolute/relative folder path. */
    ModManifest manifest;
    std::string content_hash;           /**< SHA-256 hex of the folder. */
    std::vector<CardDef> cards;
    std::vector<RuleDef> rules;
    std::vector<StatusDef> statuses;
    std::vector<MutationDef> mutations;
    std::vector<DeckDef> decks;
    std::vector<AssetBundleDef> assets;
    std::vector<TableDef> tables;
    std::vector<EntityDef> entities;
    std::vector<PresentationDef> presentations;
    std::vector<EventDef> events;
    std::vector<StreakDef> streaks;
    std::vector<DerivedEventDef> derived_events;
    std::vector<BindingDef> bindings;
    std::vector<TextsFileDef> texts;
    std::vector<GlossaryEntryDef> glossary;
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
