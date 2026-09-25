#pragma once
#include <http_router.hpp>
#include <services/password_reset_service.hpp>
#include <services/auth_service.hpp>
#include <common/email_send_log.hpp>
#include <common/email_queue.hpp>

/**
 * @file password_reset_controller.hpp
 * @brief Controller for the password-reset (magic link) HTTP endpoints.
 *
 * Translates the wire protocol (JSON bodies, cookies) into
 * PasswordResetService calls, and back. Deliberately unauthenticated: the
 * whole point of the flow is recovering access after losing the password.
 */
class PasswordResetController {
public:
    /**
     * @param router HTTP router to register the routes on. Must outlive this class.
     * @param email_queue Queue outbound reset mail is handed to.
     */
    explicit PasswordResetController(HttpRouter& router, EmailQueue& email_queue);

private:
    /**
     * @brief Handles `POST /auth/reset/request`.
     * Body: `{ "email": string, "locale"?: string }`. Always responds `202`
     * (anti-enumeration); unknown email and over-cap are silent no-ops.
     */
    void HandleRequest(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles `POST /auth/reset/confirm`.
     * Body: `{ "token": string, "password": string }`. Consumes the token,
     * sets the new password, and auto-logs-in by issuing the usual cookies.
     */
    void HandleConfirm(AppResponse* res, AppRequest* req);

    /**< @brief Limit in bytes for the HTTP payload (Anti-DDoS). */
    static constexpr int kMaxBodyBytes = 4096;

    bool                 trust_proxy_;
    AuthService          auth_service_;
    PasswordResetService reset_service_;
    EmailSendLog         email_send_log_;
    EmailQueue&          email_queue_;
};
