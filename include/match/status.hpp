#pragma once

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file status.hpp
 * @brief Status subsystem storage + stack policy API.
 *
 * The store's `status` pool holds one instance per entity, which cannot express
 *  the `independent` policy (separate instances, each with its own
 * duration). The timer layer adds the additive `status_list` component (a
 * `std::vector<Status>`) and freezes this helper API as the one way to address
 * statuses on an entity. The legacy single-instance `Status` catalog entry is
 * left intact but is no longer written by the status ops.
 *
 * Policies, applied against the existing same-kind instance(s):
 * - `replace`     re-apply overwrites magnitude and duration in place;
 * - `accumulate`  magnitudes add (saturating), duration retained;
 * - `independent` a new instance is appended with its own duration and a
 *                 freshly minted `instance_id`;
 * - `cap:N`       magnitudes add up to `N`, duration retained.
 *
 * Every function is total over the store: a dead entity, a missing
 * `status_list` or an empty status id is a structured miss, never a crash.
 */

namespace match::status {

/**
 * @struct ApplyRequest
 * @brief A fully resolved incoming status instance (def defaults merged).
 *
 * `has_stack_policy == false` means "no explicit policy on this apply": the
 * existing same-kind instance's stored policy and cap are used instead (then
 * `replace` when no instance exists). This encodes the fix-round-1 rule
 * that a present `stack_policy` arg is authoritative.
 *
 * `hidden` is the status definition's privacy flag, resolved by the
 * caller. `has_hidden` gates it: when false (the op path, which cannot
 * see the definition) an existing instance's stored flag is preserved and a
 * new instance defaults to public; when true `hidden` is authoritative.
 */
struct ApplyRequest {
    std::string status_id;
    int32_t magnitude = 1;
    bool has_stack_policy = false;
    ecs::StackPolicy stack_policy = ecs::StackPolicy::kReplace;
    uint32_t cap = 0;  /**< `cap:N` ceiling; 0 = no ceiling. */
    ecs::DurationSpec duration{ecs::DurationUnit::kTurns, 0};
    bool hidden = false;
    bool has_hidden = false;  /**< true = `hidden` came from the def. */
};

/**
 * @struct ApplyResult
 * @brief The instance an apply produced or updated.
 *
 * `applied` is false on a dead entity or an empty status id. `created` is true
 * when a new instance was appended (first apply or `independent`). The
 * remaining fields mirror the affected instance.
 */
struct ApplyResult {
    bool applied = false;
    bool created = false;
    std::string status_id;
    int32_t magnitude = 0;
    ecs::StackPolicy stack_policy = ecs::StackPolicy::kReplace;
    uint32_t cap = 0;
    ecs::DurationSpec duration{ecs::DurationUnit::kTurns, 0};
    uint32_t instance_id = 0;
    bool hidden = false;
};

/**
 * @struct ModifyResult
 * @brief Every instance an adjustment changed, in list order.
 */
struct ModifyResult {
    std::vector<ecs::Status> adjusted;
};

/**
 * @brief Next instance id for `list`: max existing id + 1, minimum 1.
 *
 * Saturates at `UINT32_MAX` rather than wrapping. Order-independent, so a
 * replayed apply sequence mints the same ids.
 */
uint32_t MintInstanceId(const ecs::StatusList& list);

/**
 * @brief Copy of every status instance on `entity`; empty on a miss.
 */
std::vector<ecs::Status> List(const ecs::EntityStore& store,
                              ecs::Entity entity);

/**
 * @brief First instance of `status_id` on `entity`, or nullptr.
 */
ecs::Status* Find(ecs::EntityStore& store, ecs::Entity entity,
                  std::string_view status_id);

/** @brief Const first instance of `status_id`, or nullptr. */
const ecs::Status* Find(const ecs::EntityStore& store, ecs::Entity entity,
                        std::string_view status_id);

/**
 * @brief Instance with the exact `instance_id` on `entity`, or nullptr.
 */
ecs::Status* FindByInstance(ecs::EntityStore& store, ecs::Entity entity,
                            uint32_t instance_id);

/** @brief Const instance with the exact `instance_id`, or nullptr. */
const ecs::Status* FindByInstance(const ecs::EntityStore& store,
                                  ecs::Entity entity, uint32_t instance_id);

/**
 * @brief True when `entity` carries at least one instance of `status_id`.
 */
bool Has(const ecs::EntityStore& store, ecs::Entity entity,
         std::string_view status_id);

/**
 * @brief True when a matching instance exists and is hidden.
 */
bool IsHidden(const ecs::EntityStore& store, ecs::Entity entity,
              std::string_view status_id);

/**
 * @brief Apply an incoming instance, merging per the effective policy.
 *
 * A different `status_id` never disturbs an existing instance: kinds coexist
 * in the list. The effective policy is the request's when `has_stack_policy`,
 * otherwise the existing same-kind instance's stored policy (then `replace`).
 *
 * @return `applied == false` on a dead entity or an empty status id; otherwise
 *         the affected instance's post-apply fields.
 */
ApplyResult Apply(ecs::EntityStore& store, ecs::Entity entity,
                  const ApplyRequest& request);

/**
 * @brief Remove every instance of `status_id` from `entity`.
 *
 * @return The removed instances in list order (empty when none matched).
 */
std::vector<ecs::Status> Remove(ecs::EntityStore& store, ecs::Entity entity,
                                std::string_view status_id);

/**
 * @brief Remove the single instance with `instance_id`.
 * @return The removed instance, or nullopt when none matched.
 */
std::optional<ecs::Status> RemoveByInstance(ecs::EntityStore& store,
                                            ecs::Entity entity,
                                            uint32_t instance_id);

/**
 * @brief Adjust every instance of `status_id` by `delta`.
 *
 * Each magnitude is saturating-added and clamped to the instance's `cap:N`
 * ceiling when it carries one. `delta` is the already-bounds-clamped op value.
 *
 * @return The adjusted instances in list order, or nullopt when none matched.
 */
std::optional<ModifyResult> Modify(ecs::EntityStore& store, ecs::Entity entity,
                                   std::string_view status_id, int64_t delta);

}  // namespace match::status
