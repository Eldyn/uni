#include "../include/action_router.hpp"
#include "../include/logger.hpp"
#include <common/ws.hpp>

ActionRouter& ActionRouter::On(const std::string& action, ActionHandler handler) {
    if (handlers_.count(action)) {
        Logger::Warn("[ActionRouter] Overwriting existing handler for: " + action);
    }

    handlers_[action] = std::move(handler);

    return *this;
}

ActionRouter& ActionRouter::OnAny(ActionHandler handler) {
    wildcards_.push_back(std::move(handler));
    return *this;
}

bool ActionRouter::Dispatch(WsContext ctx, const json& msg) const {
    std::string action;
    auto action_it = msg.find("action");
    if (action_it != msg.end() && action_it->is_string()) {
        action = action_it->get<std::string>();
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

    auto it = handlers_.find(action);
    if (it == handlers_.end()) {
        return false;   // no handler registered, WebServer will log this
    }

    try {
        it->second(ctx, msg);
    } catch (const std::exception& e) {
        Logger::Warn("[ActionRouter] Exception handling action '", action, "': ", e.what());
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
