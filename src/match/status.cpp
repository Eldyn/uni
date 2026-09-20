#include <match/status.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file status.cpp
 * @brief Status storage + stack policy bodies.
 *
 * All state lives in the additive `status_list` component; the legacy
 * single-instance `Status` component is never read or written here. The bodies
 * are total: a dead entity, a missing list or an empty status id is a
 * structured miss.
 */

namespace match::status {
namespace {

/** @brief Saturating 32-bit add (magnitudes are `int32_t`). */
int32_t SaturateAdd(int32_t a, int32_t b) {
    const int64_t sum = static_cast<int64_t>(a) + static_cast<int64_t>(b);
    if (sum > std::numeric_limits<int32_t>::max()) {
        return std::numeric_limits<int32_t>::max();
    }
    if (sum < std::numeric_limits<int32_t>::min()) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(sum);
}

/** @brief Accumulate toward `cap` (`cap == 0` = no ceiling). */
int32_t CapAccumulate(int32_t current, int32_t add, uint32_t cap) {
    const int32_t summed = SaturateAdd(current, add);
    if (cap == 0) return summed;
    if (cap > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return summed;
    }
    const int32_t ceiling = static_cast<int32_t>(cap);
    return summed > ceiling ? ceiling : summed;
}

/** @brief First same-kind instance, or nullptr. */
ecs::Status* FirstOfKind(ecs::StatusList& list, std::string_view status_id) {
    for (ecs::Status& instance : list.instances) {
        if (std::string_view(instance.status_id) == status_id) {
            return &instance;
        }
    }
    return nullptr;
}

/** @brief Build a fresh instance from a request and a minted id. */
ecs::Status MakeInstance(const ApplyRequest& request, uint32_t instance_id) {
    ecs::Status instance;
    instance.status_id = request.status_id;
    instance.magnitude = request.magnitude;
    instance.stack_policy = request.stack_policy;
    instance.cap = request.cap;
    instance.duration = request.duration;
    instance.instance_id = instance_id;
    instance.hidden = request.hidden;
    return instance;
}

/** @brief Copy an applied instance into the result envelope. */
ApplyResult ToResult(const ecs::Status& instance, bool created) {
    ApplyResult result;
    result.applied = true;
    result.created = created;
    result.status_id = instance.status_id;
    result.magnitude = instance.magnitude;
    result.stack_policy = instance.stack_policy;
    result.cap = instance.cap;
    result.duration = instance.duration;
    result.instance_id = instance.instance_id;
    result.hidden = instance.hidden;
    return result;
}

}  // namespace

uint32_t MintInstanceId(const ecs::StatusList& list) {
    uint32_t max_id = 0;
    for (const ecs::Status& instance : list.instances) {
        if (instance.instance_id > max_id) max_id = instance.instance_id;
    }
    if (max_id == std::numeric_limits<uint32_t>::max()) return max_id;
    return max_id + 1;
}

std::vector<ecs::Status> List(const ecs::EntityStore& store,
                              ecs::Entity entity) {
    const ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return {};
    return list->instances;
}

ecs::Status* Find(ecs::EntityStore& store, ecs::Entity entity,
                  std::string_view status_id) {
    if (status_id.empty()) return nullptr;
    ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return nullptr;
    return FirstOfKind(*list, status_id);
}

const ecs::Status* Find(const ecs::EntityStore& store, ecs::Entity entity,
                        std::string_view status_id) {
    if (status_id.empty()) return nullptr;
    const ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return nullptr;
    for (const ecs::Status& instance : list->instances) {
        if (std::string_view(instance.status_id) == status_id) {
            return &instance;
        }
    }
    return nullptr;
}

ecs::Status* FindByInstance(ecs::EntityStore& store, ecs::Entity entity,
                            uint32_t instance_id) {
    ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return nullptr;
    for (ecs::Status& instance : list->instances) {
        if (instance.instance_id == instance_id) return &instance;
    }
    return nullptr;
}

const ecs::Status* FindByInstance(const ecs::EntityStore& store,
                                  ecs::Entity entity, uint32_t instance_id) {
    const ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return nullptr;
    for (const ecs::Status& instance : list->instances) {
        if (instance.instance_id == instance_id) return &instance;
    }
    return nullptr;
}

bool Has(const ecs::EntityStore& store, ecs::Entity entity,
         std::string_view status_id) {
    return Find(store, entity, status_id) != nullptr;
}

bool IsHidden(const ecs::EntityStore& store, ecs::Entity entity,
              std::string_view status_id) {
    if (status_id.empty()) return false;
    const ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return false;
    for (const ecs::Status& instance : list->instances) {
        if (std::string_view(instance.status_id) == status_id
            && instance.hidden) {
            return true;
        }
    }
    return false;
}

ApplyResult Apply(ecs::EntityStore& store, ecs::Entity entity,
                  const ApplyRequest& request) {
    if (request.status_id.empty() || !store.IsAlive(entity)) {
        return ApplyResult{};
    }

    ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) {
        list = store.Add(entity, ecs::StatusList{});
        if (list == nullptr) return ApplyResult{};
    }

    ecs::Status* existing = FirstOfKind(*list, request.status_id);
    if (existing == nullptr) {
        list->instances.push_back(
            MakeInstance(request, MintInstanceId(*list)));
        return ToResult(list->instances.back(), true);
    }

    const ecs::StackPolicy policy = request.has_stack_policy
        ? request.stack_policy
        : existing->stack_policy;
    const uint32_t cap = request.has_stack_policy ? request.cap
                                                  : existing->cap;

    // INFO: `independent` never merges; it appends a second instance that
    //       carries its own duration and instance id.
    if (policy == ecs::StackPolicy::kIndependent) {
        ecs::Status created = *existing;
        created.magnitude = request.magnitude;
        created.duration = request.duration;
        created.stack_policy = policy;
        created.cap = cap;
        created.hidden = request.hidden;
        created.instance_id = MintInstanceId(*list);
        list->instances.push_back(std::move(created));
        return ToResult(list->instances.back(), true);
    }

    switch (policy) {
        case ecs::StackPolicy::kReplace:
            existing->magnitude = request.magnitude;
            existing->duration = request.duration;
            break;
        case ecs::StackPolicy::kAccumulate:
            existing->magnitude =
                SaturateAdd(existing->magnitude, request.magnitude);
            break;
        case ecs::StackPolicy::kCap:
            existing->magnitude =
                CapAccumulate(existing->magnitude, request.magnitude, cap);
            break;
        case ecs::StackPolicy::kIndependent:
            break;  // handled above
    }
    existing->stack_policy = policy;
    existing->cap = cap;
    existing->hidden = request.hidden;
    return ToResult(*existing, false);
}

std::vector<ecs::Status> Remove(ecs::EntityStore& store, ecs::Entity entity,
                                std::string_view status_id) {
    std::vector<ecs::Status> removed;
    if (status_id.empty()) return removed;
    ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return removed;
    auto it = list->instances.begin();
    while (it != list->instances.end()) {
        if (std::string_view(it->status_id) == status_id) {
            removed.push_back(*it);
            it = list->instances.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

std::optional<ecs::Status> RemoveByInstance(ecs::EntityStore& store,
                                            ecs::Entity entity,
                                            uint32_t instance_id) {
    ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return std::nullopt;
    for (auto it = list->instances.begin(); it != list->instances.end();
         ++it) {
        if (it->instance_id == instance_id) {
            ecs::Status removed = *it;
            list->instances.erase(it);
            return removed;
        }
    }
    return std::nullopt;
}

std::optional<ModifyResult> Modify(ecs::EntityStore& store, ecs::Entity entity,
                                   std::string_view status_id, int64_t delta) {
    if (status_id.empty()) return std::nullopt;
    ecs::StatusList* list = store.Get<ecs::StatusList>(entity);
    if (list == nullptr) return std::nullopt;

    ModifyResult result;
    for (ecs::Status& instance : list->instances) {
        if (std::string_view(instance.status_id) != status_id) continue;
        int64_t next = static_cast<int64_t>(instance.magnitude) + delta;
        if (instance.stack_policy == ecs::StackPolicy::kCap
            && instance.cap > 0
            && instance.cap <= static_cast<uint32_t>(
                   std::numeric_limits<int32_t>::max())
            && next > static_cast<int64_t>(instance.cap)) {
            next = static_cast<int64_t>(instance.cap);
        }
        if (next > std::numeric_limits<int32_t>::max()) {
            next = std::numeric_limits<int32_t>::max();
        }
        if (next < std::numeric_limits<int32_t>::min()) {
            next = std::numeric_limits<int32_t>::min();
        }
        instance.magnitude = static_cast<int32_t>(next);
        result.adjusted.push_back(instance);
    }
    if (result.adjusted.empty()) return std::nullopt;
    return result;
}

}  // namespace match::status
