#include <match/ops/ops.hpp>

/**
 * @file prompt_ops.cpp
 * @brief Prompt op body.
 *
 * The op layer replaces the `not implemented` body. The real op fills
 * `OpContext::input_request` and returns `OpStatus::kNeedsInput`; the
 * Resolver pauses and the caller supplies the chosen value.
 */

namespace match::ops::detail {

OpResult OpPrompt(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
