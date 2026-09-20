#include <match/ops/ops.hpp>

/**
 * @file random_ops.cpp
 * @brief Roll op body.
 *
 * The op layer replaces the `not implemented` body; RNG state is the timer
 * layer. Every roll is logged in the event stream, which is why this is a
 * normal op and not a Resolver primitive.
 */

namespace match::ops::detail {

OpResult OpRoll(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
