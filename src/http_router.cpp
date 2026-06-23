#include <http_router.hpp>
#include <logger.hpp>

HttpRouter& HttpRouter::Get(const std::string& path, HttpHandler handler) {
    routes_.emplace_back("GET", path, std::move(handler));
    return *this;
}

HttpRouter& HttpRouter::Post(const std::string& path, HttpHandler handler) {
    routes_.emplace_back("POST", path, std::move(handler));
    return *this;
}

HttpRouter& HttpRouter::OnAny(std::function<bool(AppResponse*, AppRequest*)> handler) {
    wildcards_.push_back(std::move(handler));
    return *this;
}

HttpHandler HttpRouter::WrapWithWildcards(HttpHandler handler) {
    // INFO: Capture wildcards_ by reference — safe because HttpRouter
    //       outlives all uWS handlers (both owned by WebServer with the same
    //       lifetime).
    return [this, handler = std::move(handler)](
            AppResponse* res, AppRequest* req) {
        for (const auto& wildcard : wildcards_) {
            if (!wildcard(res, req)) {
                // Wildcard aborted — it is responsible for the response.
                return;
            }
        }
        handler(res, req);
    };
}

void HttpRouter::Attach(AppHttp& app) {
    for (auto& route : routes_) {
        HttpHandler wrapped = WrapWithWildcards(route.handler);

        if (route.method == "GET") {
            app.get(route.path, [wrapped](AppResponse* res, AppRequest* req) {
                wrapped(res, req);
            });
        } else if (route.method == "POST") {
            app.post(route.path, [wrapped](AppResponse* res, AppRequest* req) {
                wrapped(res, req);
            });
        } else {
            Logger::Warn("[HttpRouter] Unknown method: " + route.method + " for path: " + route.path);
        }
    }

    Logger::Info("[HttpRouter] Attached " + std::to_string(routes_.size()) + " route(s)");
}
