#include <match/ops/ops.hpp>

/**
 * @file win_ops.cpp
 * @brief Win op bodies.
 *
 * The op layer replaces each `not implemented` body; `check_win` fires the
 * `win_check` hook and placements reuse the `Placements` component.
 */

namespace match::ops::detail {

OpResult OpCheckWin(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpDeclareWinner(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpAddPlacement(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
