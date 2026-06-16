#pragma once
#include <map>
#include <string>
#include <string_view>
#include <App.h>
#include <nlohmann/json.hpp>
#include <database.hpp>
#include <action_router.hpp>
#include <http_router.hpp>
#include <websocket_context.hpp>
#include <common/rate_limiter.hpp>

/**
 * @file webserver.hpp
 * @brief Definition of the main WebServer class that manages the entire lifecycle of the network application.
 *
 * This file contains the base infrastructure for starting the HTTP and WebSocket server
 * based on uWebSockets. It manages the routes, the database initialization and the active connections.
 */

/**
 * @class WebServer
 * @brief Main engine of the backend application.
 * * Initializes the SSL application (if configured), configures the HTTP and WebSocket routers,
 * maintains a registry of the active connections and provides hooks for connection open/close events.
 * Does not support copying or assignment (non-copyable class).
 * * @tag SRV-CORE-000
 */
class WebServer {
public:
    /**
     * @brief Constructor of the WebServer.
     * @param port The port on which the server will listen (e.g. 443 or 8080).
     * @param keyFile Path to the SSL private key file.
     * @param certFile Path to the SSL certificate file.
     * @param dbFile Path to the SQLite database file.
     * @param frontendPath Path to the directory holding the built frontend static files.
     * @tag SRV-CORE-001
     */
    explicit WebServer(int port,
                       std::string_view keyFile      = "key.pem",
                       std::string_view certFile     = "cert.pem",
                       std::string_view dbFile       = "uni.sqlite",
                       std::string_view frontendPath = "public");

    /**
     * @brief Destructor. Takes care of releasing any pending resources.
     * @tag SRV-CORE-002
     */
    ~WebServer();

    WebServer(const WebServer&)            = delete;
    WebServer& operator=(const WebServer&) = delete;

    /**
     * @brief Starts the server's listening loop (blocking method).
     * Puts uWebSockets into listening mode on the specified port.
     * @tag SRV-CORE-003
     */
    void Run();

    /**
     * @brief Retrieves a reference to the WebSocket action router.
     * @return ActionRouter& Router to register the WS message handlers.
     * @tag SRV-CORE-004
     */
    ActionRouter& GetActionRouter()   { return ws_router_;   }

    /**
     * @brief Retrieves a reference to the HTTP request router.
     * @return HttpRouter& Router to register the REST/HTTP routes.
     * @tag SRV-CORE-005
     */
    HttpRouter&   GetHTTPRouter()     { return http_router_; }

    /**
     * @brief Retrieves the underlying instance of the uWebSockets application.
     * @return AppHttp& The uWS application (SSL or plain per kAppSSL).
     * @tag SRV-CORE-006
     */
    AppHttp&      GetApp()            { return app_;         }

    /**
     * @typedef ConnectionHandler
     * @brief Callback invoked when a WebSocket is opened or closed.
     * @tag SRV-TYP-001
     */
    using ConnectionHandler = std::function<void(AppWebSocket*, PerSocketData*)>;

    /**
     * @brief Adds a callback to execute when a new WS connection opens.
     * @param handler The function to register.
     * @tag SRV-CORE-007
     */
    void OnConnectionOpen(ConnectionHandler handler);

    /**
     * @brief Adds a callback to execute when a WS connection closes.
     * @param handler The function to register.
     * @tag SRV-CORE-008
     */
    void OnConnectionClose(ConnectionHandler handler);

private:
    int    port_;           /**< Listening port of the server. */
    string db_file_;        /**< Path of the sqlite DB file. */
    string frontend_path_;  /**< Path of the directory with built frontend static files. */
    bool   trust_proxy_;    /**< Honour X-Forwarded-For (set when behind a proxy). */

    AppHttp app_;           /**< Main uWebSockets instance (SSL or plain per kAppSSL). */
    std::map<string, AppWebSocket*> connections_; /**< Map of connected users (Username -> Socket). */

    ActionRouter ws_router_;    /**< Handler for dispatching WebSocket messages. */
    HttpRouter   http_router_;  /**< Handler for dispatching HTTP requests. */

    RateLimiter http_limiter_; /**< Per-IP limiter for general HTTP/API routes. */
    RateLimiter auth_limiter_; /**< Stricter per-IP limiter for the auth routes. */
    RateLimiter ws_limiter_;   /**< Per-connection limiter for WebSocket actions. */
    RateLimiter::Clock::time_point last_evict_; /**< Last idle-bucket sweep. */

    /**
     * @brief Initializes the database connection and applies the schema if necessary.
     * @return true if the initialization succeeded, false otherwise.
     * @tag SRV-PRIV-001
     */
    bool InitDB();

    /**
     * @brief Periodically drops idle rate-limiter buckets (at most once a minute)
     *        so the per-IP maps cannot grow without bound. Called from middleware.
     * @tag SRV-PRIV-009
     */
    void MaybeEvict();

    /**
     * @brief Registers the internal routes and applies the wildcards (middleware) to the uWS app.
     * @tag SRV-PRIV-002
     */
    void RegisterRoutes();

    /**
     * @brief Generic handler for HTTP POST requests (delegated to the HttpRouter).
     * @tag SRV-PRIV-003
     */
    void HandlePost(AppResponse*, AppRequest*);

    /**
     * @brief Generic handler for HTTP GET requests (delegated to the HttpRouter).
     * @tag SRV-PRIV-004
     */
    void HandleGet (AppResponse*, AppRequest*);

    /**
     * @brief Handles the successful WebSocket upgrade event.
     * @tag SRV-PRIV-005
     */
    void OnSocketOpen   (AppWebSocket*);

    /**
     * @brief Receives the incoming WebSocket frames and forwards them to the ActionRouter.
     * @tag SRV-PRIV-006
     */
    void OnSocketMessage(AppWebSocket*, std::string_view, uWS::OpCode);

    /**
     * @brief Handles the disconnection event of a socket.
     * @tag SRV-PRIV-007
     */
    void OnSocketClosed (AppWebSocket*);

    /**
     * @brief Internal utility to read the entire content of a file (e.g. for static resources).
     * @param path Path of the file.
     * @return std::string The content of the file.
     * @tag SRV-UTIL-001
     */
    static std::string     ReadFile    (std::string_view path);

    /**
     * @brief Deduces the MimeType of a file based on its extension.
     * @param path Path of the file.
     * @return std::string The MIME Type (e.g. "text/html").
     * @tag SRV-UTIL-002
     */
    static std::string     GetMimeType (const string& path);

    std::vector<ConnectionHandler> on_open_hooks_;   /**< List of connection open hooks. */
    std::vector<ConnectionHandler> on_close_hooks_;  /**< List of connection close hooks. */
};
