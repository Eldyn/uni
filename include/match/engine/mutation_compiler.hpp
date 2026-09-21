#pragma once

#include <match/modload/artifacts.hpp>

#include <nlohmann/json.hpp>

#include <functional>
#include <set>
#include <string>
#include <vector>

/**
 * @file mutation_compiler.hpp
 * @brief Assembly-time mutation compiler.
 *
 * The compiler is the other half of the ruling: mutations are applied when
 * the match is assembled, not when a graph is walked. For one target graph it
 * consumes the mutations naming that target, in the caller's (mod-list) order,
 * and returns the effective `BehaviorGraph` the Resolver walks. The Resolver
 * therefore needs no mutation awareness, and `call_original` becomes an
 * assembly-time splice marker rather than a runtime op.
 *
 * Phase-1 modes: `replace`, `wrap` (before/after, `call_original` splice) and
 * `veto`. `filter` and restriction-entry targets are deferred:
 * they are inert and log a WARN. A malformed mutation is likewise inert and
 * warned about, never a crash.
 */

namespace match::engine {

/**
 * @struct MutationCompileOptions
 * @brief Out-of-band facts the compiler cannot read off a `MutationDef`.
 *
 * Whether a mutation `target` names a behavior kind or a play-restriction
 * entry is known only to the semantic validator, which tracks
 * `restriction_ids_`. The caller passes that set, so
 * a mutation aimed at a restriction entry is skipped with a WARN instead of
 * being folded into the wrong graph.
 */
struct MutationCompileOptions {
    /** @brief Ids that name play-restriction entries, not behavior kinds. */
    std::set<std::string, std::less<>> restriction_targets;
};

/**
 * @brief Fold `mutations` into one effective graph for `original`.
 *
 * @param original   Target graph as declared (its node list is the base).
 * @param mutations  Mutations naming this target, in mod-list order.
 * @param options    Out-of-band target classification (see above).
 * @return A graph whose `nodes` is the compiled node list and whose `raw` is
 *         `{"nodes": <nodes>}`. The entry node is `nodes.front()`.
 *
 * Ordering: execution order equals mod-list order in both directions
 * Before-wraps and `replace` fold so the earliest mod is
 * outermost (`[A,B]` before -> `A_pre, B_pre, core`); after-wraps fold so the
 * earliest mod runs first after the core (`[A,B]` after -> `core, A_post,
 * B_post`). Every `veto` is applied after that fold, so it guards the
 * effective graph whichever side of a `replace`/`wrap` it appears on.
 */
modload::BehaviorGraph CompileMutations(
    const modload::BehaviorGraph& original,
    const std::vector<const modload::MutationDef*>& mutations,
    const MutationCompileOptions& options = MutationCompileOptions{});

}  // namespace match::engine
