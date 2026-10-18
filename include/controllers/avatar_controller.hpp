#pragma once
#include <avatar/catalogue.hpp>
#include <avatar/loadout.hpp>
#include <avatar/loadout_repository.hpp>
#include <http_router.hpp>

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>

/**
 * @file avatar_controller.hpp
 * @brief The HTTP API for the one active loadout per account.
 *
 * `GET /avatar/loadout/me` returns the saved loadout (or the default look),
 * `PUT /avatar/loadout/me` validates and saves a whole loadout atomically. A
 * guest session can read its own default look but may not save one.
 */

/**
 * @class AvatarController
 * @brief Serves and stores the account loadout over HTTP.
 */
class AvatarController {
public:
    /**
     * @brief Constructor of the AvatarController.
     * Registers the GET and PUT routes on the provided HttpRouter.
     * @param router The central HTTP router of the application.
     * @param catalogue The process catalogue, used to validate a save and to
     *        build the default and guest looks.
     */
    AvatarController(HttpRouter& router, const uni::avatar::Catalogue& catalogue);

    /**
     * @brief Called after a successful save with the username and the saved
     *        loadout. Empty unless the owner wires it; the lobby binds it to
     *        broadcast `loadout_changed`.
     */
    std::function<void(const std::string& username,
                       const nlohmann::json& loadout)> on_saved;

private:
    /**
     * @brief Handler for the GET route `/avatar/loadout/me`.
     * Returns the saved loadout, the default look when nothing was saved, or
     * the session default for a guest. 401 without a valid session cookie.
     */
    void HandleGetMe(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the PUT route `/avatar/loadout/me`.
     * Validates and stores the whole loadout, then calls `on_saved`. 401
     * without a valid session cookie, 403 for a guest, 400 on a bad or
     * rejected body.
     */
    void HandlePutMe(AppResponse* res, AppRequest* req);

    const uni::avatar::Catalogue& catalogue_;
    uni::avatar::StarterOwnership ownership_;
    uni::avatar::LoadoutRepository repository_;
};
