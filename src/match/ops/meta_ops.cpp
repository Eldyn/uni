#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

/**
 * @file meta_ops.cpp
 * @brief Scheduling and meta op bodies.
 *
 * `emit_signal` is the custom named-packet seam (the Lua/plugin and VFX hint
 * carrier): it emits a `signal` descriptor carrying the declared `{name,
 * payload}`. Audience filtering is the view layer's concern; this op only
 * shapes the descriptor. `call_original` is not a normal op — it is the
 * `wrap`-mutation marker and keeps its explanatory error. The `schedule` node
 * form of is a Resolver node type, not an `OpCatalog()` row.
 */

namespace match::ops::detail {

OpResult OpEmitSignal(ecs::EntityStore& store, const OpArgs& args,
                      OpContext& ctx) {
    (void)store;
    (void)ctx;

    std::string name;
    if (!args.GetString("name", name) || name.empty()) {
        return OpResult::Resolved();
    }

    nlohmann::json payload = nlohmann::json::object();
    if (args.Has("payload")) {
        const nlohmann::json* provided = args.GetObject("payload");
        if (provided == nullptr) return OpResult::Resolved();
        payload = *provided;
    }

    // INFO: The descriptor body is `{name, payload}`; the view layer
    //       attaches the audience/sequence metadata and filters the packet.
    const nlohmann::json signal =
        nlohmann::json{{"name", name}, {"payload", std::move(payload)}};
    OpResult result = OpResult::Resolved(signal);
    result.events.push_back(MakeEvent("signal", signal));
    return result;
}

/**
 * WARN: `call_original` is special. It is not a normal op body; it is only
 *       meaningful inside a `wrap` mutation, where it invokes the wrapped
 *       original behavior. The stub keeps it registered for
 *       catalog parity and returns an explanatory error; the Resolver's
 *       `wrap` path is what gives it meaning.
 */
OpResult OpCallOriginal(ecs::EntityStore&, const OpArgs&, OpContext&) {
    return OpResult::Error("not implemented");
}

}  // namespace match::ops::detail
