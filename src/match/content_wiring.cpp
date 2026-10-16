#include <match/content_wiring.hpp>

#include <match/ops/op_helpers.hpp>

#include <string>
#include <utility>
#include <vector>

/**
 * @file content_wiring.cpp
 * @brief content/condition wiring bodies.
 */

namespace match::wiring {

void InstallDefaultConditions(resolver::ConditionRegistry& registry) {
    ops::RegisterDefaultConditions(registry);
}

void BindTurnsElapsed(ops::ResolutionFrame& frame, int64_t turns) {
    ops::BindTurnsElapsed(frame, turns);
}

void LoadCardTags(const std::vector<modload::CardDef>& cards,
                  ops::CardTagTable& out) {
    ops::CardTagTable table;
    for (const modload::CardDef& card : cards) {
        if (card.kind_id.empty()) continue;
        table[card.kind_id] = card.tags;
    }
    out = std::move(table);
}

void WireContent(resolver::ConditionRegistry& registry,
                 const std::vector<modload::CardDef>& cards,
                 ops::CardTagTable& out_tags) {
    InstallDefaultConditions(registry);
    LoadCardTags(cards, out_tags);
}

void LoadTables(const std::vector<modload::TableDef>& tables,
                ops::TableRegistry& out) {
    ops::TableRegistry registry;
    for (const modload::TableDef& table : tables) {
        if (table.table_id.empty()) continue;
        registry[table.table_id] = table;
    }
    out = std::move(registry);
}

void LoadEntities(const std::vector<modload::EntityDef>& entities,
                  modload::EntityRegistry& out) {
    modload::EntityRegistry registry;
    for (const modload::EntityDef& entity : entities) {
        if (entity.entity_id.empty()) continue;
        registry[entity.entity_id] = entity;
    }
    out = std::move(registry);
}

void LoadPresentations(
    const std::vector<modload::PresentationDef>& presentations,
    modload::PresentationRegistry& out) {
    modload::PresentationRegistry registry;
    for (const modload::PresentationDef& presentation : presentations) {
        if (presentation.presentation_id.empty()) continue;
        registry[presentation.presentation_id] = presentation;
    }
    out = std::move(registry);
}

void LoadEvents(const std::vector<modload::EventDef>& events,
                modload::EventRegistry& out) {
    modload::EventRegistry registry;
    for (const modload::EventDef& event : events) {
        if (event.event_id.empty()) continue;
        registry[event.event_id] = event;
    }
    out = std::move(registry);
}

void LoadStreaks(const std::vector<modload::StreakDef>& streaks,
                 modload::StreakRegistry& out) {
    modload::StreakRegistry registry;
    for (const modload::StreakDef& streak : streaks) {
        if (streak.streak_id.empty()) continue;
        registry[streak.streak_id] = streak;
    }
    out = std::move(registry);
}

void LoadDerived(const std::vector<modload::DerivedEventDef>& derived,
                 modload::DerivedRegistry& out) {
    modload::DerivedRegistry registry;
    for (const modload::DerivedEventDef& definition : derived) {
        if (definition.derived_id.empty()) continue;
        registry[definition.derived_id] = definition;
    }
    out = std::move(registry);
}

void LoadBindings(const std::vector<modload::BindingDef>& bindings,
                  modload::BindingRegistry& out) {
    modload::BindingRegistry registry;
    for (const modload::BindingDef& binding : bindings) {
        if (binding.binding_id.empty()) continue;
        registry[binding.binding_id] = binding;
    }
    out = std::move(registry);
}

}  // namespace match::wiring
