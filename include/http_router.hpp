#pragma once
#include <websocket_context.hpp>
#include <functional>
#include <string>
#include <vector>

/**
 * @file http_router.hpp
 * @brief Implementation of the router for HTTP/REST requests.
 * * Accumulates the routes during the startup phase and applies them (Attach) in bulk
 * to the uWebSockets application instance before execution.
 */

/**
 * @typedef HttpHandler
 * @brief Signature for HTTP request handlers.
 * Receives pointers to the uWS `AppResponse` and `AppRequest` objects.
 * @tag HTTP-RTR-TYP-001
 */
using HttpHandler = std::function<void(AppResponse*, AppRequest*)>;

/**
 * @class HttpRouter
 * @brief Handles the registration and routing of HTTP calls (GET, POST and middleware).
 * * The approach differs slightly from the ActionRouter because uWebSockets requires
 * HTTP routes to be registered explicitly directly on its native instance.
 * @tag HTTP-RTR-CLS-001
 */
class HttpRouter {
public:
    HttpRouter()                             = default;
    HttpRouter(const HttpRouter&)            = delete;
    HttpRouter& operator=(const HttpRouter&) = delete;

    /**
     * @brief Registers a handler for a GET route.
     * Supports uWS's native wildcard paths (e.g. "/\*").
     * @param path The URI path, must start with "/".
     * @param handler The function to execute.
     * @return HttpRouter& Reference for chaining.
     * @tag HTTP-RTR-MTH-001
     */
    HttpRouter& Get(const std::string& path, HttpHandler handler);

    /**
     * @brief Registers a handler for a POST route.
     * @param path The URI path, must start with "/".
     * @param handler The function to execute.
     * @return HttpRouter& Reference for chaining.
     * @tag HTTP-RTR-MTH-002
     */
    HttpRouter& Post(const std::string& path, HttpHandler handler);

    /**
     * @brief Registers a middleware that is executed before any HTTP request.
     * Very useful to configure global logging or CORS headers.
     * * WARNING: If it returns false to interrupt the chain, the middleware
     * must take care of sending the HTTP response to the client itself.
     * @param handler The middleware function.
     * @return HttpRouter& Reference for chaining.
     * @tag HTTP-RTR-MTH-003
     */
    HttpRouter& OnAny(std::function<bool(AppResponse*, AppRequest*)> handler);

    /**
     * @brief Applies all the accumulated routes (and their middleware) to the native uWebSockets instance.
     * Must be called exactly once by the `WebServer` before `Run()`.
     * @param app Reference to the current uWebSockets instance.
     * @tag HTTP-RTR-MTH-004
     */
    void Attach(AppHttp& app);

private:
    /**
     * @struct Route
     * @brief Internal structure that temporarily stores the routes before loading.
     * @tag HTTP-RTR-STR-001
     */
    struct Route {
        std::string method;   /**< HTTP method ("GET" or "POST"). */
        std::string path;     /**< Registered URI path. */
        HttpHandler handler;  /**< Associated callback function. */

        Route(std::string m, std::string p, HttpHandler h) : method(m), path(p), handler(h) {}
    };

    std::vector<Route>                                          routes_;
    std::vector<std::function<bool(AppResponse*, AppRequest*)>> wildcards_;

    /**
     * @brief Wraps a specific handler so that it first runs the registered wildcards.
     * @param handler The original handler to decorate.
     * @return HttpHandler The handler packaged with the middleware logic.
     * @tag HTTP-RTR-PRIV-001
     */
    HttpHandler WrapWithWildcards(HttpHandler handler);
};
