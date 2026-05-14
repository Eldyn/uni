#include "../include/action_router.hpp"
#include "../include/logger.hpp"

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
    // Run wildcards first — any returning false aborts the entire chain.
    for (const auto& wildcard : wildcards_) {
        if (!wildcard(ctx, msg)) {
            return false;
        }
    }

    const std::string action = msg.value("action", "");
    auto it = handlers_.find(action);

    if (it == handlers_.end()) {
        return false;   // no handler registered — WebServer will log this
    }

    it->second(ctx, msg);
    return true;
}
