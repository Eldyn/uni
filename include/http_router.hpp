#pragma once
#include <router.hpp>
#include <websocket_context.hpp>
#include <functional>
#include <string>
#include <vector>

using HttpHandler = std::function<void(AppResponse*, AppRequest*)>;

// Routes incoming HTTP requests to registered handlers by method + path.
//
// Handlers are accumulated via Get()/Post() before the server starts,
// then applied all at once to the uWS app via Attach().
//
// Modules self-register in their constructors:
//   explicit AuthModule(HttpRouter& router) {
//       router.Post("/auth",     [this](...){ HandleLogin(...);    });
//       router.Post("/register", [this](...){ HandleRegister(...); });
//   }
//
// Wildcards ("*") run before every specific handler.
// Return false from a wildcard to abort the request chain.
// Returning false does NOT automatically send a response —
// the wildcard is responsible for writing a response before returning false.
//
// Note: Attach() must be called exactly once, before WebServer::Run().
// Calling it more than once registers duplicate routes in uWS.
class HttpRouter : public Router {
public:
    HttpRouter() = default;

    // Register a GET handler. Path must start with "/".
    // Supports uWS wildcard paths, e.g. "/*".
    HttpRouter& Get(const std::string& path, HttpHandler handler);

    // Register a POST handler. Path must start with "/".
    HttpRouter& Post(const std::string& path, HttpHandler handler);

    // Register a wildcard that runs before every request regardless of
    // method or path. Useful for logging and CORS headers.
    // Return false to abort — wildcard must write the response itself.
    HttpRouter& OnAny(std::function<bool(AppResponse*, AppRequest*)> handler);

    // Apply all accumulated routes to the uWS app.
    // Called once by WebServer before Run().
    void Attach(uWS::SSLApp& app);

private:
    struct Route {
        std::string method;   // "GET" or "POST"
        std::string path;
        HttpHandler handler;
    };

    std::vector<Route>                                          routes_;
    std::vector<std::function<bool(AppResponse*, AppRequest*)>> wildcards_;

    // Wraps a handler so wildcards are run first.
    // Captures wildcards_ by reference — must not outlive this HttpRouter.
    HttpHandler WrapWithWildcards(HttpHandler handler);
};
