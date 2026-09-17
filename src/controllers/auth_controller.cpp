#include <controllers/auth_controller.hpp>
#include <common/http.hpp>
#include <common/env.hpp>
#include <nlohmann/json.hpp>
#include <logger.hpp>
#include <common/email_templates.hpp>

using json = nlohmann::json;

AuthController::AuthController(HttpRouter& router, EmailQueue& email_queue)
    : trust_proxy_(Env::Get("TRUST_PROXY", "0") != "0"),
      email_queue_(email_queue),
      verify_request_limiter_(std::stod(Env::Get("RATE_VERIFY_BURST", "3")),
                              std::stod(Env::Get("RATE_VERIFY_RPS",   "0.05"))),
      verify_attempt_throttle_(std::stoi(Env::Get("VERIFY_MAX_FAILS", "5")),
                               std::chrono::seconds(std::stoi(Env::Get("VERIFY_LOCKOUT_SEC", "300")))) {
    router.Post("/auth/register", [this](AppResponse* res, AppRequest* req) {
        HandleRegister(res, req);
    });

    router.Post("/auth/login", [this](AppResponse* res, AppRequest* req) {
        HandleLogin(res, req);
    });

    // Per-IP auth_limiter_ already covers routes starting with /auth/, so no additional wiring is needed there
    router.Post("/auth/verify/request-code", [this](AppResponse* res, AppRequest* req) {
        HandleRequestCode(res, req);
    });

    router.Post("/auth/guest", [this](AppResponse* res, AppRequest* req) {
        HandleGuest(res, req);
    });

    router.Get("/auth/guest/me", [this](AppResponse* res, AppRequest* req) {
        HandleGuestMe(res, req);
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

        if (!token || token->empty()) {
            Logger::Warn("[HTTP] Rejected auth-me, missing token");
            res->writeStatus("401 Unauthorized")->end();
            return;
        }

        auto payload = AuthService::VerifyToken(*token);

        if (!payload) {
            Logger::Warn("[HTTP] Rejected auth-me, invalid token");
            res->writeStatus("401 Unauthorized")->end();
            return;
        }

        auto status = auth_service_.GetAccountStatus(payload->username);
        if (!status) {
            Logger::Warn("[HTTP] Rejected auth-me, user not found: " + payload->username);
            res->writeStatus("401 Unauthorized")->end();
            return;
        }

        Logger::Info("[Auth] Login successful: " + payload->username);
        res->writeHeader("Set-Cookie",
                         "auth_token=" + *token + "; HttpOnly; Secure; SameSite=Strict; Path=/")
           ->writeHeader("Set-Cookie",
                         "ws_token=" + *token + "; HttpOnly; Secure; SameSite=None; Path=/")
           ->writeHeader("Content-Type", "application/json")
           ->end(json({
               {"username", payload->username},
               {"email", status->email},
               {"email_verified", status->email_verified}
           }).dump());
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

void AuthController::HandleRegister(AppResponse* res, AppRequest* req) {
    // INFO: Resolve the IP synchronously: req is invalid once ReadBody's
    //       async callback runs, so capture what is needed by value now.
    const std::string ip = http::GetClientIp(res, req, trust_proxy_);

    http::ReadBody(res, kMaxBodyBytes, [this, res, ip](const std::string& body) {
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
        std::string locale   = data.value("locale",   "en");

        auto result = auth_service_.Register(username, email, password, locale);
        if (!result) {
            WriteError(res, result.error());
            return;
        }

        auto session = auth_service_.Login(email, password, ip);
        if (!session) {
            WriteError(res, session.error());
            return;
        }

        res->writeHeader("Set-Cookie",
                         "auth_token=" + session->token +
                             "; HttpOnly; Secure; SameSite=Strict; Path=/")
           ->writeHeader("Set-Cookie",
                         "ws_token=" + session->token +
                             "; HttpOnly; Secure; SameSite=None; Path=/")
           ->writeHeader("Content-Type", "application/json")
           ->end(json({{"status", "ok"},
                       {"username", session->username},
                       {"email_verified", false}}).dump());
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

void AuthController::HandleGuestMe(AppResponse* res, AppRequest* req) {
    std::string_view cookies = req->getHeader("cookie");
    auto token = http::GetCookieValue(cookies, "ws_token");

    if (!token) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    auto payload = AuthService::VerifyToken(*token);
    if (!payload) {
        Logger::Warn("[HTTP] Rejected guest-me, invalid token");
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    // INFO: No auth_token is set here: a restored guest stays a guest, it
    //       is never promoted to a full account by this endpoint.
    res->writeHeader("Content-Type", "application/json")
       ->end("{\"username\": \"" + payload->username + "\"}");
}

void AuthController::HandleLogin(AppResponse* response, AppRequest* req) {
    // INFO: Resolve the IP synchronously: req is invalid once ReadBody's
    //       async callback runs, so capture what is needed by value now.
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

void AuthController::HandleRequestCode(AppResponse* res, AppRequest* req) {
    const std::string ip = http::GetClientIp(res, req, trust_proxy_);

    std::string_view cookies = req->getHeader("cookie");
    auto ws_token = http::GetCookieValue(cookies, "ws_token");
    auto auth_token = http::GetCookieValue(cookies, "auth_token");
    auto token = ws_token;
    if (auth_token) token = auth_token;

    if (!token || token->empty()) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    auto payload = AuthService::VerifyToken(*token);
    if (!payload) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    auto status = auth_service_.GetAccountStatus(payload->username);
    if (!status) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    if (status->email_verified) {
        res->writeStatus("409 Conflict")->end();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_evict_ >= std::chrono::seconds(60)) {
        last_evict_ = now;
        verify_request_limiter_.Evict();
        verify_attempt_throttle_.Evict();
    }

    if (!verify_request_limiter_.Allow(std::to_string(status->id) + "|" + ip)) {
        WriteError(res, Error::TooManyRequests("Verification request limit reached. Please wait."));
        return;
    }

    auto sends_res = verification_service_.SendsInLast24h(status->id);
    if (!sends_res) {
        WriteError(res, sends_res.error());
        return;
    }
    if (*sends_res >= Env::GetInt("EMAIL_MAX_SENDS_PER_DAY", 5)) {
        WriteError(res, Error::TooManyRequests("Daily verification email limit reached. Try again tomorrow."));
        return;
    }

    http::ReadBody(res, kMaxBodyBytes, [this, res, id = status->id, username = payload->username, email = status->email, db_locale = status->locale](const std::string& body) {
        std::string effective_locale = db_locale;
        json data = json::object();
        if (!body.empty()) {
            try {
                data = json::parse(body);
            } catch (...) {
                WriteError(res, Error::BadRequest("Invalid JSON"));
                return;
            }
        }

        if (data.is_object() && data.contains("locale") && data["locale"].is_string()) {
            std::string req_locale = data["locale"].get<std::string>();
            if (req_locale == "en" || req_locale == "de" || req_locale == "es" || 
                req_locale == "it" || req_locale == "ja" || req_locale == "ko" || req_locale == "zh") {
                effective_locale = req_locale;
                (void)Database::Get().Exec("UPDATE users SET locale = ? WHERE id = ?;", {effective_locale, id});
            }
        }

        auto issue_res = verification_service_.IssueCode(username);
        if (!issue_res) {
            WriteError(res, issue_res.error());
            return;
        }

        auto mail = RenderVerifyEmail(VerifyEmailData{
            .username = username,
            .code = issue_res->plaintext_code,
            .magic_link = BuildVerifyMagicLink(issue_res->plaintext_code),
            .locale = effective_locale
        });
        mail.to_address = email;

        email_queue_.Enqueue(std::move(mail));
        verification_service_.RecordSend(id);

        res->writeStatus("202 Accepted")
           ->writeHeader("Content-Type", "application/json")
           ->end(json({{"status", "queued"}}).dump());
    });
}
