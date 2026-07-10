#pragma once

#include <services/friend_service.hpp>
#include <transport/iaction_router.hpp>
#include <transport/ibroadcaster.hpp>
#include <transport/ipresence_store.hpp>
#include <nlohmann/json.hpp>

/**
 * @file friend_controller.hpp
 * @brief Controller for handling friend requests and the friends list.
 * * Dispatches the `friend_*` WebSocket actions to FriendService, which owns
 * the request/accept/remove rules.
 */

/**
 * @class FriendController
 * @brief Receives and processes friend-request and friends-list WebSocket actions.
 * @tag CTRL-FRIEND-001
 */
class FriendController {
public:
    /**
     * @brief Constructor of the friend controller.
     * Registers the `friend_*` WebSocket action handlers on the ActionRouter.
     * @param router   WebSocket action router (DI seam).
     * @param broadcast Transport layer for sends (DI seam).
     * @param presence Interface to look up online users and their sockets.
     * @tag CTRL-FRIEND-MTH-001
     */
    FriendController(IActionRouter& router, IBroadcaster& broadcast, IPresenceStore& presence);

private:
    IActionRouter&  action_router_;
    IBroadcaster&   broadcaster_;
    IPresenceStore& presence_;
    FriendService   friend_service_;

    /**
     * @brief Handles a `friend_request` action, sending a request to another user.
     * @tag CTRL-FRIEND-ACT-001
     */
    void HandleFriendRequest(WsContext ctx, const nlohmann::json& message);

    /**
     * @brief Handles a `friend_response` action, accepting or declining a request.
     * @tag CTRL-FRIEND-ACT-002
     */
    void HandleFriendResponse(WsContext ctx, const nlohmann::json& message);

    /**
     * @brief Handles a `friend_list_request` action, returning the caller's
     * friends, incoming requests, and outgoing requests.
     * @tag CTRL-FRIEND-ACT-003
     */
    void HandleFriendListRequest(WsContext ctx, const nlohmann::json& message);

    /**
     * @brief Builds and sends a `friend_list` message to the given user, if online.
     * @tag CTRL-FRIEND-ACT-004
     */
    void SendFriendListTo(const std::string& username, const std::string& request_id = "");
};
