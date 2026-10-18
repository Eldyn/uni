#pragma once
#include <economy/economy_service.hpp>
#include <http_router.hpp>

/**
 * @file economy_controller.hpp
 * @brief The HTTP API for the cash economy: the private read API and the shop.
 *
 * `GET /economy/me` returns the caller's balance, owned item ids and last 50
 * ledger rows. `POST /shop/buy` buys one item once per `request_id`. Both
 * routes need a valid session cookie and refuse guests: a guest has no `users`
 * row and therefore no balance to read or spend.
 */

/**
 * @class EconomyController
 * @brief Serves the balance/owned/ledger read and the buy route over HTTP.
 */
class EconomyController {
public:
    /**
     * @brief Constructor of the EconomyController.
     * Registers the GET and POST routes on the provided HttpRouter.
     * @param router The central HTTP router of the application.
     * @param economy The ledger service that owns the balance and the buys.
     */
    EconomyController(HttpRouter& router, uni::economy::EconomyService& economy);

private:
    /**
     * @brief Handler for the GET route `/economy/me`.
     * Returns `{ balance, owned, ledger }` with the last 50 ledger rows. 401
     * without a valid session cookie, 403 for a guest.
     */
    void HandleGetMe(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the POST route `/shop/buy`.
     * Buys one item idempotently on `request_id`. 401 without a valid session
     * cookie, 403 for a guest, 400 on a bad body, 402/404/409/410 from the
     * buy rules.
     */
    void HandleBuy(AppResponse* res, AppRequest* req);

    uni::economy::EconomyService& economy_;
};
