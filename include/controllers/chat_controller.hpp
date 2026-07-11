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
     * @param router                    WebSocket action router (DI seam).
     * @param broadcast                 Transport layer for sends/publishes (DI seam).
     * @param presence                  Presence lookups for routing direct messages (DI seam).
     * @param send_global_history_on_join Tri-state override for
     *   `CHAT_GLOBAL_HISTORY_ON_JOIN`: -1 reads the env var, 0/1 force
     *   disabled/enabled (test seam).
     * @param global_history_limit      Override for `CHAT_GLOBAL_HISTORY_SIZE`:
     *   `kUnsetHistoryLimit` reads the env var, -1 sends the entire in-memory
     *   ring buffer, any other value caps how many of the newest messages are
     *   sent on join (test seam).
     * @tag CTRL-CHAT-MTH-001
     */
    static constexpr int kUnsetHistoryLimit = -2;

    ChatController(IActionRouter& router, IBroadcaster& broadcast, IPresenceStore& presence,
                  int send_global_history_on_join = -1,
                  int global_history_limit = kUnsetHistoryLimit);

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

    /** Whether a newly connected socket receives recent global chat history. */
    bool send_global_history_on_join_;
    /**
     * Max number of global messages sent on join (newest N of the in-memory
     * ring buffer). -1 sends the entire buffer.
     */
    int global_history_limit_;

    /**
     * @brief Handles a `chat_send` action.
     * @tag CTRL-CHAT-ACT-001
     */
    void HandleChatSend(WsContext ctx, const nlohmann::json& message);

    /**
     * @brief Handles a `chat_history_request` action, replying with the
     * requester's DM history against the given target.
     * @tag CTRL-CHAT-ACT-002
     */
    void HandleChatHistoryRequest(WsContext ctx, const nlohmann::json& message);
};
