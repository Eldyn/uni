#include <doctest/doctest.h>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>

#include <cstdint>
#include <string>
#include <typeindex>
#include <vector>

using namespace match::ecs;

namespace {

// INFO: minimal data-only components local to this suite; the real catalog
//       structs are exercised separately.
struct Position {
    int x = 0;
    int y = 0;
};

struct Label {
    std::string text;
};

struct Health {
    int hp = 0;
};

bool ContainsEntity(const std::vector<Entity>& entities, Entity needle) {
    for (const Entity& e : entities) {
        if (e == needle) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("entity_store: create read update remove components") {
    EntityStore store;
    Entity e = store.Create();
    CHECK(store.IsAlive(e));
    CHECK(store.LiveCount() == 1);

    Position* pos = store.Add<Position>(e, Position{1, 2});
    REQUIRE(pos != nullptr);
    CHECK(pos->x == 1);
    CHECK(pos->y == 2);
    CHECK(store.Has<Position>(e));

    Position* read = store.Get<Position>(e);
    REQUIRE(read != nullptr);
    CHECK(read->x == 1);
    CHECK(read->y == 2);

    Position* updated = store.Add<Position>(e, Position{3, 4});
    REQUIRE(updated != nullptr);
    CHECK(store.Get<Position>(e)->x == 3);
    CHECK(store.Pool<Position>().Size() == 1);

    Label* label = store.Add<Label>(e, Label{"ace"});
    REQUIRE(label != nullptr);
    CHECK(store.Has<Label>(e));

    CHECK(store.Remove<Position>(e));
    CHECK_FALSE(store.Has<Position>(e));
    CHECK(store.Get<Position>(e) == nullptr);
    CHECK_FALSE(store.Remove<Position>(e));

    CHECK(store.Remove<Label>(e));
    CHECK(store.EntitiesWith<Position>().empty());
    CHECK(store.EntitiesWith<Label>().empty());
    CHECK(store.EntitiesWith<Health>().empty());
}

TEST_CASE("entity_store: generational handle invalidates on destroy and reuse") {
    EntityStore store;
    Entity first = store.Create();
    store.Add<Health>(first, Health{10});
    REQUIRE(store.Get<Health>(first) != nullptr);

    CHECK(store.Destroy(first));
    CHECK_FALSE(store.IsAlive(first));
    CHECK(store.Get<Health>(first) == nullptr);
    CHECK_FALSE(store.Has<Health>(first));
    CHECK_FALSE(store.Destroy(first));

    Entity reused = store.Create();
    CHECK(reused.index == first.index);
    CHECK(reused.generation != first.generation);
    CHECK(store.IsAlive(reused));

    // INFO: components do not survive the Destroy that freed the slot.
    CHECK(store.Get<Health>(reused) == nullptr);
    CHECK_FALSE(store.Has<Health>(reused));

    Health* hp = store.Add<Health>(reused, Health{20});
    REQUIRE(hp != nullptr);
    CHECK(hp->hp == 20);
    CHECK(store.Get<Health>(reused)->hp == 20);

    // INFO: the stale handle stays dead even though the slot is occupied.
    CHECK_FALSE(store.IsAlive(first));
    CHECK(store.Get<Health>(first) == nullptr);
    CHECK_FALSE(store.Destroy(first));
    CHECK(store.LiveCount() == 1);
}

TEST_CASE("entity_store: never-existing and out-of-range handles are misses") {
    EntityStore store;

    Entity ghost{9999, 0};
    CHECK_FALSE(store.IsAlive(ghost));
    CHECK(store.Get<Position>(ghost) == nullptr);
    CHECK_FALSE(store.Has<Position>(ghost));
    CHECK_FALSE(store.Remove<Position>(ghost));

    Entity real = store.Create();
    Entity wrong_generation{real.index, real.generation + 7};
    CHECK_FALSE(store.IsAlive(wrong_generation));
    CHECK(store.Get<Position>(wrong_generation) == nullptr);
    CHECK_FALSE(store.Has<Position>(wrong_generation));

    // INFO: index 0 is a real slot, generation 0 is a real generation, but the
    //       pair must match the live slot to be valid.
    Entity before_creation{0, 0};
    CHECK(store.IsAlive(before_creation));
    store.Destroy(real);
    CHECK_FALSE(store.IsAlive(before_creation));
}

TEST_CASE("entity_store: missing component access is a miss, not UB") {
    EntityStore store;
    Entity e = store.Create();

    CHECK(store.Get<Position>(e) == nullptr);
    CHECK_FALSE(store.Has<Position>(e));
    CHECK_FALSE(store.Remove<Position>(e));
    CHECK(store.Pool<Position>().Size() == 0);

    REQUIRE(store.Add<Position>(e, Position{9, 9}) != nullptr);
    CHECK(store.Get<Label>(e) == nullptr);
    CHECK_FALSE(store.Has<Label>(e));
    CHECK(store.Pool<Label>().Size() == 0);
    CHECK(store.Get<Position>(e) != nullptr);

    CHECK(store.EntitiesWith<Label>().empty());
}

TEST_CASE("entity_store: iteration covers only live entities") {
    EntityStore store;
    Entity a = store.Create();
    Entity b = store.Create();
    Entity c = store.Create();
    store.Add<Position>(a, Position{1, 1});
    store.Add<Position>(b, Position{2, 2});
    store.Add<Position>(c, Position{3, 3});

    REQUIRE(store.Destroy(b));
    CHECK(store.LiveCount() == 2);

    std::vector<Entity> live = store.LiveEntities();
    CHECK(live.size() == 2);
    CHECK(ContainsEntity(live, a));
    CHECK(ContainsEntity(live, c));
    CHECK_FALSE(ContainsEntity(live, b));

    std::vector<Entity> visited;
    store.ForEachLive([&visited](Entity e) { visited.push_back(e); });
    CHECK(visited.size() == 2);
    CHECK(ContainsEntity(visited, a));
    CHECK(ContainsEntity(visited, c));

    std::vector<Entity> with_position = store.EntitiesWith<Position>();
    CHECK(with_position.size() == 2);
    CHECK(ContainsEntity(with_position, a));
    CHECK(ContainsEntity(with_position, c));
    CHECK_FALSE(ContainsEntity(with_position, b));
}

TEST_CASE("entity_store: destroying an entity clears every component pool") {
    EntityStore store;
    Entity e = store.Create();
    store.Add<Position>(e, Position{1, 1});
    store.Add<Label>(e, Label{"x"});
    store.Add<Health>(e, Health{5});

    REQUIRE(store.Destroy(e));
    CHECK(store.Pool<Position>().Size() == 0);
    CHECK(store.Pool<Label>().Size() == 0);
    CHECK(store.Pool<Health>().Size() == 0);
}

TEST_CASE("entity_store: catalog registers every phase-1 component id") {
    const ComponentCatalog& catalog = ComponentCatalog::Instance();

    struct Expected {
        const char* id;
        std::type_index type;
    };
    const Expected expected[] = {
        {"card_identity", std::type_index(typeid(CardIdentity))},
        {"in_zone", std::type_index(typeid(InZone))},
        {"face_spec", std::type_index(typeid(FaceSpec))},
        {"card_behavior", std::type_index(typeid(CardBehavior))},
        {"auto_trigger", std::type_index(typeid(AutoTrigger))},
        {"window_spec", std::type_index(typeid(WindowSpec))},
        {"player_info", std::type_index(typeid(PlayerInfo))},
        {"hand", std::type_index(typeid(Hand))},
        {"turn_state", std::type_index(typeid(TurnState))},
        {"status", std::type_index(typeid(Status))},
        {"status_list", std::type_index(typeid(StatusList))},
        {"visibility_grant", std::type_index(typeid(VisibilityGrant))},
        {"draw_debt", std::type_index(typeid(DrawDebt))},
        {"play_restriction", std::type_index(typeid(PlayRestriction))},
        {"active_type_req", std::type_index(typeid(ActiveTypeReq))},
        {"window_state", std::type_index(typeid(WindowState))},
        {"pending_schedule", std::type_index(typeid(PendingSchedule))},
        {"match_meta", std::type_index(typeid(MatchMeta))},
        {"placements", std::type_index(typeid(Placements))},
        {"rng_state", std::type_index(typeid(RngState))},
        {"pile_contents", std::type_index(typeid(PileContents))},
    };

    for (const Expected& entry : expected) {
        const ComponentType* type = catalog.Find(entry.id);
        REQUIRE(type != nullptr);
        CHECK(type->id == entry.id);
        CHECK(type->type == entry.type);
        CHECK(catalog.Contains(entry.id));
    }
    CHECK(catalog.Types().size() == 21);

    CHECK(catalog.Find("no_such_component") == nullptr);
    CHECK_FALSE(catalog.Contains("no_such_component"));
    CHECK(catalog.Find("") == nullptr);
}

TEST_CASE("entity_store: catalog by-id access validates entity liveness") {
    EntityStore store;
    Entity e = store.Create();

    Hand hand;
    hand.cards.push_back(Entity{7, 0});
    CHECK(AddComponent(store, e, "hand", &hand));
    CHECK(HasComponent(store, e, "hand"));
    CHECK(store.Pool<Hand>().Size() == 1);

    const void* raw = GetComponent(store, e, "hand");
    REQUIRE(raw != nullptr);
    const Hand* read = static_cast<const Hand*>(raw);
    REQUIRE(read->cards.size() == 1);
    CHECK(read->cards.front().index == 7);

    void* mut = GetComponent(store, e, "hand");
    REQUIRE(mut != nullptr);
    static_cast<Hand*>(mut)->cards.push_back(Entity{8, 0});
    CHECK(store.Get<Hand>(e)->cards.size() == 2);

    // INFO: unknown IDs and null values are structured misses.
    CHECK_FALSE(HasComponent(store, e, "no_such_component"));
    CHECK(GetComponent(store, e, "no_such_component") == nullptr);
    CHECK_FALSE(AddComponent(store, e, "no_such_component", &hand));
    CHECK_FALSE(RemoveComponent(store, e, "no_such_component"));
    CHECK_FALSE(AddComponent(store, e, "hand", nullptr));

    REQUIRE(store.Destroy(e));

    // INFO: a dead handle must never store a ghost via the erased seam.
    CHECK_FALSE(AddComponent(store, e, "hand", &hand));
    CHECK_FALSE(HasComponent(store, e, "hand"));
    CHECK(GetComponent(store, e, "hand") == nullptr);
    CHECK_FALSE(RemoveComponent(store, e, "hand"));
    CHECK(store.Pool<Hand>().Size() == 0);
}

TEST_CASE("entity_store: unchecked pool primitives reject bogus indices") {
    EntityStore store;
    Entity e = store.Create();

    const ComponentType* hand_type = ComponentCatalog::Instance().Find("hand");
    REQUIRE(hand_type != nullptr);
    IComponentPool& pool = hand_type->Acquire(store);
    CHECK(&pool == static_cast<IComponentPool*>(&store.Pool<Hand>()));

    Hand hand;
    CHECK(pool.AddErased(e, &hand));
    CHECK(pool.Size() == 1);
    CHECK(pool.RemoveErased(e));
    CHECK_FALSE(pool.HasErased(e));

    // INFO: sentinel / over-cap indices must not resize the sparse index or
    //       store anything.
    CHECK_FALSE(pool.AddErased(Entity{kInvalidEntityIndex, 0}, &hand));
    CHECK_FALSE(pool.AddErased(Entity{kMaxEntityIndex + 1, 0}, &hand));
    CHECK(pool.Size() == 0);
    CHECK(kMaxEntityIndex < kInvalidEntityIndex);
}

TEST_CASE("entity_store: compact card bound constants") {
    CHECK(kMaxMods == 256);
    CHECK(kMaxKindsPerMod == 4096);
    CHECK(kMaxInstancesPerKind == 4096);
    CHECK(kCompactCardModBits == 8);
    CHECK(kCompactCardKindBits == 12);
    CHECK(kCompactCardInstanceBits == 12);
}

TEST_CASE("entity_store: compact card packing and unpacking") {
    auto card = MakeCompactCard(1, 2, 3);
    REQUIRE(card.has_value());
    CHECK(card->ModIndex() == 1);
    CHECK(card->KindIndex() == 2);
    CHECK(card->InstanceId() == 3);

    // INFO: bits 31..24 mod, 23..12 kind, 11..0 instance.
    CHECK(card->bits == ((1u << 24) | (2u << 12) | 3u));

    auto boundary = MakeCompactCard(kMaxMods - 1, kMaxKindsPerMod - 1,
                                    kMaxInstancesPerKind - 1);
    REQUIRE(boundary.has_value());
    CHECK(boundary->ModIndex() == 255);
    CHECK(boundary->KindIndex() == 4095);
    CHECK(boundary->InstanceId() == 4095);

    auto zero = MakeCompactCard(0, 0, 0);
    REQUIRE(zero.has_value());
    CHECK(zero->bits == 0u);

    CHECK_FALSE(MakeCompactCard(kMaxMods, 0, 0).has_value());
    CHECK_FALSE(MakeCompactCard(0, kMaxKindsPerMod, 0).has_value());
    CHECK_FALSE(MakeCompactCard(0, 0, kMaxInstancesPerKind).has_value());

    CompactCardV2 lhs{card.value()};
    CompactCardV2 rhs{card.value()};
    CHECK(lhs == rhs);
    CompactCardV2 other = MakeCompactCard(4, 5, 6).value();
    CHECK_FALSE(lhs == other);
}
