#include <match/ops/ops.hpp>

/**
 * @file meta_ops.cpp
 * @brief Scheduling and meta op bodies.
 *
 * The op layer replaces each `not implemented` body. `emit_signal` is the
 * custom packet seam; `call_original` is special (see below). The `schedule`
 * node form of is a Resolver node type, not an `OpCatalog()` row.
 */

namespace match::ops::detail {

OpResult OpEmitSignal(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

/**
 * WARN: `call_original` is special. It is not a normal op body; it is only
 *       meaningful inside a `wrap` mutation, where it invokes the wrapped
 *       original behavior. The stub keeps it registered for catalog parity;
 *       The op layer owns the real wrapping body.
 */
OpResult OpCallOriginal(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
