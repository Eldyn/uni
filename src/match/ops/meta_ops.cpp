#include <logger.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/ecs/components.hpp>
#include <match/modload/event_definition_keys.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/vocabulary.hpp>
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

namespace {

/**
 * @brief Check one payload value against its declared field type.
 *
 * A `seat` value is a selector token (for example `@self`); it becomes the
 * username of the first seat the selector names.
 *
 * @return false when the value does not fit the type.
 */
bool CoerceFieldValue(const std::string& type, const nlohmann::json& value,
                      ecs::EntityStore& store, const OpContext& context,
                      nlohmann::json& out) {
    if (type == "seat") {
        if (!value.is_string()) return false;
        const std::string token = value.get<std::string>();
        if (token.empty() || token[0] != '@') return false;
        const std::optional<ecs::Entity> seat =
            context.frame.FirstSelector(token);
        if (!seat.has_value()) return false;
        const ecs::PlayerInfo* info = store.Get<ecs::PlayerInfo>(*seat);
        if (info == nullptr || info->username.empty()) return false;
        out = info->username;
        return true;
    }
    if (type == "color") {
        if (!value.is_string()
            || !modload::IsValidLocalId(value.get<std::string>())) {
            return false;
        }
    } else if (type == "integer") {
        if (!value.is_number_integer()) return false;
    } else if (type == "number") {
        if (!value.is_number()) return false;
    } else if (type == "boolean") {
        if (!value.is_boolean()) return false;
    } else if (type == "string") {
        if (!value.is_string()) return false;
    } else {
        return false;
    }
    out = value;
    return true;
}

}  // namespace

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

OpResult OpEmitEvent(ecs::EntityStore& store, const OpArgs& args,
                     OpContext& ctx) {
    std::string event_id;
    if (!args.GetString("id", event_id) || ctx.registries == nullptr) {
        return OpResult::Resolved();
    }
    const auto definition = ctx.registries->events.find(event_id);
    if (definition == ctx.registries->events.end()) {
        Logger::Warn("[emit_event] dropped: '", event_id, "' is not defined");
        return OpResult::Resolved();
    }
    // WARN: a mod emits only its own events. Without this check a rule could
    //       forge an event of another namespace, such as a `uni:` event.
    if (ctx.mod_id == nullptr
        || *ctx.mod_id != definition->second.namespace_id) {
        Logger::Warn("[emit_event] dropped: mod '",
                     ctx.mod_id == nullptr ? std::string() : *ctx.mod_id,
                     "' cannot emit '", event_id, "'");
        return OpResult::Resolved();
    }

    const nlohmann::json empty_payload = nlohmann::json::object();
    const nlohmann::json* provided =
        args.Has("payload") ? args.GetObject("payload") : &empty_payload;
    if (provided == nullptr) {
        Logger::Warn("[emit_event] dropped: payload of '", event_id,
                     "' is not an object");
        return OpResult::Resolved();
    }

    // INFO: a field that the definition does not declare is dropped. A value
    //       of the wrong type drops the whole event.
    nlohmann::json payload = nlohmann::json::object();
    for (const modload::EventFieldDef& field : definition->second.fields) {
        const auto value = provided->find(field.name);
        if (value == provided->end()) continue;
        nlohmann::json coerced;
        if (!CoerceFieldValue(field.type, *value, store, ctx, coerced)) {
            Logger::Warn("[emit_event] dropped: field '", field.name,
                         "' of '", event_id, "' is not a ", field.type);
            return OpResult::Resolved();
        }
        if (coerced.is_string()
            && coerced.get_ref<const std::string&>().size()
                   > modload::event_keys::kMaxEventStringBytes) {
            Logger::Warn("[emit_event] dropped: field '", field.name,
                         "' of '", event_id, "' is longer than ",
                         modload::event_keys::kMaxEventStringBytes,
                         " bytes");
            return OpResult::Resolved();
        }
        payload[field.name] = std::move(coerced);
    }
    if (payload.dump().size() > modload::event_keys::kMaxEventPayloadBytes) {
        Logger::Warn("[emit_event] dropped: payload of '", event_id,
                     "' is longer than ",
                     modload::event_keys::kMaxEventPayloadBytes, " bytes");
        return OpResult::Resolved();
    }

    const nlohmann::json body = nlohmann::json{
        {"id", event_id},
        {"payload", std::move(payload)},
        {"retention", definition->second.retention}};
    OpResult result = OpResult::Resolved(body);
    nlohmann::json event = MakeEvent("mod_event", body);
    // INFO: a mod event is the first to go when the event budget is full.
    event["droppable"] = true;
    result.events.push_back(std::move(event));
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
