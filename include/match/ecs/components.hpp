#pragma once

#include <match/ecs/entity_store.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

/**
 * @file components.hpp
 * @brief Phase-1 component catalog and string-ID introspection.
 *
 * Every component is a data-only struct; meaning is attached by systems. The
 * catalog assigns each struct a stable string ID. Mods reference components by
 * these IDs in op args, and the component catalog is the extension point for
 * future component kinds.
 *
 * Behavior graphs, window declarations and restriction conditions are carried
 * as raw JSON here. The store does not interpret them; the Resolver and the
 * validator (validator) own their meaning.
 */

namespace match::ecs {

// --- card_identity ---------------------------------------------------------

/**
 * @struct CardIdentity
 * @brief Frozen kind reference for a card entity.
 */
struct CardIdentity {
    uint32_t mod_index = 0;   /**< position in the frozen mod list. */
    uint32_t kind_index = 0;  /**< position in that mod's kind list. */
    std::string kind_id;      /**< frozen `namespace:id` reference. */
};

// --- in_zone ---------------------------------------------------------------

/**
 * @enum ZoneKind
 * @brief Where a card currently lives.
 */
enum class ZoneKind {
    kHand,
    kDrawPile,
    kDiscardPile,
    kLimbo,
};

/**
 * @struct ZoneRef
 * @brief Zone identity: hand of a player, or a pile, or limbo.
 */
struct ZoneRef {
    ZoneKind kind = ZoneKind::kLimbo;
    Entity owner{};  /**< player entity for kHand; unused for piles. */
};

/**
 * @struct InZone
 * @brief Current zone of a card plus its ordinal within that zone.
 */
struct InZone {
    ZoneRef zone;
    uint32_t ordinal = 0;  /**< display order / pile position. */
};

// --- face_spec -------------------------------------------------------------

/**
 * @enum FaceKind
 * @brief Declarative face shapes.
 */
enum class FaceKind {
    kText,
    kArtRef,
    kEmoji,
    kBlank,
};

/**
 * @struct FaceSpec
 * @brief Copy of a card's definition face; basis of the client face hash.
 */
struct FaceSpec {
    FaceKind kind = FaceKind::kBlank;
    std::string color;
    std::string label;
    std::string url;
    int art_version = 1;
};

// --- card_behavior ---------------------------------------------------------

/**
 * @struct CardBehavior
 * @brief Trigger-to-graph map copied from the definition at assembly.
 *
 * Keys are hook names (e.g. `on_play`, `after:play`); values are raw behavior
 * graphs, already post-mutation once assembly has applied them.
 */
struct CardBehavior {
    std::map<std::string, nlohmann::json> triggers;
};

// --- auto_trigger ----------------------------------------------------------

/**
 * @struct AutoTrigger
 * @brief Must-apply auto card: condition + graph + `must_apply`.
 */
struct AutoTrigger {
    nlohmann::json condition;  /**< condition object. */
    nlohmann::json graph;      /**< behavior graph to run when matched. */
    bool must_apply = false;   /**< true = fires without player agency. */
};

// --- window_spec -----------------------------------------------------------

/**
 * @struct WindowSpec
 * @brief A card's window declaration, copied from the definition.
 */
struct WindowSpec {
    std::string responders;    /**< selector, e.g. `@others`. */
    nlohmann::json respond_with;  /**< eligibility filter. */
    std::string duration;      /**< `env` or a duration token. */
    std::string on_response;   /**< response route. */
    std::string default_route; /**< default (timeout) route. */
    nlohmann::json raw;        /**< verbatim declaration. */
};

// --- player_info -----------------------------------------------------------

/**
 * @struct PlayerInfo
 * @brief Identity + connection state of a player entity.
 */
struct PlayerInfo {
    std::string username;
    uint32_t seat = 0;
    bool connected = true;
    bool is_bot = false;
    bool ready = false;
};

// --- hand ------------------------------------------------------------------

/**
 * @struct Hand
 * @brief Ordered card entity refs (display order) for a player.
 */
struct Hand {
    std::vector<Entity> cards;
};

// --- turn_state ------------------------------------------------------------

/**
 * @struct TurnState
 * @brief Turn ownership, deadline and queued extra turns for a player.
 */
struct TurnState {
    bool is_current = false;
    int64_t turn_deadline_ms = 0;  /**< absolute epoch ms; 0 = none. */
    uint32_t extra_turns_pending = 0;
};

// --- status ----------------------------------------------------------------

/**
 * @enum StackPolicy
 * @brief How re-applying a status merges with an existing instance.
 */
enum class StackPolicy {
    kReplace,
    kAccumulate,
    kIndependent,
    kCap,
};

/**
 * @enum DurationUnit
 * @brief Duration units.
 */
enum class DurationUnit {
    kMs,
    kTurns,
    kRounds,
    kCardsPlayed,
};

/**
 * @struct DurationSpec
 * @brief One duration leg: unit plus value.
 */
struct DurationSpec {
    DurationUnit unit = DurationUnit::kTurns;
    int64_t value = 0;
};

/**
 * @struct Status
 * @brief A status instance on any entity.
 */
struct Status {
    std::string status_id;  /**< frozen `namespace:id` status kind. */
    int32_t magnitude = 1;
    StackPolicy stack_policy = StackPolicy::kReplace;
    uint32_t cap = 0;       /**< kCap ceiling; 0 when unused. */
    DurationSpec duration;
    uint32_t instance_id = 0;  /**< independent-instance identity. */
    bool hidden = false;       /**< private status. */
};

// --- visibility_grant ------------------------------------------------------

/**
 * @enum Aspect
 * @brief Visibility aspect bits.
 */
enum class Aspect : uint32_t {
    kCount = 1u << 0,
    kColor = 1u << 1,
    kValue = 1u << 2,
    kIdentity = 1u << 3,
    kPosition = 1u << 4,
};

/**
 * @struct VisibilityGrant
 * @brief `(viewer, aspect_mask, expires)` entries for an entity.
 */
struct VisibilityGrant {
    struct Entry {
        Entity viewer;
        uint32_t aspect_mask = 0;
        int64_t expires_ms = 0;  /**< absolute epoch ms; 0 = never. */
    };
    std::vector<Entry> entries;
};

// --- draw_debt -------------------------------------------------------------

/**
 * @struct DrawDebt
 * @brief Accumulated pending draw count (draw-stacking successor).
 */
struct DrawDebt {
    uint32_t count = 0;
};

// --- play_restriction ------------------------------------------------------

/**
 * @enum RestrictionPhase
 * @brief Restriction pipeline entry kind.
 */
enum class RestrictionPhase {
    kAllow,
    kDeny,
};

/**
 * @struct RestrictionEntry
 * @brief One restriction pipeline entry on the match entity.
 */
struct RestrictionEntry {
    std::string id;
    RestrictionPhase phase = RestrictionPhase::kDeny;
    nlohmann::json condition;
};

/**
 * @struct PlayRestriction
 * @brief Ordered restriction pipeline.
 */
struct PlayRestriction {
    std::vector<RestrictionEntry> entries;
};

// --- active_type_req -------------------------------------------------------

/**
 * @struct ActiveTypeReq
 * @brief Current required card type, or none.
 */
struct ActiveTypeReq {
    std::optional<std::string> type;  /**< empty = no requirement. */
};

// --- window_state ----------------------------------------------------------

/**
 * @struct WindowResponse
 * @brief One collected response inside an open window.
 */
struct WindowResponse {
    Entity responder;
    Entity card{};  /**< invalid when `pass` is true. */
    bool pass = false;
    uint64_t arrival_seq = 0;  /**< server arrival order for tie-breaks. */
};

/**
 * @struct WindowState
 * @brief Open response window on the match entity.
 */
struct WindowState {
    bool open = false;
    uint32_t window_id = 0;
    std::vector<Entity> responders;
    int64_t deadline_ms = 0;
    std::string default_route;
    std::string filter_digest;
    std::vector<WindowResponse> responses;
};

// --- pending_schedule ------------------------------------------------------

/**
 * @struct PendingSchedule
 * @brief Deferred graphs with duration specs.
 */
struct PendingSchedule {
    struct Entry {
        uint32_t id = 0;
        nlohmann::json graph;
        DurationSpec duration;
        int64_t started_ms = 0;  /**< absolute epoch ms the entry was armed. */
    };
    std::vector<Entry> entries;
};

// --- match_meta ------------------------------------------------------------

/**
 * @enum Direction
 * @brief Play direction, forward is seat order +1.
 */
enum class Direction {
    kForward = 1,
    kReverse = -1,
};

/**
 * @struct ModRef
 * @brief One entry of the frozen match mod list (priority order).
 */
struct ModRef {
    std::string id;
    std::string version;
};

/**
 * @struct BudgetLedger
 * @brief Accumulated runtime-budget counters.
 */
struct BudgetLedger {
    uint64_t chain_steps = 0;
    uint64_t events = 0;
    uint32_t must_apply_depth = 0;
};

/**
 * @struct MatchMeta
 * @brief Match-level identity and counters.
 */
struct MatchMeta {
    std::vector<ModRef> mods;  /**< frozen mod list, priority order. */
    Direction direction = Direction::kForward;
    uint32_t round = 0;
    uint64_t rng_seed = 0;
    BudgetLedger budgets;
};

// --- placements ------------------------------------------------------------

/**
 * @struct Placements
 * @brief Finish order.
 */
struct Placements {
    std::vector<Entity> order;
};

// --- rng_state -------------------------------------------------------------

/**
 * @struct RngState
 * @brief Seed plus monotonic op counter.
 */
struct RngState {
    uint64_t seed = 0;
    uint64_t op_counter = 0;
};

// --- pile_contents ---------------------------------------------------------

/**
 * @enum PileKind
 * @brief Whether a pile entity is the draw pile or the discard pile.
 */
enum class PileKind {
    kDraw,
    kDiscard,
};

/**
 * @struct PileContents
 * @brief Ordered card refs held by a draw/discard pile entity.
 */
struct PileContents {
    PileKind kind = PileKind::kDraw;
    std::vector<Entity> cards;
};

// --- component catalog -----------------------------------------------------

/**
 * @struct ComponentType
 * @brief Catalog entry: a component's string ID, C++ type and pool accessor.
 *
 * WARN: `Acquire` is an unchecked low-level hook — it does not validate entity
 *       liveness. Use the checked `AddComponent` / `GetComponent` /
 *       `HasComponent` / `RemoveComponent` wrappers below to address
 *       components by ID.
 */
struct ComponentType {
    std::string_view id;  /**< stable catalog ID referenced by mods. */
    std::type_index type; /**< concrete struct type. */
    IComponentPool& (*Acquire)(EntityStore& store);  /**< acquire-or-create. */
};

/**
 * @class ComponentCatalog
 * @brief Registry of every phase-1 component ID.
 *
 * Lookup is a structured miss (`nullptr` / `false`), never a crash. The
 * catalog is singleton and its ID set is frozen for phase 1.
 */
class ComponentCatalog {
public:
    ComponentCatalog(const ComponentCatalog&) = delete;
    ComponentCatalog& operator=(const ComponentCatalog&) = delete;
    ComponentCatalog(ComponentCatalog&&) = delete;
    ComponentCatalog& operator=(ComponentCatalog&&) = delete;

    /** Process-wide catalog instance. */
    static const ComponentCatalog& Instance();

    /** Registered type for `id`, or nullptr when unknown (structured miss). */
    const ComponentType* Find(std::string_view id) const;

    /** True when `id` is registered. */
    bool Contains(std::string_view id) const;

    /** Every registered type, in registration order. */
    const std::vector<ComponentType>& Types() const { return types_; }

    /** Every registered ID, in registration order. */
    std::vector<std::string_view> Ids() const;

private:
    ComponentCatalog();

    std::vector<ComponentType> types_;
    std::unordered_map<std::string_view, std::size_t> by_id_;
};

// --- checked by-ID access (the sanctioned erased seam) ---------------------

// These wrappers validate entity liveness AND catalog ID before touching a
// pool, so dead handles and unknown IDs are structured misses. The ops and
// The view builder should address components by catalog ID through these,
// never through the raw `ComponentType::Acquire` / `*Erased` primitives.

/**
 * @brief Adds or overwrites a component addressed by catalog ID.
 *
 * @return true when stored; false on a dead entity, unknown ID or null value.
 */
bool AddComponent(EntityStore& store, Entity entity, std::string_view id,
                  const void* value);

/**
 * @brief Mutable component addressed by catalog ID.
 *
 * @return Pointer to the component, or nullptr on a dead entity, unknown ID
 *         or absent component. Invalidated by later `Add`/`Remove` on the
 *         pool.
 */
void* GetComponent(EntityStore& store, Entity entity, std::string_view id);

/**
 * @brief Const component addressed by catalog ID.
 *
 * @return Pointer to the component, or nullptr on a dead entity, unknown ID
 *         or absent component. Invalidated by later `Add`/`Remove` on the
 *         pool.
 */
const void* GetComponent(const EntityStore& store, Entity entity,
                         std::string_view id);

/**
 * @brief Presence of a component addressed by catalog ID.
 *
 * @return true only for a live entity carrying the component; false on a dead
 *         entity, unknown ID or absent component.
 */
bool HasComponent(const EntityStore& store, Entity entity, std::string_view id);

/**
 * @brief Removes a component addressed by catalog ID.
 *
 * @return true when removed; false on a dead entity, unknown ID or absent
 *         component.
 */
bool RemoveComponent(EntityStore& store, Entity entity, std::string_view id);

}  // namespace match::ecs
