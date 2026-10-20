#pragma once
#include <avatar/catalogue.hpp>
#include <economy/economy_service.hpp>
#include <http_router.hpp>

/**
 * @file economy_controller.hpp
 * @brief The HTTP API for the cash economy: the private read API and the shop.
 *
 * `GET /economy/me` returns the caller's balance, owned item ids and last 50
 * ledger rows. `POST /shop/buy` buys one item once per `request_id`. The
 * wishlist, popularity and cosmetic routes read and write the marketplace
 * state. Every route needs a valid session cookie and refuses guests: a guest
 * has no `users` row and therefore no balance, wishlist or loadout.
 */

/**
 * @class EconomyController
 * @brief Serves the economy read, buy, wishlist, popularity and cosmetic routes.
 */
class EconomyController {
public:
    /**
     * @brief Constructor of the EconomyController.
     * Registers the economy and marketplace routes on the provided HttpRouter.
     * @param router The central HTTP router of the application.
     * @param economy The ledger service that owns the balance and the buys.
     * @param catalogue The catalogue that decides which item is visible and
     *        which slot a cosmetic fits. Defaults to the process catalogue.
     */
    EconomyController(
        HttpRouter& router, uni::economy::EconomyService& economy,
        const uni::avatar::Catalogue& catalogue =
            uni::avatar::Catalogue::Instance());

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

    /**
     * @brief Handler for the GET route `/shop/wishlist`.
     * Returns `{ items }` oldest first. 401 without a session, 403 for a guest.
     */
    void HandleGetWishlist(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the POST route `/shop/wishlist`.
     * Body `{ item }`, returns the updated `{ items }`. Rejects an unknown or
     * hidden item with 404 and a guest with 403.
     */
    void HandleAddWishlist(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the DELETE route `/shop/wishlist`.
     * Body `{ item }`, returns the updated `{ items }`. Removing an item that
     * is not wishlisted is a no-op.
     */
    void HandleRemoveWishlist(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the GET route `/shop/popular`.
     * Query `days` (default 7, clamped 1..90) and `limit` (default 12, clamped
     * 1..50). Returns `{ days, items: [{ item, purchases, wishlists }] }`.
     */
    void HandlePopular(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the GET route `/cosmetics/me`.
     * Returns `{ slots: { card_face, card_back, playmat } }`, each an item id
     * or `null` for the default look.
     */
    void HandleGetCosmetics(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the PUT route `/cosmetics/me`.
     * Body `{ slot, item }`, returns the updated `{ slots }`. Rejects an
     * unknown item or slot with 404/400 and an unowned item with 404 not_owned.
     */
    void HandlePutCosmetics(AppResponse* res, AppRequest* req);

    uni::economy::EconomyService& economy_;
    const uni::avatar::Catalogue& catalogue_;
};
