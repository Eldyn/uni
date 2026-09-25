#include <controllers/password_reset_controller.hpp>
#include <common/http.hpp>
#include <common/env.hpp>
#include <common/contract.hpp>
#include <common/email_templates.hpp>
#include <nlohmann/json.hpp>
#include <logger.hpp>

using json = nlohmann::json;

namespace {

void WriteError(AppResponse* res, const Error& err) {
    // INFO: DB/internal failures are logged in full server-side but never
    //       echoed: err.message can carry SQLite text or env names.
    const bool internal = err.code == Error::Code::kDatabaseFailure ||
                          err.code == Error::Code::kInternalError;
    const std::string message = internal ? "Internal server error" : err.message;
    if (internal) Logger::Error("[HTTP] internal error: " + err.message);

    if (err.code == Error::Code::kTooManyRequests) {
        res->writeStatus(err.HttpStatus())->writeHeader("Retry-After", "60");
    } else {
        res->writeStatus(err.HttpStatus());
    }
    res->writeHeader("Content-Type", "application/json")
       ->end(json({{"error", message}}).dump());
}

constexpr bool IsAllowedLocale(const std::string& locale) {
    return locale == "en" || locale == "de" || locale == "es" || locale == "it" ||
           locale == "ja" || locale == "ko" || locale == "zh";
}

}  // namespace

PasswordResetController::PasswordResetController(HttpRouter& router, EmailQueue& email_queue)
    : email_queue_(email_queue) {
    router.Post("/auth/reset/request", [this](AppResponse* res, AppRequest* req) {
        HandleRequest(res, req);
    });

    router.Post("/auth/reset/confirm", [this](AppResponse* res, AppRequest* req) {
        HandleConfirm(res, req);
    });
}

void PasswordResetController::HandleRequest(AppResponse* res, AppRequest* req) {
    (void)req;

    http::ReadBody(res, kMaxBodyBytes, [this, res](const std::string& body) {
        json data = json::object();
        if (!body.empty()) {
            try {
                data = json::parse(body);
            } catch (...) {
                WriteError(res, Error::BadRequest("Invalid JSON"));
                return;
            }
        }

        const std::string email = data.is_object() ? data.value("email", "") : "";
        std::string requested_locale;
        if (data.is_object() && data.contains("locale") && data["locale"].is_string()) {
            requested_locale = data["locale"].get<std::string>();
        }

        // INFO: Always 202, regardless of whether the account exists, whether a
        //       send is over the daily cap, or whether a DB read failed. The
        //       response shape must carry no account/quota signal.
        auto respond_queued = [res]() {
            res->writeStatus("202 Accepted")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"status", "queued"}}).dump());
        };

        if (email.empty()) {
            respond_queued();
            return;
        }

        auto row = Database::Get().QueryOne("SELECT id FROM users WHERE email = ?;", {email});
        if (!row) {
            Logger::Error("[Reset] request lookup failed: " + row.error().message);
            respond_queued();
            return;
        }
        if (!row->has_value()) {
            respond_queued();
            return;
        }

        const int user_id = row->value().Get<int>("id");

        auto sends = email_send_log_.SendsInLast24h(user_id);
        if (!sends) {
            Logger::Error("[Reset] send-log read failed: " + sends.error().message);
            respond_queued();
            return;
        }
        if (*sends >= Env::GetInt("EMAIL_MAX_SENDS_PER_DAY", 5)) {
            // Silent over-cap: same 202, no quota signal.
            respond_queued();
            return;
        }

        auto issued = reset_service_.IssueTokenForEmail(email);
        if (!issued) {
            if (issued.error().code != Error::Code::kNotFound) {
                Logger::Error("[Reset] issue failed: " + issued.error().message);
            }
            respond_queued();
            return;
        }

        // INFO: The requested locale is used for copy only; the account's
        //       stored locale is never mutated (the requester may not be the
        //       account owner, and this endpoint is unauthenticated).
        const std::string locale =
            IsAllowedLocale(requested_locale) ? requested_locale : issued->locale;

        auto mail = RenderResetEmail(ResetEmailData{
            .username = issued->username,
            .magic_link = BuildResetMagicLink(issued->plaintext_token),
            .locale = locale
        });
        mail.to_address = issued->email;

        email_queue_.Enqueue(std::move(mail));
        email_send_log_.RecordSend(user_id);

        respond_queued();
    });
}

void PasswordResetController::HandleConfirm(AppResponse* res, AppRequest* req) {
    (void)req;

    http::ReadBody(res, kMaxBodyBytes, [this, res](const std::string& body) {
        json data;
        try {
            data = json::parse(body);
        } catch (...) {
            WriteError(res, Error::BadRequest("Invalid JSON"));
            return;
        }

        if (!data.is_object()) {
            WriteError(res, Error::BadRequest("Invalid JSON"));
            return;
        }

        const std::string token    = data.value("token", "");
        const std::string password = data.value("password", "");

        // INFO: Short/weak password is a 400 (same policy/message as register),
        //       distinct from the generic 401 for a bad token.
        if (password.size() < static_cast<size_t>(contract::kPasswordMin)) {
            Error err = Error::InvalidInput("Password must be at least 8 characters");
            res->writeStatus("400 Bad Request")
               ->writeHeader("Content-Type", "application/json")
               ->end(json({{"error", err.message}}).dump());
            return;
        }

        auto consumed = reset_service_.ConsumeToken(token, password);
        if (!consumed) {
            // One generic failure for invalid/expired/consumed tokens.
            WriteError(res, Error::Unauthorised("Invalid or expired link"));
            return;
        }

        auto session = auth_service_.IssueToken(*consumed);
        if (!session) {
            WriteError(res, session.error());
            return;
        }

        // INFO: Same cookie shape as HandleLogin: auth_token is SameSite=Strict
        //       (direct use), ws_token is SameSite=None (cross-origin embeds).
        res->writeHeader("Set-Cookie",
                         "auth_token=" + *session +
                             "; HttpOnly; Secure; SameSite=Strict; Path=/")
           ->writeHeader("Set-Cookie",
                         "ws_token=" + *session +
                             "; HttpOnly; Secure; SameSite=None; Path=/")
           ->writeHeader("Content-Type", "application/json")
           ->end(json({{"status", "ok"}}).dump());
    });
}
