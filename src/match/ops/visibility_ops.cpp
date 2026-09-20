#include <match/ops/ops.hpp>

/**
 * @file visibility_ops.cpp
 * @brief Visibility op bodies.
 *
 * The op layer replaces each `not implemented` body; grants reuse the store
 * `VisibilityGrant` component and aspects from `VisibilityAspects()`.
 */

namespace match::ops::detail {

OpResult OpGrantVisibility(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

OpResult OpRevokeVisibility(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
