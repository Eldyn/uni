#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @file entity_store.hpp
 * @brief Dense ECS store: generational entity handles + typed component pools.
 *
 * Frozen API. Later code consumes this and does not redefine it.
 *
 * - Entities are untyped; meaning comes from components.
 * - A handle is `{index, generation}`. Destroying an entity bumps its slot
 *   generation, so stale handles are dead; the slot is reused by later
 *   creates. Every read is generation-validated.
 * - Dead-handle or missing-component access is a validated miss
 *   (nullptr / false), never undefined behaviour.
 * - Component pools are dense `std::vector` storage with a sparse
 *   index from entity slot to dense slot; the dense entry carries the full
 *   handle so a reused slot cannot alias a stale component.
 */

namespace match::ecs {

/** Reserved entity index meaning "no entity"; never a live slot. */
inline constexpr uint32_t kInvalidEntityIndex =
    std::numeric_limits<uint32_t>::max();

/**
 * @brief Upper bound on addressable entity slot indices.
 *
 * Safety cap on the sparse index so a bogus handle cannot force a multi-GB
 * allocation in `ComponentPool::Add`. Far above any real match size (card
 * counts are bounded by deck content, never balance-capped) and shared by
 * `EntityStore::Create` so no live entity can exceed it.
 */
inline constexpr uint32_t kMaxEntityIndex = (1u << 24) - 1;

/**
 * @struct Entity
 * @brief Generational entity handle.
 */
struct Entity {
    uint32_t index = 0;
    uint32_t generation = 0;

    bool operator==(const Entity&) const = default;
};

/**
 * @class IComponentPool
 * @brief Type-erased pool interface used by the component catalog and
 *        generic tooling. Concrete storage is `ComponentPool<T>`.
 *
 * Erased operations mirror the typed ones and use an `Erased` suffix so they
 * never collide with a typed overload (a `T* Get(Entity)` and a
 * `void* Get(Entity)` cannot coexist in one overload set).
 *
 * WARN: the `*Erased` primitives do NOT validate entity liveness or catalog
 *       IDs — they are low-level storage hooks. Code outside the store must
 *       use the checked by-ID wrappers in components.hpp (`AddComponent` /
 *       `GetComponent` / `HasComponent` / `RemoveComponent`) or the typed
 *       `EntityStore` API, never the primitives directly.
 */
class IComponentPool {
public:
    virtual ~IComponentPool() = default;

    /** `std::type_index` of the concrete component type. */
    virtual std::type_index Type() const = 0;

    /** `std::type_info` of the concrete component type. */
    virtual const std::type_info& TypeInfo() const = 0;

    /** Number of stored components. */
    virtual std::size_t Size() const = 0;

    /** True when `entity` currently carries this component. */
    virtual bool HasErased(Entity entity) const = 0;

    /** Pointer to the component, or nullptr on a dead/missing handle. */
    virtual void* GetErased(Entity entity) = 0;

    /** Const pointer to the component, or nullptr on a dead/missing handle. */
    virtual const void* GetErased(Entity entity) const = 0;

    /** Copies `*value` (a `const T`) into the pool; false on null/miss. */
    virtual bool AddErased(Entity entity, const void* value) = 0;

    /** Removes the component; false when absent. */
    virtual bool RemoveErased(Entity entity) = 0;
};

/**
 * @class ComponentPool
 * @brief Dense component storage for one type with sparse handle indexing.
 */
template <typename T>
class ComponentPool final : public IComponentPool {
public:
    struct Entry {
        Entity entity;
        T value;
    };

    /**
     * @brief Adds or updates the component for `entity`.
     *
     * WARN: a pointer returned by `Add`/`Get` is invalidated by any later
     *       `Add` (dense growth) or `Remove` (swap-pop) on the same pool.
     *
     * @return Pointer to the stored value; nullptr when `entity.index` is the
     *         reserved "no slot" sentinel or exceeds `kMaxEntityIndex` (the
     *         sparse-index safety cap).
     */
    T* Add(Entity entity, T value) {
        if (entity.index == kInvalidEntityIndex ||
            entity.index > kMaxEntityIndex) {
            return nullptr;
        }
        if (entity.index >= sparse_.size()) {
            sparse_.resize(static_cast<std::size_t>(entity.index) + 1, kNoSlot);
        }
        const uint32_t slot = sparse_[entity.index];
        if (slot != kNoSlot) {
            if (dense_[slot].entity == entity) {
                dense_[slot].value = std::move(value);
                return &dense_[slot].value;
            }
            RemoveSlot(slot);
        }
        dense_.push_back(Entry{entity, std::move(value)});
        sparse_[entity.index] = static_cast<uint32_t>(dense_.size() - 1);
        return &dense_.back().value;
    }

    /**
     * Pointer to the component, or nullptr on a dead/missing handle.
     *
     * WARN: invalidated by any later `Add`/`Remove` on this pool.
     */
    T* Get(Entity entity) { return Find(entity); }

    /**
     * Const pointer to the component, or nullptr on a dead/missing handle.
     *
     * WARN: invalidated by any later `Add`/`Remove` on this pool.
     */
    const T* Get(Entity entity) const { return Find(entity); }

    /** True when `entity` carries this component. */
    bool Has(Entity entity) const { return Find(entity) != nullptr; }

    /** Removes the component; false when absent. */
    bool Remove(Entity entity) {
        if (entity.index >= sparse_.size()) return false;
        const uint32_t slot = sparse_[entity.index];
        if (slot == kNoSlot || !(dense_[slot].entity == entity)) return false;
        RemoveSlot(slot);
        return true;
    }

    /** Drops every component in the pool. */
    void Clear() {
        dense_.clear();
        sparse_.assign(sparse_.size(), kNoSlot);
    }

    /** Number of stored components. */
    std::size_t Size() const override { return dense_.size(); }

    /** Handles currently carrying this component. */
    std::vector<Entity> Entities() const {
        std::vector<Entity> out;
        out.reserve(dense_.size());
        for (const Entry& entry : dense_) out.push_back(entry.entity);
        return out;
    }

    /** Dense storage; iterate `entry.entity` and `entry.value`. */
    const std::vector<Entry>& Entries() const { return dense_; }

    std::type_index Type() const override { return std::type_index(typeid(T)); }

    const std::type_info& TypeInfo() const override { return typeid(T); }

    bool HasErased(Entity entity) const override {
        return Find(entity) != nullptr;
    }

    void* GetErased(Entity entity) override { return Find(entity); }

    const void* GetErased(Entity entity) const override {
        return Find(entity);
    }

    bool AddErased(Entity entity, const void* value) override {
        if (value == nullptr) return false;
        return Add(entity, *static_cast<const T*>(value)) != nullptr;
    }

    bool RemoveErased(Entity entity) override { return Remove(entity); }

private:
    static constexpr uint32_t kNoSlot = kInvalidEntityIndex;

    T* Find(Entity entity) {
        if (entity.index >= sparse_.size()) return nullptr;
        const uint32_t slot = sparse_[entity.index];
        if (slot == kNoSlot || !(dense_[slot].entity == entity)) return nullptr;
        return &dense_[slot].value;
    }

    const T* Find(Entity entity) const {
        if (entity.index >= sparse_.size()) return nullptr;
        const uint32_t slot = sparse_[entity.index];
        if (slot == kNoSlot || !(dense_[slot].entity == entity)) return nullptr;
        return &dense_[slot].value;
    }

    void RemoveSlot(uint32_t slot) {
        const uint32_t last = static_cast<uint32_t>(dense_.size() - 1);
        const uint32_t removed_index = dense_[slot].entity.index;
        if (slot != last) {
            dense_[slot] = std::move(dense_[last]);
            sparse_[dense_[slot].entity.index] = slot;
        }
        dense_.pop_back();
        sparse_[removed_index] = kNoSlot;
    }

    std::vector<uint32_t> sparse_;  /**< entity index -> dense slot. */
    std::vector<Entry> dense_;      /**< dense component storage. */
};

/**
 * @class EntityStore
 * @brief Owns entity slots and every typed component pool.
 */
class EntityStore {
public:
    EntityStore() = default;
    EntityStore(const EntityStore&) = delete;
    EntityStore& operator=(const EntityStore&) = delete;
    EntityStore(EntityStore&&) = default;
    EntityStore& operator=(EntityStore&&) = default;
    ~EntityStore() = default;

    /** Creates an entity, reusing a freed slot when available. */
    Entity Create();

    /** Destroys an entity, clears its components and bumps its generation. */
    bool Destroy(Entity entity);

    /** True when `entity` matches the live generation of its slot. */
    bool IsAlive(Entity entity) const;

    /** Number of live entities. */
    std::size_t LiveCount() const { return live_count_; }

    /** Handles of every live entity, in ascending slot order. */
    std::vector<Entity> LiveEntities() const;

    /** Calls `fn(Entity)` for every live entity, in ascending slot order. */
    template <typename F>
    void ForEachLive(F&& fn) const {
        for (uint32_t i = 0; i < generations_.size(); ++i) {
            if (alive_[i]) fn(Entity{i, generations_[i]});
        }
    }

    /** Acquire-or-create the typed pool. */
    template <typename T>
    ComponentPool<T>& Pool() {
        const std::type_index key(typeid(T));
        auto it = pools_.find(key);
        if (it != pools_.end()) {
            return static_cast<ComponentPool<T>&>(*it->second);
        }
        auto owned = std::make_unique<ComponentPool<T>>();
        ComponentPool<T>* raw = owned.get();
        pools_.emplace(key, std::move(owned));
        return *raw;
    }

    /** Existing typed pool, or nullptr when none has been created. */
    template <typename T>
    const ComponentPool<T>* Pool() const {
        auto it = pools_.find(std::type_index(typeid(T)));
        if (it == pools_.end()) return nullptr;
        return static_cast<const ComponentPool<T>*>(it->second.get());
    }

    /** Existing erased pool by type, or nullptr. */
    IComponentPool* FindPool(std::type_index type);

    /** Existing erased pool by type, or nullptr. */
    const IComponentPool* FindPool(std::type_index type) const;

    /**
     * Adds or updates `T`; nullptr when `entity` is dead.
     *
     * WARN: the returned pointer is invalidated by any later `Add`/`Remove`
     *       on the same component pool.
     */
    template <typename T>
    T* Add(Entity entity, T value) {
        if (!IsAlive(entity)) return nullptr;
        return Pool<T>().Add(entity, std::move(value));
    }

    /**
     * `T` for a live entity, or nullptr on dead/missing.
     *
     * WARN: invalidated by any later `Add`/`Remove` on the `T` pool.
     */
    template <typename T>
    T* Get(Entity entity) {
        if (!IsAlive(entity)) return nullptr;
        IComponentPool* pool = FindPool(std::type_index(typeid(T)));
        if (pool == nullptr) return nullptr;
        return static_cast<T*>(pool->GetErased(entity));
    }

    /**
     * Const `T` for a live entity, or nullptr on dead/missing.
     *
     * WARN: invalidated by any later `Add`/`Remove` on the `T` pool.
     */
    template <typename T>
    const T* Get(Entity entity) const {
        if (!IsAlive(entity)) return nullptr;
        const IComponentPool* pool = FindPool(std::type_index(typeid(T)));
        if (pool == nullptr) return nullptr;
        return static_cast<const T*>(pool->GetErased(entity));
    }

    /** True when a live entity carries `T`. */
    template <typename T>
    bool Has(Entity entity) const {
        return Get<T>(entity) != nullptr;
    }

    /** Removes `T`; false on dead entity or absent component. */
    template <typename T>
    bool Remove(Entity entity) {
        if (!IsAlive(entity)) return false;
        IComponentPool* pool = FindPool(std::type_index(typeid(T)));
        if (pool == nullptr) return false;
        return pool->RemoveErased(entity);
    }

    /** Live entities carrying `T`, in ascending slot order. */
    template <typename T>
    std::vector<Entity> EntitiesWith() const {
        std::vector<Entity> out;
        const ComponentPool<T>* pool = Pool<T>();
        if (pool == nullptr) return out;
        for (const typename ComponentPool<T>::Entry& entry : pool->Entries()) {
            if (IsAlive(entry.entity)) out.push_back(entry.entity);
        }
        return out;
    }

private:
    std::vector<uint32_t> generations_;  /**< per slot generation. */
    std::vector<uint8_t> alive_;         /**< per slot live flag. */
    std::vector<uint32_t> free_slots_;   /**< freed slots to reuse. */
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>>
        pools_;                          /**< one pool per component type. */
    std::size_t live_count_ = 0;
};

}  // namespace match::ecs
