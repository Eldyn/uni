#include <controllers/auth_controller.hpp>
#include <common/http.hpp>
#include <common/env.hpp>
#include <nlohmann/json.hpp>
#include <logger.hpp>

using json = nlohmann::json;

AuthController::AuthController(HttpRouter& router)
    : trust_proxy_(Env::Get("TRUST_PROXY", "0") != "0") {
    router.Post("/auth/register", [this](AppResponse* res, AppRequest* req) {
        HandleRegister(res, req);
    });

    router.Post("/auth/login", [this](AppResponse* res, AppRequest* req) {
        HandleLogin(res, req);
    });

    router.Post("/auth/guest", [this](AppResponse* res, AppRequest* req) {
        HandleGuest(res, req);
    });

    router.Post("/auth/logout", [this](AppResponse* res, AppRequest* req) {
        res->writeStatus("200 OK")
           ->writeHeader("Set-Cookie",
                         "auth_token=; Max-Age=0; HttpOnly; Secure; SameSite=Strict; Path=/")
           ->writeHeader("Set-Cookie",
                         "ws_token=; Max-Age=0; HttpOnly; Secure; SameSite=None; Path=/")
           ->end();
    });

    router.Get("/auth/me", [this](AppResponse* res, AppRequest* req) {
        std::string_view cookies = req->getHeader("cookie");
        auto token = http::GetCookieValue(cookies, "auth_token");

        auto payload = AuthService::VerifyToken(*token);

        if (!payload) {
            Logger::Warn("[HTTP] Rejected auth-me, invalid token");
            res->writeStatus("401 Unauthorized")->end();
            return;
        }

        Logger::Info("[Auth] Login successful: " + payload->username);
        res->writeHeader("Set-Cookie",
                         "auth_token=" + *token + "; HttpOnly; Secure; SameSite=Strict; Path=/")
           ->writeHeader("Set-Cookie",
                         "ws_token=" + *token + "; HttpOnly; Secure; SameSite=None; Path=/")
           ->writeHeader("Content-Type", "application/json")
           ->end("{\"username\": \"" + payload->username + "\"}");
    });
}

namespace {

void WriteError(AppResponse* res, const Error& err) {
    if (err.code == Error::Code::kTooManyRequests) {
        res->writeStatus(err.HttpStatus())->writeHeader("Retry-After", "60");
    } else {
        res->writeStatus(err.HttpStatus());
    }
    res->writeHeader("Content-Type", "application/json")
       ->end(json({{"error", err.message}}).dump());
}

}  // namespace

void AuthController::HandleRegister(AppResponse* res, AppRequest* /*req*/) {
    http::ReadBody(res, kMaxBodyBytes, [this, res](const std::string& body) {
        json data;
        try {
            data = json::parse(body);
        } catch (...) {
            WriteError(res, Error::BadRequest("Invalid JSON"));
            return;
        }

        std::string username = data.value("username", "");
        std::string email    = data.value("email",    "");
        std::string password = data.value("password", "");

        auto result = auth_service_.Register(username, email, password);
        if (!result) {
            WriteError(res, result.error());
            return;
        }

        res->writeHeader("Content-Type", "application/json")
           ->end(json({{"status", "ok"}, {"message", "Registration successful"}}).dump());
    });
}

void AuthController::HandleGuest(AppResponse* res, AppRequest* /*req*/) {
    auto session = auth_service_.CreateGuestSession();
    if (!session) {
        WriteError(res, session.error());
        return;
    }

    // INFO: ws_token only, no auth_token, so /auth/me keeps returning 401
    //       and the client never mistakes a guest for a full account.
    res->writeHeader("Set-Cookie",
                     "ws_token=" + session->token + "; HttpOnly; Secure; SameSite=None; Path=/")
       ->writeHeader("Content-Type", "application/json")
       ->end("{\"username\": \"" + session->username + "\"}");
}

void AuthController::HandleLogin(AppResponse* response, AppRequest* req) {
    // INFO: Resolve the IP synchronously: req is invalid once ReadBody's
    //       async callback runs, so capture what we need by value now.
    const std::string ip = http::GetClientIp(response, req, trust_proxy_);

    http::ReadBody(response, kMaxBodyBytes, [this, response, ip](const std::string& body) {
        json data;
        try {
            data = json::parse(body);
        } catch (...) {
            WriteError(response, Error::BadRequest("Invalid JSON"));
            return;
        }

        std::string email    = data.value("email",    "");
        std::string password = data.value("password", "");

        if (email.empty() || password.empty()) {
            WriteError(response, Error::InvalidInput("email and password are required"));
            return;
        }

        auto session = auth_service_.Login(email, password, ip);
        if (!session) {
            WriteError(response, session.error());
            return;
        }

        response->writeHeader("Set-Cookie",
                              "auth_token=" + session->token +
                                  "; HttpOnly; Secure; SameSite=Strict; Path=/")
                ->writeHeader("Set-Cookie",
                              "ws_token=" + session->token +
                                  "; HttpOnly; Secure; SameSite=None; Path=/")
                ->writeHeader("Content-Type", "application/json")
                ->end("{\"username\": \"" + session->username + "\"}");
    });
}
