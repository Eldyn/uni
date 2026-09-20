#include <match/ops/ops.hpp>

/**
 * @file restriction_ops.cpp
 * @brief Type and restriction op bodies.
 *
 * The op layer replaces each `not implemented` body; the pipeline reuses the
 * store `PlayRestriction` / `RestrictionEntry` components and the allow/deny
 * semantics.
 */

namespace match::ops::detail {

OpResult OpSetActiveType(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpClearActiveType(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpAddRestriction(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpRemoveRestriction(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
