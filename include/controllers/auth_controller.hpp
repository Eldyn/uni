#pragma once
#include <http_router.hpp>
#include <services/auth_service.hpp>
#include <services/verification_service.hpp>
#include <common/email_queue.hpp>
#include <common/rate_limiter.hpp>
#include <common/login_throttle.hpp>

/**
 * @file auth_controller.hpp
 * @brief Controller for authentication, registration and JWT token management.
 * * Translates between the wire protocol (HTTP requests/cookies/JSON bodies)
 * and AuthService, which owns the actual authentication domain logic.
 */

/**
 * @class AuthController
 * @brief Handles the HTTP routes for login/registration/guest sessions.
 * Parses requests, delegates to AuthService, and serializes the result
 * (cookies, JSON body, status code) back to the client. Holds no
 * authentication policy itself.
 */
class AuthController {
public:
    /**
     * @brief Constructor of the AuthController.
     * Automatically registers the POST and GET routes on the provided HTTP router (`/auth/register`, `/auth/login`, etc.).
     * * @param router Reference to the central HTTP router. Must outlive this class.
     */
    explicit AuthController(HttpRouter& router, EmailQueue& email_queue);

private:
    // --- HTTP Route Handlers ---

    /**
     * @brief Handles the POST route `/auth/register`.
     * Validates the inputs (length and special characters), computes the secure hash of the password
     * and stores the new user in the database.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     */
    void HandleRegister(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the POST route `/auth/login`.
     * Looks up the user in the database, recomputes the hash to verify the provided password
     * and, on success, issues and returns a signed JWT.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     */
    void HandleLogin(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the POST route `/auth/verify/request-code`.
     */
    void HandleRequestCode(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the POST route `/auth/verify/confirm`.
     */
    void HandleConfirmCode(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the POST route `/auth/guest`.
     * Issues an ephemeral guest session: a generated display name plus a
     * ws_token JWT so account-less players can browse and join lobbies.
     * Guests have no DB row, stats and saved matches stay account-only. The
     * '#' in the generated name is outside the registration username pattern,
     * so a guest can never collide with a registered account.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     */
    void HandleGuest(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the POST route `/auth/logout`.
     * Invalidates the session on the client side (e.g. by requesting deletion of the JWT cookie).
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     */
    void HandleLogout(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the GET route `/auth/me`.
     * Returns the details of the currently authenticated user by evaluating their token.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     */
    void HandleMe(AppResponse* res, AppRequest* req);

    /**
     * @brief Handles the GET route `/auth/guest/me`.
     * Restores an ephemeral guest session from the ws_token cookie, without
     * promoting it to a full account (no auth_token is read or issued).
     * Lets a page refresh keep an existing guest identity instead of the
     * client minting a brand-new one.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     */
    void HandleGuestMe(AppResponse* res, AppRequest* req);

    // --- Validation and Security Parameters ---

    /**< @brief Limit in bytes for the HTTP payload (Anti-DDoS). */
    static constexpr int kMaxBodyBytes   = 4096;

    bool trust_proxy_;      /**< Honour X-Forwarded-For when resolving the client IP. */
    AuthService auth_service_;
    VerificationService verification_service_;
    EmailQueue&         email_queue_;
    RateLimiter         verify_request_limiter_;
    LoginThrottle       verify_attempt_throttle_;
    std::chrono::steady_clock::time_point last_evict_{std::chrono::steady_clock::now()};
};
