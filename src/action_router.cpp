#include "../include/action_router.hpp"
#include "../include/logger.hpp"
#include <common/ws.hpp>

ActionRouter& ActionRouter::On(const std::string& action, ActionHandler handler) {
    if (handlers_.count(action)) {
        Logger::Warn("[ActionRouter] Overwriting existing handler for: " + action);
    }

    handlers_[action] = std::move(handler);
    // Keep the hashed fast path in sync (overwrite on dup registration, same
    // as the string map above).
    hash_handlers_[Fnv1a(action)] = {action, handlers_[action]};

    return *this;
}

ActionRouter& ActionRouter::OnAny(ActionHandler handler) {
    wildcards_.push_back(std::move(handler));
    return *this;
}

bool ActionRouter::Dispatch(WsContext ctx, const json& msg) const {
    std::string_view action;
    auto action_it = msg.find("action");
    if (action_it != msg.end() && action_it->is_string()) {
        // View into the DOM's own storage: no std::string alloc on dispatch.
        action = action_it->get_ref<const std::string&>();
    }
    return Dispatch(ctx, msg, Fnv1a(action));
}

bool ActionRouter::Dispatch(WsContext ctx, const json& msg, uint32_t action_hash) const {
    std::string_view action;
    auto action_it = msg.find("action");
    if (action_it != msg.end() && action_it->is_string()) {
        action = action_it->get_ref<const std::string&>();
    }

    // Run wildcards first, any returning false aborts the entire chain.
    for (const auto& wildcard : wildcards_) {
        try {
            if (!wildcard(ctx, msg)) {
                return false;
            }
        } catch (const std::exception& e) {
            Logger::Warn("[ActionRouter] Exception in wildcard handler: ", e.what());
            return false;
        }
    }

    // Hashed fast path: no std::string construction on hit. The stored name
    // is verified so a hash collision (or an escape-encoded raw span) can
    // never misroute; mismatches fall back to the exact string map.
    const ActionHandler* handler = nullptr;
    if (auto hh = hash_handlers_.find(action_hash);
        hh != hash_handlers_.end() && hh->second.first == action) {
        handler = &hh->second.second;
    } else {
        auto it = handlers_.find(std::string(action));
        if (it == handlers_.end()) {
            return false;   // no handler registered, WebServer will log this
        }
        handler = &it->second;
    }

    try {
        (*handler)(ctx, msg);
    } catch (const std::exception& e) {
        Logger::Warn("[ActionRouter] Exception handling action '", std::string(action), "': ", e.what());
        if (ctx.socket) {
            const std::string request_id = ws::GetOr<std::string>(msg, "request_id", "");
            // INFO: the exception text is logged above, never echoed: it can
            //       carry library internals.
            ws::SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                          request_id, "Malformed request");
        }
    }
    return true;
}
