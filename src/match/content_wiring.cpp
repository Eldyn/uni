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

void LoadCardTags(const std::vector<modload::CardDef>& cards) {
    ops::CardTagTable table;
    for (const modload::CardDef& card : cards) {
        if (card.kind_id.empty()) continue;
        table[card.kind_id] = card.tags;
    }
    ops::SetCardTagTable(std::move(table));
}

void WireContent(resolver::ConditionRegistry& registry,
                 const std::vector<modload::CardDef>& cards) {
    InstallDefaultConditions(registry);
    LoadCardTags(cards);
}

}  // namespace match::wiring
