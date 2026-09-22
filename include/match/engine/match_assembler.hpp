#pragma once

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/ecs/event_bus.hpp>
#include <match/ecs/hooks.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/play_conditions.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/resolver.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file match_assembler.hpp
 * @brief Match assembly: freeze registries and build the ECS store.
 *
 * The assembler is the only place a per-match registry is frozen. From the
 * active mod set (the deck's `mods` list, in priority
 * order), the deck's card multiset and the seated players it:
 *
 * - freezes the mod list order into `MatchMeta.mods` (dispatch priority);
 * - freezes the per-mod kind table and the `CompactCardV2` index maps,
 *   assigning deterministic kind-sorted instance ids and enforcing the
 *   256 / 4096 / 4096 index-space bounds with a structured error;
 * - creates card entities (`CardIdentity` / `InZone` / `FaceSpec` /
 *   `CardBehavior` / `WindowSpec`), player entities (`PlayerInfo` / `Hand` /
 *   `TurnState`), draw and discard pile entities (`PileContents`) and the
 *   match entity (`MatchMeta` / `Placements` / `RngState` / `PlayRestriction` /
 *   `ActiveTypeReq` / `PendingSchedule` / `BudgetLedger`);
 * - seeds the RNG,
 *   shuffles the draw pile, deals `starting_cards` to each player in seat order
 *   and opens the discard with a numeric starter card (legacy `GenerateDeck` /
 *   `Start` semantics);
 * - subscribes every rule hook and card behavior as a mod system on the
 *   `EventBus` in frozen order (mod list order, then registration order) and
 *   dispatches `match_start`, which is what installs the vanilla restriction
 *   entries shipped as `after:match_start` data
 * - installs the `PlayConditionMatcher` and populates the per-match the op
 *   layer `CardTagTable` (`MatchRegistries::card_tags`) from the loaded card
 *   defs.
 *
 * ADDITIVE: the legacy `match::MatchInstance` keeps its own
 * files until the swap. Everything here lives in `match::engine`.
 */

namespace match::engine {

/**
 * @struct MatchPlayerSpec
 * @brief One seated player handed to assembly (seat order = vector order).
 */
struct MatchPlayerSpec {
    std::string username;
    bool is_bot = false;
    bool connected = true;
    bool ready = false;
};

/**
 * @struct MatchAssemblyOptions
 * @brief Non-content inputs to assembly.
 */
struct MatchAssemblyOptions {
    std::vector<MatchPlayerSpec> players;  /**< seat order, seat 0 first. */
    int starting_cards = 7;                /**< legacy `starting_cards`. */
    std::optional<uint64_t> seed;          /**< explicit; else env / random. */
};

/**
 * @struct KindIndex
 * @brief Frozen position of a card kind in the `CompactCardV2` index space.
 */
struct KindIndex {
    uint32_t mod_index = 0;   /**< position in the frozen mod list. */
    uint32_t kind_index = 0;  /**< position in that mod's sorted kind list. */
};

/**
 * @struct MatchRegistries
 * @brief The frozen per-match index maps plus root entity handles.
 *
 * Kind ids are sorted per mod so `kind_index` is deterministic; instance ids
 * are assigned kind-sorted with a per-kind counter, so two assemblies of the
 * same content produce identical `CompactCardV2` values.
 */
struct MatchRegistries {
    std::vector<ecs::ModRef> mods;  /**< frozen mod list, priority order. */
    /** Sorted kind ids per `mod_index` (position in `mods`). */
    std::vector<std::vector<std::string>> kinds_by_mod;
    /** `kind_id` -> frozen position. */
    std::unordered_map<std::string, KindIndex> kind_index;
    /** Card entities in creation (kind-sorted) order. */
    std::vector<ecs::Entity> cards;
    /** Compact identity parallel to `cards`. */
    std::vector<ecs::CompactCardV2> card_ids;
    /** Player entities in seat order. */
    std::vector<ecs::Entity> players;
    ecs::Entity match{};
    ecs::Entity draw_pile{};
    ecs::Entity discard_pile{};

    /**
     * @brief Frozen `kind_id -> declared tags` for this match's content.
     *
     * the card tag table lives here, per match, instead of a
     * process-global static. `has_card_tag` / `draw_penalty` / `tag:` routing
     * read it through `OpContext::registries`, so two concurrent matches with
     * different content cannot clobber each other. Assembly fills it once
     * (before `match_start`) and it is read-only during play.
     */
    ops::CardTagTable card_tags;

    /** @brief Frozen position of `kind_id`, or nullopt when not active. */
    std::optional<KindIndex> FindKind(const std::string& kind_id) const;

    /** @brief Compact identity of an assembled card, or nullopt. */
    std::optional<ecs::CompactCardV2> CardId(ecs::Entity card) const;

    /** @brief Assembled card entity for a compact identity, or nullopt. */
    std::optional<ecs::Entity> CardEntity(ecs::CompactCardV2 id) const;

    /**
     * @brief Record `card` under its compact identity in both index maps.
     *
     * The one place `EntityKey` packing happens; assembly uses it per created
     * card and tests use it to build a fixture map. `card_ids` is appended in
     * call order, so `cards[i]` and `card_ids[i]` stay parallel when both are
     * pushed together.
     */
    void AddCard(ecs::Entity card, ecs::CompactCardV2 id);

    /** @brief Internal: packed-entity lookup table for `CardId`. */
    std::unordered_map<uint64_t, uint32_t> card_by_entity;
    /** @brief Internal: reverse `CompactCardV2.bits` -> card entity. */
    std::unordered_map<uint32_t, ecs::Entity> entity_by_card;
};

/**
 * @struct ModSystem
 * @brief One subscribed mod system: a rule hook or a card behavior.
 *
 * `card_kind` is set for card behaviors so the hook filter only runs the
 * played card's own behavior; rule hooks leave it empty and use `where`.
 */
struct ModSystem {
    std::string mod_id;                    /**< owner mod (dispatch order). */
    uint32_t registration_index = 0;       /**< order within the mod. */
    ecs::HookId hook;                      /**< resolved hook identity. */
    std::string source_id;                 /**< rule id or card kind id. */
    std::optional<std::string> card_kind;  /**< set for card behaviors. */
    std::optional<nlohmann::json> where;   /**< hook condition filter. */
    modload::BehaviorGraph graph;          /**< behavior graph to walk. */
};

/**
 * @struct HookRun
 * @brief Outcome of one mod-system invocation, logged.
 */
struct HookRun {
    std::string mod_id;
    std::string source_id;
    ecs::HookId hook;
    resolver::ResolveStatus status = resolver::ResolveStatus::kComplete;
    std::vector<nlohmann::json> events;
    std::vector<nlohmann::json> effects;  /**< op effect descriptors. */
    std::string error;

    // INFO: Additive pause continuation. When a walk pauses
    //       (`kNeedsInput` / `kWindow` / `kSchedule`) the engine (and the
    //       engine for windows) resumes it from these fields; all empty means
    //       the run completed without pausing. No frozen signature changes.
    std::size_t system_index = 0;            /**< index into `systems`. */
    resolver::SelectorContext context;       /**< selectors at the pause. */
    std::optional<ops::InputRequest> input_request;
    std::optional<resolver::WindowRequest> window;
    std::optional<resolver::ScheduleRequest> schedule;
    std::optional<resolver::ResumeToken> resume;
};

/**
 * @struct AssemblyError
 * @brief Structured assembly failure in the shape.
 */
struct AssemblyError {
    std::string check;    /**< short machine-readable check id. */
    std::string message;  /**< human-readable explanation with counts. */
};

class MatchAssembler;

/**
 * @struct MatchDeckSnapshot
 * @brief Retained deck identity + settings for the wire `match_start`.
 *
 * The engine freezes only the deck's mod list and card multiset; the view layer
 * also needs the deck id / name and the lobby settings bag to build
 * `match_start` and to carry real settings into `match_end`. Retained
 * additively at assembly time (this resolves the former `match_instance.cpp`
 * TODO).
 */
struct MatchDeckSnapshot {
    std::string deck_id;    /**< Full `namespace:id`, or empty. */
    std::string name;       /**< Human-readable deck name, or empty. */
    nlohmann::json settings = nlohmann::json::object(); /**< Settings bag. */
};

/**
 * @class MatchAssembly
 * @brief The frozen per-match state: store, bus and registries.
 *
 * Heap-pinned (owned by `std::unique_ptr`) because the Resolver holds
 * references into `store`, `runtime`, `bus`, `budget` and `conditions`; the
 * object is non-copyable and non-movable so those references stay valid.
 */
class MatchAssembly {
public:
    MatchAssembly() = default;
    MatchAssembly(const MatchAssembly&) = delete;
    MatchAssembly& operator=(const MatchAssembly&) = delete;
    MatchAssembly(MatchAssembly&&) = delete;
    MatchAssembly& operator=(MatchAssembly&&) = delete;
    ~MatchAssembly() = default;

    /** @brief Entity store holding every assembled entity. */
    ecs::EntityStore store;
    /** @brief Hook bus with every mod system subscribed in frozen order. */
    ecs::EventBus bus;
    /** @brief Frozen registries and root entity handles. */
    MatchRegistries registries;
    /** @brief Default op bodies (`RegisterDefaultOps`). */
    ops::OpRuntime runtime;
    /** @brief Default condition evaluators. */
    resolver::ConditionRegistry conditions;
    /** @brief Match budget ledger (mirrored into `MatchMeta.budgets`). */
    ecs::BudgetLedger budget;
    /** @brief Behavior-graph walker bound to the members above. */
    std::unique_ptr<resolver::Resolver> resolver;
    /** @brief Matcher installed for the restriction pipeline. */
    std::unique_ptr<modload::PlayConditionMatcher> play_matcher;
    /** @brief Content facts for the lookup, keyed by kind id. */
    std::map<std::string, modload::PlayCardFacts> card_facts;
    /** @brief Every subscribed mod system, in frozen dispatch order. */
    std::vector<ModSystem> systems;
    /** @brief Log of every system invocation (assembly and later hooks). */
    std::vector<HookRun> runs;

    /**
     * @brief Deck identity + settings retained from the assembly input.
     *
     * Populated by `MatchAssembler::Assemble`; the view layer reads it through
     * `Deck()` to build `match_start` and `match_end` settings.
     */
    MatchDeckSnapshot deck;

    /** @brief Retained deck identity + settings snapshot. */
    const MatchDeckSnapshot& Deck() const { return deck; }

    /** @brief Every subscribed system, in frozen dispatch order. */
    const std::vector<ModSystem>& Systems() const { return systems; }

    /**
     * @brief Run system `index` for `payload` (the EventBus callback body).
     *
     * Public so the engine can dispatch the in-play hooks through the bus and
     * reuse exactly the same system execution path. A no-op when `index` is out
     * of range, the owning mod is disarmed, the card-kind filter fails or a
     * `where` filter evaluates false.
     */
    void RunSystem(std::size_t index, ecs::HookPayload& payload);

private:
    friend class MatchAssembler;
};

/**
 * @struct AssemblyResult
 * @brief `Assemble` outcome: the pinned assembly or a structured error.
 */
struct AssemblyResult {
    std::unique_ptr<MatchAssembly> assembly;
    std::optional<AssemblyError> error;

    /** @brief True when `assembly` is present. */
    bool ok() const { return assembly != nullptr; }
};

/**
 * @class MatchAssembler
 * @brief Builds a `MatchAssembly` from loaded mods, a deck and players.
 */
class MatchAssembler {
public:
    /**
     * @brief Freeze registries and build the match's ECS store.
     *
     * @param mods     Every loaded mod (from `ScanModsDirectory`).
     * @param deck     The deck snapshot; `deck.mods` is the frozen mod order.
     * @param options  Seated players, starting hand size and optional seed.
     * @return The pinned assembly, or a structured error (never both).
     */
    static AssemblyResult Assemble(const std::vector<modload::LoadedMod>& mods,
                                   const modload::DeckDef& deck,
                                   const MatchAssemblyOptions& options);
};

}  // namespace match::engine
