#pragma once
#include <string>
#include <cstdint>
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
    std::string ip;         /**< Client IP captured at upgrade (for rate limiting). */
    uint32_t    lobby_id = 0; /**< Numeric ID of the lobby the user is in (0 = none). */
};

/**
 * @def UNI_ENABLE_SSL
 * @brief Compile-time switch selecting the TLS-enabled uWS app (1) or plain HTTP (0).
 *
 * uWebSockets bakes the SSL flag into its types (`HttpResponse<SSL>`,
 * `WebSocket<SSL, ...>`, `TemplatedApp<SSL>`), so it can only be chosen at
 * compile time. Defaults to 1 (TLS) for local development with self-signed
 * certs. Build with `-DUNI_ENABLE_SSL=OFF` to emit plain HTTP — required when a
 * platform terminates TLS at its edge and proxies HTTP to the container
 * (e.g. Render, Fly, most reverse proxies).
 */
#ifndef UNI_ENABLE_SSL
#define UNI_ENABLE_SSL 1
#endif

/** Resolved SSL flag used to parametrise every uWS type below. */
inline constexpr bool kAppSSL = UNI_ENABLE_SSL;

/**
 * @typedef AppHttp
 * @brief The uWS application type: `uWS::SSLApp` when TLS is on, `uWS::App` when off.
 * @tag WS-CTX-TYP-000
 */
using AppHttp = uWS::TemplatedApp<kAppSSL>;

/**
 * @typedef AppWebSocket
 * @brief Alias for the complex uWebSockets socket type configured for routing.
 * @tag WS-CTX-TYP-001
 */
using AppWebSocket = uWS::WebSocket<kAppSSL, true, PerSocketData>;

/**
 * @typedef AppRequest
 * @brief Alias for the incoming uWebSockets HTTP request.
 * @tag WS-CTX-TYP-002
 */
using AppRequest   = uWS::HttpRequest;

/**
 * @typedef AppResponse
 * @brief Alias for the outgoing uWebSockets HTTP response (TLS flag follows kAppSSL).
 * @tag WS-CTX-TYP-003
 */
using AppResponse  = uWS::HttpResponse<kAppSSL>;

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
    /**< Operation code of the WebSocket frame (e.g. Text or Binary). */
    uWS::OpCode    op_code;
};
