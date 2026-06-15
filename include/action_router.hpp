#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <router.hpp>
#include <websocket_context.hpp>

/**
 * @file action_router.hpp
 * @brief Implementation of the router for dispatching WebSocket messages.
 * * Routes incoming JSON messages to the appropriate handlers by inspecting
 * the "action" field within the payload.
 */

using json = nlohmann::json;

/**
 * @typedef ActionHandler
 * @brief Signature for WebSocket action handlers.
 * * Returns a boolean:
 * - `true`: Continue the dispatch (pass to the next specific handler).
 * - `false`: Stop the dispatch (used by middleware/wildcards to block the action, e.g. authentication guard).
 * @tag ACT-RTR-TYP-001
 */
using ActionHandler = std::function<bool(WsContext, const json&)>;

/**
 * @class ActionRouter
 * @brief Dispatches WebSocket messages to the various controllers based on the action type.
 * * Supports the use of "wildcards" (middleware) that are executed before every specific
 * action. If a wildcard returns `false`, the specific handler is never called.
 * Inherits from `Router` to disable copying.
 * @tag ACT-RTR-CLS-001
 */
class ActionRouter : public Router {
public:
    ActionRouter() = default;

    /**
     * @brief Registers a handler for a specific action.
     * @param action The name of the action (e.g. "play_card").
     * @param handler The function to execute when the action is received.
     * @return ActionRouter& Reference to itself for chaining.
     * @tag ACT-RTR-MTH-001
     */
    ActionRouter& On(const std::string& action, ActionHandler handler);

    /**
     * @brief Registers a "wildcard" handler (middleware).
     * Middleware are executed in registration order before every specific handler.
     * @param handler The function to execute. If it returns false, it blocks the chain.
     * @return ActionRouter& Reference to itself.
     * @tag ACT-RTR-MTH-002
     */
    ActionRouter& OnAny(ActionHandler handler);

    /**
     * @brief Starts the dispatch of an incoming message.
     * * Called internally by the `WebServer` on every received WebSocket frame.
     * Executes the wildcards first; if they all return true, it looks up and executes the specific handler.
     * @param ctx The context of the WebSocket connection.
     * @param msg The JSON payload received from the client.
     * @return true if a specific handler was found and executed, false if no handler exists (after running the wildcards).
     * @tag ACT-RTR-MTH-003
     */
    bool Dispatch(WsContext ctx, const json& msg) const;

private:
    std::unordered_map<std::string, ActionHandler> handlers_; /**< Map of actions to their specific handlers. */

    std::vector<ActionHandler> wildcards_; /**< List of middleware executed before every specific action. */
};
