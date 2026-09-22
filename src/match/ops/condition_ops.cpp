#include <match/ops/ops.hpp>

/**
 * @file condition_ops.cpp
 * @brief Family home for condition ops.
 *
 * INFO: Conditions are not effect ops and have no rows in
 *       `OpCatalog()`. The op layer evaluates them through `ConditionCatalog()`
 *       inside the Resolver's `branch` / `where` / `veto` paths. The op runtime
 *       never registers them, so this family file intentionally defines no
 * stub.
 */

namespace match::ops::detail {

[[maybe_unused]] constexpr char kConditionFamilyHasNoOps[] =
    "conditions are evaluated, not registered";

}  // namespace match::ops::detail
