#pragma once

#include <match/modload/artifacts.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/resolver.hpp>

#include <cstdint>
#include <vector>

/**
 * @file content_wiring.hpp
 * @brief content/condition wiring seams for match assembly.
 *
 * The op layer review found `RegisterDefaultConditions` and
 * `BindTurnsElapsed` had no production caller, and the review found the
 * `CardTagTable` was never populated. The engine owns the full match wiring;
 * this header is the small, explicit seam it (and tests) call so the
 * conditions are installed and card tags are loaded into the per-match
 * registries.
 */

namespace match::wiring {

/**
 * @brief Install the condition evaluators into `registry`.
 *
 * Thin named seam over `ops::RegisterDefaultConditions` so match assembly has
 * one content-wiring entry point rather than reaching into `match::ops`.
 */
void InstallDefaultConditions(resolver::ConditionRegistry& registry);

/**
 * @brief Bind the match's turn counter for the `turns_elapsed` condition.
 *
 * Delegates to `ops::BindTurnsElapsed`; the caller owns the counter
 * because the frozen the store `MatchMeta` has no turns field.
 */
void BindTurnsElapsed(ops::ResolutionFrame& frame, int64_t turns);

/**
 * @brief Build `kind_id -> tags` for loaded card defs into `out`.
 *
 * Replaces `out` with an entry for every loaded card, so
 * `has_card_tag` works at runtime. the table is written to the
 * caller-owned per-match `MatchRegistries::card_tags`, never a process global,
 * so concurrent matches cannot clobber each other. Called once at content
 * load, before resolution starts.
 */
void LoadCardTags(const std::vector<modload::CardDef>& cards,
                  ops::CardTagTable& out);

/**
 * @brief Install the default conditions and load card tags in one call.
 *
 * The single content-load seam the engine can invoke; equivalent to calling
 * `InstallDefaultConditions` and `LoadCardTags` in sequence.
 */
void WireContent(resolver::ConditionRegistry& registry,
                 const std::vector<modload::CardDef>& cards,
                 ops::CardTagTable& out_tags);

}  // namespace match::wiring
