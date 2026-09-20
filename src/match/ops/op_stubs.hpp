#pragma once

#include <match/ops/ops.hpp>

/**
 * @file op_stubs.hpp
 * @brief Private declarations of the per-family op entry points.
 *
 * One declaration per `OpCatalog()` name so `default_ops.cpp` can wire the
 * catalog to bodies in exactly one place. Each declaration maps to a stub in
 * the matching `src/match/ops/<family>.cpp`; the op layer replaces the bodies
 * with the real implementations, not these signatures.
 *
 * `OpCallOriginal` is special: `call_original` is a `wrap`-mutation marker,
 * not a normal op body.
 */

namespace match::ops::detail {

// --- card and pile ops ------------------------------------------------
OpResult OpDrawCards(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpMoveCard(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpTransferCard(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpPassHands(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpSwapHands(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpRedistributeHands(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpMaterializeCard(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpRemoveCard(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpReplaceCard(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpPeekPile(ecs::EntityStore&, const OpArgs&, OpContext&);

// --- conditions have no OpCatalog rows (see condition_ops.cpp) --------

// --- turn and flow ops ------------------------------------------------
OpResult OpAdvanceTurn(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpSkipTurn(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpReverseDirection(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpExtraTurn(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpRedirectTurn(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpSetTurnTimer(ecs::EntityStore&, const OpArgs&, OpContext&);

// --- status ops -------------------------------------------------------
OpResult OpApplyStatus(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpRemoveStatus(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpModifyStatus(ecs::EntityStore&, const OpArgs&, OpContext&);

// --- prompt op --------------------------------------------------------
OpResult OpPrompt(ecs::EntityStore&, const OpArgs&, OpContext&);

// --- randomness -------------------------------------------------------
OpResult OpRoll(ecs::EntityStore&, const OpArgs&, OpContext&);

// --- visibility ops ---------------------------------------------------
OpResult OpGrantVisibility(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpRevokeVisibility(ecs::EntityStore&, const OpArgs&, OpContext&);

// --- type and restriction ops -----------------------------------------
OpResult OpSetActiveType(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpClearActiveType(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpAddRestriction(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpRemoveRestriction(ecs::EntityStore&, const OpArgs&, OpContext&);

// --- win ops ----------------------------------------------------------
OpResult OpCheckWin(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpDeclareWinner(ecs::EntityStore&, const OpArgs&, OpContext&);
OpResult OpAddPlacement(ecs::EntityStore&, const OpArgs&, OpContext&);

// --- scheduling and meta ---------------------------------------------
OpResult OpEmitSignal(ecs::EntityStore&, const OpArgs&, OpContext&);
/** WARN: `call_original` is a `wrap`-mutation marker, not a normal op body. */
OpResult OpCallOriginal(ecs::EntityStore&, const OpArgs&, OpContext&);

}  // namespace match::ops::detail
