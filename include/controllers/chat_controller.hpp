#pragma once

#include <services/chat_service.hpp>
#include <transport/iaction_router.hpp>
#include <transport/ibroadcaster.hpp>
#include <nlohmann/json.hpp>

/**
 * @file chat_controller.hpp
 * @brief Controller for handling chat messages over WebSocket.
 * * Dispatches the `chat_send` action to ChatService, which owns the global
 * chat history ring buffer.
 */

/**
 * @class ChatController
 * @brief Receives chat messages and broadcasts them to the right topic.
 * Currently handles the `"global"` channel; `"lobby"` and `"dm"` channels
 * are added by later phases of the chat/friends work.
 * @tag CTRL-CHAT-001
 */
class ChatController {
public:
    /**
     * @brief Constructor of the chat controller.
     * Registers the `chat_send` WebSocket action handler on the ActionRouter.
     * @param router    WebSocket action router (DI seam).
     * @param broadcast Transport layer for sends/publishes (DI seam).
     * @tag CTRL-CHAT-MTH-001
     */
    ChatController(IActionRouter& router, IBroadcaster& broadcast);

    /**
     * @brief Subscribes a freshly connected socket to the `"global"` chat
     * topic and sends it the current global chat history.
     * @tag CTRL-CHAT-MTH-002
     */
    void OnOpen(AppWebSocket* socket, PerSocketData* socket_data);

private:
    IActionRouter& action_router_;
    IBroadcaster&  broadcaster_;
    ChatService    chat_service_;

    /**
     * @brief Handles a `chat_send` action.
     * @tag CTRL-CHAT-ACT-001
     */
    void HandleChatSend(WsContext ctx, const nlohmann::json& message);
};
