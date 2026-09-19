#include <match/ecs/entity_store.hpp>

#include <cstdint>
#include <typeindex>
#include <vector>

namespace match::ecs {

Entity EntityStore::Create() {
    uint32_t index = 0;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        index = static_cast<uint32_t>(generations_.size());
        generations_.push_back(0);
        alive_.push_back(0);
    }
    alive_[index] = 1;
    ++live_count_;
    return Entity{index, generations_[index]};
}

bool EntityStore::Destroy(Entity entity) {
    if (!IsAlive(entity)) return false;
    for (auto& entry : pools_) {
        entry.second->RemoveErased(entity);
    }
    alive_[entity.index] = 0;
    ++generations_[entity.index];
    free_slots_.push_back(entity.index);
    --live_count_;
    return true;
}

bool EntityStore::IsAlive(Entity entity) const {
    return entity.index < generations_.size() && alive_[entity.index] != 0 &&
           generations_[entity.index] == entity.generation;
}

std::vector<Entity> EntityStore::LiveEntities() const {
    std::vector<Entity> out;
    out.reserve(live_count_);
    for (uint32_t i = 0; i < generations_.size(); ++i) {
        if (alive_[i]) out.push_back(Entity{i, generations_[i]});
    }
    return out;
}

IComponentPool* EntityStore::FindPool(std::type_index type) {
    auto it = pools_.find(type);
    if (it == pools_.end()) return nullptr;
    return it->second.get();
}

const IComponentPool* EntityStore::FindPool(std::type_index type) const {
    auto it = pools_.find(type);
    if (it == pools_.end()) return nullptr;
    return it->second.get();
}

}  // namespace match::ecs
