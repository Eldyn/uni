#pragma once

#include <services/chat_service.hpp>
#include <transport/iaction_router.hpp>
#include <transport/ibroadcaster.hpp>
#include <transport/ipresence_store.hpp>
#include <nlohmann/json.hpp>

/**
 * @file chat_controller.hpp
 * @brief Controller for handling chat messages over WebSocket.
 * * Dispatches the `chat_send` action to ChatService, which owns the global
 * chat history ring buffer and DM persistence.
 */

/**
 * @class ChatController
 * @brief Receives chat messages and routes them to the right channel:
 * `"global"` and `"lobby"` broadcast over a pub/sub topic, `"dm"` persists
 * and delivers directly to the recipient's socket if they're online.
 * @tag CTRL-CHAT-001
 */
class ChatController {
public:
    /**
     * @brief Constructor of the chat controller.
     * Registers the `chat_send` WebSocket action handler on the ActionRouter.
     * @param router    WebSocket action router (DI seam).
     * @param broadcast Transport layer for sends/publishes (DI seam).
     * @param presence  Presence lookups for routing direct messages (DI seam).
     * @tag CTRL-CHAT-MTH-001
     */
    ChatController(IActionRouter& router, IBroadcaster& broadcast, IPresenceStore& presence);

    /**
     * @brief Subscribes a freshly connected socket to the `"global"` chat
     * topic.
     * @tag CTRL-CHAT-MTH-002
     */
    void OnOpen(AppWebSocket* socket, PerSocketData* socket_data);

private:
    IActionRouter&  action_router_;
    IBroadcaster&   broadcaster_;
    IPresenceStore& presence_;
    ChatService     chat_service_;

    /**
     * @brief Handles a `chat_send` action.
     * @tag CTRL-CHAT-ACT-001
     */
    void HandleChatSend(WsContext ctx, const nlohmann::json& message);
};
