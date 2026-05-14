#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <router.hpp>
#include <websocket_context.hpp>

using json = nlohmann::json;

// Handler signature: returns bool.
//   true  — continue dispatch (pass through to the specific handler).
//   false — abort dispatch (wildcard swallowed the message, e.g. auth guard).
//
// Both wildcards ("*") and specific handlers share this signature so that
// wildcards can act as middleware and abort the chain when needed.
using ActionHandler = std::function<bool(WsContext, const json&)>;

// Routes incoming WebSocket JSON messages to registered handlers by
// inspecting the "action" field.
//
// Usage:
//   router.On("join",  handler);       // specific action
//   router.On("*",     middleware);    // wildcard — runs before every action
//
// Wildcards run in registration order. If any wildcard returns false,
// the specific handler is never called.
//
// Modules self-register in their constructors:
//   explicit GameModule(ActionRouter& router) {
//       router.On("play_card", [this](...){ ... });
//   }
class ActionRouter : public Router {
public:
    ActionRouter() = default;
    // Register a handler. 
    // Returns *this for chaining.
    ActionRouter& On(const std::string& action, ActionHandler handler);

    // Registers a wildcard handler, which can return a boolean
    // to choose whether to continue dispatching or not.
    ActionRouter& OnAny(ActionHandler handler);

    // Dispatch a message. Called by WebServer on every incoming WS frame.
    // Returns true if a specific handler was found and called,
    // false if no matching handler exists (after running wildcards).
    bool Dispatch(WsContext ctx, const json& msg) const;

private:
    std::unordered_map<std::string, ActionHandler> handlers_;

    // Wildcards stored separately — run before every specific handler.
    std::vector<ActionHandler> wildcards_;
};
