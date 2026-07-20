#pragma once
#include <functional>
#include <string>
#include <nlohmann/json.hpp>
#include <websocket_context.hpp>

using json = nlohmann::json;

/**
 * @typedef ActionHandler
 * @brief Signature for WebSocket action handlers.
 */
using ActionHandler = std::function<bool(WsContext, const json&)>;

/**
 * @class IActionRouter
 * @brief Abstract interface for registering WebSocket action handlers.
 */
class IActionRouter {
public:
    virtual ~IActionRouter() = default;

    /**
     * @brief Registers a handler for a specific action.
     * @param action The action name string (e.g. "lobby_create").
     * @param handler The handler to invoke.
     * @return IActionRouter& Reference to self for chaining.
     */
    virtual IActionRouter& On(const std::string& action, ActionHandler handler) = 0;

    /**
     * @brief Registers a wildcard middleware executed before every specific handler.
     * @param handler Middleware function; returning false blocks the specific handler.
     * @return IActionRouter& Reference to self for chaining.
     */
    virtual IActionRouter& OnAny(ActionHandler handler) = 0;
};
