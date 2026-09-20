#include <match/ops/ops.hpp>

/**
 * @file status_ops.cpp
 * @brief Status op bodies.
 *
 * The op layer replaces each `not implemented` body; duration/stack semantics
 *  are and reuse the `Status` component.
 */

namespace match::ops::detail {

OpResult OpApplyStatus(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpRemoveStatus(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpModifyStatus(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
