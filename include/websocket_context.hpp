#pragma once
#include <string>
#include <App.h>

/**
 * @file websocket_context.hpp
 * @brief Defines the types and context structures associated with WebSocket connections.
 * * This file bridges the uWebSockets library and the application logic,
 * defining the data kept in memory for each individual connected socket.
 */

/**
 * @struct PerSocketData
 * @brief State data kept inside each uWS connection.
 * * The uWebSockets library owns the memory of this structure
 * for the entire lifetime of the socket. It is used to track the identity
 * of the connected user without having to use external lookup maps.
 * @tag WS-CTX-STR-001
 */
struct PerSocketData {
    std::string username;   /**< Username set post-upgrade after JWT verification. */
    std::string room;       /**< Current uWS broadcast room (e.g. the lobby ID). */
    std::string lobby_code; /**< The invite code of the lobby the user is in. */
};

/**
 * @typedef AppWebSocket
 * @brief Alias for the complex uWebSockets socket type configured for SSL and routing.
 * @tag WS-CTX-TYP-001
 */
using AppWebSocket = uWS::WebSocket<true, true, PerSocketData>;

/**
 * @typedef AppRequest
 * @brief Alias for the incoming uWebSockets HTTP request.
 * @tag WS-CTX-TYP-002
 */
using AppRequest   = uWS::HttpRequest;

/**
 * @typedef AppResponse
 * @brief Alias for the outgoing uWebSockets HTTP response (with SSL support).
 * @tag WS-CTX-TYP-003
 */
using AppResponse  = uWS::HttpResponse<true>;

/**
 * @struct WsContext
 * @brief Execution context passed to every handler of the `ActionRouter`.
 * * Encapsulates everything a handler needs to process a message and
 * respond, removing the need to access global state.
 * @tag WS-CTX-STR-002
 */
struct WsContext {
    AppWebSocket* socket;       /**< Pointer to the socket that sent the message. */
    PerSocketData* socket_data;  /**< Session data associated with the calling socket. */
    uWS::OpCode    op_code;      /**< Operation code of the WebSocket frame (e.g. Text or Binary). */
};
