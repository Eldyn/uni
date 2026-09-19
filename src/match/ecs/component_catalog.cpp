#include <match/ecs/components.hpp>

#include <match/ecs/entity_store.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <typeindex>
#include <vector>

namespace match::ecs {
namespace {

/** Acquire-or-create the typed pool through its erased interface. */
template <typename T>
IComponentPool& AcquirePool(EntityStore& store) {
    return store.template Pool<T>();
}

struct Registration {
    std::string_view id;
    std::type_index type;
    IComponentPool& (*acquire)(EntityStore&);
};

/**
 * INFO: The full phase-1 catalog. Order here
 *       is the catalog's registration order and is stable.
 */
const std::array<Registration, 20>& Registrations() {
    static const std::array<Registration, 20> kRegistrations = {{
        {"card_identity", std::type_index(typeid(CardIdentity)),
         &AcquirePool<CardIdentity>},
        {"in_zone", std::type_index(typeid(InZone)), &AcquirePool<InZone>},
        {"face_spec", std::type_index(typeid(FaceSpec)),
         &AcquirePool<FaceSpec>},
        {"card_behavior", std::type_index(typeid(CardBehavior)),
         &AcquirePool<CardBehavior>},
        {"auto_trigger", std::type_index(typeid(AutoTrigger)),
         &AcquirePool<AutoTrigger>},
        {"window_spec", std::type_index(typeid(WindowSpec)),
         &AcquirePool<WindowSpec>},
        {"player_info", std::type_index(typeid(PlayerInfo)),
         &AcquirePool<PlayerInfo>},
        {"hand", std::type_index(typeid(Hand)), &AcquirePool<Hand>},
        {"turn_state", std::type_index(typeid(TurnState)),
         &AcquirePool<TurnState>},
        {"status", std::type_index(typeid(Status)), &AcquirePool<Status>},
        {"visibility_grant", std::type_index(typeid(VisibilityGrant)),
         &AcquirePool<VisibilityGrant>},
        {"draw_debt", std::type_index(typeid(DrawDebt)),
         &AcquirePool<DrawDebt>},
        {"play_restriction", std::type_index(typeid(PlayRestriction)),
         &AcquirePool<PlayRestriction>},
        {"active_type_req", std::type_index(typeid(ActiveTypeReq)),
         &AcquirePool<ActiveTypeReq>},
        {"window_state", std::type_index(typeid(WindowState)),
         &AcquirePool<WindowState>},
        {"pending_schedule", std::type_index(typeid(PendingSchedule)),
         &AcquirePool<PendingSchedule>},
        {"match_meta", std::type_index(typeid(MatchMeta)),
         &AcquirePool<MatchMeta>},
        {"placements", std::type_index(typeid(Placements)),
         &AcquirePool<Placements>},
        {"rng_state", std::type_index(typeid(RngState)),
         &AcquirePool<RngState>},
        {"pile_contents", std::type_index(typeid(PileContents)),
         &AcquirePool<PileContents>},
    }};
    return kRegistrations;
}

}  // namespace

ComponentCatalog::ComponentCatalog() {
    const auto& registrations = Registrations();
    types_.reserve(registrations.size());
    for (const Registration& registration : registrations) {
        types_.push_back(ComponentType{registration.id, registration.type,
                                       registration.acquire});
        by_id_.emplace(registration.id, types_.size() - 1);
    }
}

const ComponentCatalog& ComponentCatalog::Instance() {
    static const ComponentCatalog kInstance;
    return kInstance;
}

const ComponentType* ComponentCatalog::Find(std::string_view id) const {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) return nullptr;
    return &types_[it->second];
}

bool ComponentCatalog::Contains(std::string_view id) const {
    return by_id_.find(id) != by_id_.end();
}

std::vector<std::string_view> ComponentCatalog::Ids() const {
    std::vector<std::string_view> ids;
    ids.reserve(types_.size());
    for (const ComponentType& type : types_) ids.push_back(type.id);
    return ids;
}

}  // namespace match::ecs
