#include <match/ops/ops.hpp>

#include "op_stubs.hpp"

#include <map>
#include <string>

/**
 * @file default_ops.cpp
 * @brief The one place production ops are registered.
 *
 * Every `OpCatalog()` name is registered. Known names map to their per-family
 * stub in `detail`; the `call_original` marker maps to its stub too (it is
 * documented as special there). A catalog name with no family stub falls back
 * to a generic "not implemented" body so registration coverage never drifts
 * from the catalog. The op layer replaces the family bodies; this wiring stays.
 */

namespace match::ops {
namespace {

/** Generic fallback for a catalog name without a dedicated family stub. */
OpResult OpNotImplemented(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace

void RegisterDefaultOps(OpRuntime& runtime) {
    // INFO: the single production registration map. Keyed by OpCatalog()
    //       names; the loop below re-checks coverage against the catalog.
    const std::map<std::string, OpFn> defaults = {
        // card and pile ops.
        {"draw_cards", &detail::OpDrawCards},
        {"draw_until_playable", &detail::OpDrawUntilPlayable},
        {"move_card", &detail::OpMoveCard},
        {"play_card", &detail::OpPlayCard},
        {"transfer_card", &detail::OpTransferCard},
        {"pass_hands", &detail::OpPassHands},
        {"swap_hands", &detail::OpSwapHands},
        {"redistribute_hands", &detail::OpRedistributeHands},
        {"materialize_card", &detail::OpMaterializeCard},
        {"remove_card", &detail::OpRemoveCard},
        {"replace_card", &detail::OpReplaceCard},
        {"peek_pile", &detail::OpPeekPile},
        // turn and flow ops.
        {"advance_turn", &detail::OpAdvanceTurn},
        {"skip_turn", &detail::OpSkipTurn},
        {"reverse_direction", &detail::OpReverseDirection},
        {"extra_turn", &detail::OpExtraTurn},
        {"redirect_turn", &detail::OpRedirectTurn},
        {"set_turn_timer", &detail::OpSetTurnTimer},
        // status ops.
        {"apply_status", &detail::OpApplyStatus},
        {"remove_status", &detail::OpRemoveStatus},
        {"modify_status", &detail::OpModifyStatus},
        // prompt op.
        {"prompt", &detail::OpPrompt},
        // randomness.
        {"roll", &detail::OpRoll},
        // visibility ops.
        {"grant_visibility", &detail::OpGrantVisibility},
        {"revoke_visibility", &detail::OpRevokeVisibility},
        // type and restriction ops.
        {"set_active_type", &detail::OpSetActiveType},
        {"clear_active_type", &detail::OpClearActiveType},
        {"add_restriction", &detail::OpAddRestriction},
        {"remove_restriction", &detail::OpRemoveRestriction},
        // win ops.
        {"check_win", &detail::OpCheckWin},
        {"declare_winner", &detail::OpDeclareWinner},
        {"add_placement", &detail::OpAddPlacement},
        // scheduling and meta.
        {"emit_signal", &detail::OpEmitSignal},
        {"call_original", &detail::OpCallOriginal},
    };

    for (const modload::OpSignature& signature : modload::OpCatalog()) {
        auto it = defaults.find(signature.name);
        runtime.Register(signature.name,
                         it == defaults.end() ? &OpNotImplemented
                                              : it->second);
    }
}

OpRuntime MakeDefaultRuntime() {
    OpRuntime runtime;
    RegisterDefaultOps(runtime);
    return runtime;
}

}  // namespace match::ops
