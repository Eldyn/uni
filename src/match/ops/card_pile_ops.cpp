#include <match/ops/ops.hpp>

/**
 * @file card_pile_ops.cpp
 * @brief Card and pile op bodies.
 *
 * The op layer replaces each `not implemented` body with the real total,
 * bounded implementation. This file owns only the ten ops.
 */

namespace match::ops::detail {

OpResult OpDrawCards(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpMoveCard(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpTransferCard(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpPassHands(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpSwapHands(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpRedistributeHands(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpMaterializeCard(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpRemoveCard(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpReplaceCard(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpPeekPile(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
