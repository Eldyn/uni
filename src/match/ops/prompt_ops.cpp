#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

/**
 * @file prompt_ops.cpp
 * @brief Prompt op body.
 *
 * `prompt` is the generic envelope opener. it never blocks: it
 * fills the one op-writable slot `OpContext::input_request`
 * (`InputRequest{kind,target,payload}`) so the Resolver can surface the pause,
 * and returns `OpStatus::kNeedsInput` whose `value` is the envelope
 * (`kind`/`payload`/`timeout_ms`/`default`). `response_schema` is deliberately
 * absent: it is the engine and the session layer's to attach once the kind
 * registry resolves a kind.
 *
 * `target` is a selector; the Resolver owns binding. An unbound/empty
 * selector, a dead target, a missing/empty `kind`, a non-object `payload` or a
 * `timeout` outside its declared 0..600000 range is a fail-safe `kResolved`
 * no-op, never a crash. No event is emitted here: prompt packet assembly
 * (`prompt_open`, prompt id, deadline) belongs.
 */

namespace match::ops::detail {
namespace {

using nlohmann::json;

/** @brief Lower bound of the declared `timeout` `IntArg`. */
constexpr int64_t kMinTimeoutMs = 0;

/** @brief Upper bound of the declared `timeout` `IntArg`. */
constexpr int64_t kMaxTimeoutMs = 600000;

}  // namespace

OpResult OpPrompt(ecs::EntityStore& store, const OpArgs& args,
                  OpContext& ctx) {
    std::string kind;
    if (!args.GetString("kind", kind) || kind.empty()) {
        return OpResult::Resolved();
    }

    const std::optional<ecs::Entity> target = args.FirstEntity("target");
    if (!target.has_value() || !store.IsAlive(*target)) {
        return OpResult::Resolved();
    }

    // INFO: `payload` is opaque to the op; only its shape is checked so a
    //       malformed envelope never reaches a client. Schema validation is
    //       The engine and the session layer's (the kind registry).
    json payload = json::object();
    if (const json* raw_payload = args.Find("payload")) {
        if (!raw_payload->is_object()) return OpResult::Resolved();
        payload = *raw_payload;
    }

    int64_t timeout_ms = 0;
    if (args.Has("timeout")) {
        if (!args.GetInt("timeout", timeout_ms)) return OpResult::Resolved();
        if (timeout_ms < kMinTimeoutMs || timeout_ms > kMaxTimeoutMs) {
            return OpResult::Resolved();
        }
    }

    // INFO: Timeout semantics: `default` binds on timeout when present.
    //       The op passes the declared default through untouched; absence is
    //       JSON null (the graph's `branch.else` is the Resolver's fallback).
    json default_value = nullptr;
    if (const json* raw_default = args.Find("default")) {
        default_value = *raw_default;
    }

    ctx.input_request = InputRequest{};
    ctx.input_request->kind = kind;
    ctx.input_request->target = *target;
    ctx.input_request->payload = payload;

    return OpResult::NeedsInput(json{{"kind", kind},
                                     {"payload", payload},
                                     {"timeout_ms", timeout_ms},
                                     {"default", default_value}});
}

}  // namespace match::ops::detail
