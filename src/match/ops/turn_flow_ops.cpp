#include <match/ops/ops.hpp>

/**
 * @file turn_flow_ops.cpp
 * @brief Turn and flow op bodies.
 *
 * The op layer replaces each `not implemented` body. Timers are the timer
 * layer; these ops only change turn/flow state.
 */

namespace match::ops::detail {

OpResult OpAdvanceTurn(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpSkipTurn(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpReverseDirection(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpExtraTurn(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpRedirectTurn(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpSetTurnTimer(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
