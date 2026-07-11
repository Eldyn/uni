/**
 * @file chat_controller.cpp
 * @brief Implementation of the ChatController class handling chat messages
 * over WebSocket.
 */

#include <controllers/chat_controller.hpp>
#include <common/ws.hpp>
#include <common/payloads.hpp>
#include <common/env.hpp>
#include <optional>

using json = nlohmann::json;

namespace {
constexpr const char* kGlobalChatTopic = "global";

/** Builds a `chat_history` frame from one shard, oldest message first. */
json MakeChatHistoryResponse(const std::string& request_id, const std::string& channel,
                             const std::optional<std::string>& target,
                             const ChatHistoryPage& page) {
    auto resp = ws::MakeResponse(ws::ServerAction::kChatHistory, request_id);
    resp["channel"] = channel;
    if (target.has_value()) resp["target"] = *target;
    resp["has_more"] = page.has_more;
    resp["messages"] = json::array();
    for (const auto& entry : page.messages) {
        resp["messages"].push_back(
            {{"id", entry.id}, {"username", entry.username}, {"message", entry.message}});
    }
    return resp;
}
}  // namespace

ChatController::ChatController(IActionRouter& router, IBroadcaster& broadcast,
                               IPresenceStore& presence, int send_global_history_on_join,
                               int global_history_limit)
    : action_router_(router), broadcaster_(broadcast), presence_(presence),
      send_global_history_on_join_(send_global_history_on_join >= 0
                                        ? send_global_history_on_join != 0
                                        : Env::Get("CHAT_GLOBAL_HISTORY_ON_JOIN", "1") != "0"),
      global_history_limit_(global_history_limit != kUnsetHistoryLimit
                                 ? global_history_limit
                                 : Env::GetInt("CHAT_GLOBAL_HISTORY_SIZE", 64)) {
    action_router_.On(ws::ClientAction::kChatSend, [this](WsContext ctx, const json& msg) {
        HandleChatSend(ctx, msg);
        return true;
    });
    action_router_.On(ws::ClientAction::kChatHistoryRequest, [this](WsContext ctx, const json& msg) {
        HandleChatHistoryRequest(ctx, msg);
        return true;
    });
}

void ChatController::OnOpen(AppWebSocket* socket, PerSocketData* socket_data) {
    (void)socket_data;
    broadcaster_.Subscribe(socket, kGlobalChatTopic);

    if (!send_global_history_on_join_) return;

    // global_history_limit_ is env/constructor-resolved (trusted), so a -1
    // override is allowed here to mean "the whole in-memory buffer, one shard".
    auto page = chat_service_.GetGlobalHistoryPage(std::nullopt, global_history_limit_);
    broadcaster_.SendJson(socket, MakeChatHistoryResponse("", "global", std::nullopt, page));
}

void ChatController::HandleChatSend(WsContext ctx, const json& message) {
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");

    auto payload_res = ws::ParsePayload<ws::ChatSendPayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, payload_res.error().message);
        return;
    }

    const std::string& username = ctx.socket_data->username;

    if (!chat_service_.AllowSend(username)) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kRateLimited,
                               request_id);
        return;
    }

    if (payload_res->channel == kGlobalChatTopic) {
        chat_service_.PostGlobalMessage(username, payload_res->message);

        auto resp = ws::MakeResponse(ws::ServerAction::kChatMessage, request_id);
        resp["username"] = username;
        resp["message"]  = payload_res->message;
        resp["channel"]  = kGlobalChatTopic;
        broadcaster_.PublishJson(kGlobalChatTopic, resp);
        return;
    }

    if (payload_res->channel == "lobby") {
        if (ctx.socket_data->lobby_code.empty()) {
            broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kNotInLobby,
                                   request_id);
            return;
        }

        auto resp = ws::MakeResponse(ws::ServerAction::kChatMessage, request_id);
        resp["username"] = username;
        resp["message"]  = payload_res->message;
        resp["channel"]  = "lobby";
        broadcaster_.PublishJson("lobby_" + ctx.socket_data->lobby_code, resp);
        return;
    }

    if (payload_res->channel == "dm") {
        if (!payload_res->target.has_value()) {
            broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                                   request_id, "Missing DM target");
            return;
        }

        const std::string recipient = *payload_res->target;
        auto send_result = chat_service_.SendDirectMessage(username, recipient,
                                                            payload_res->message);
        if (!send_result) {
            broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                                   request_id, send_result.error().message);
            return;
        }

        auto resp = ws::MakeResponse(ws::ServerAction::kChatMessage, request_id);
        resp["username"] = username;
        resp["message"]  = payload_res->message;
        resp["channel"]  = "dm";
        resp["target"]   = recipient;

        broadcaster_.SendJson(ctx.socket, resp);

        AppWebSocket* recipient_socket = presence_.GetSocket(recipient);
        if (recipient_socket != nullptr) {
            broadcaster_.SendJson(recipient_socket, resp);
        }
        return;
    }

    broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                           request_id, "Unsupported chat channel");
}

void ChatController::HandleChatHistoryRequest(WsContext ctx, const json& message) {
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");

    auto payload_res = ws::ParsePayload<ws::ChatHistoryRequestPayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, payload_res.error().message);
        return;
    }

    const std::string& username = ctx.socket_data->username;
    const std::string channel = payload_res->channel.value_or("dm");
    // limit is untrusted client input — GetGlobalHistoryPage/GetDirectHistoryPage
    // both clamp it to [1, kMaxShardSize] (or default when <= 0); a client can
    // never request the unbounded (-1) mode reserved for the OnOpen push.
    const int limit = payload_res->limit.value_or(0);

    if (channel == "global") {
        auto page = chat_service_.GetGlobalHistoryPage(payload_res->before_id, limit);
        broadcaster_.SendJson(ctx.socket,
                              MakeChatHistoryResponse(request_id, "global", std::nullopt, page));
        return;
    }

    if (!payload_res->target.has_value()) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, "Missing DM target");
        return;
    }

    auto page_res = chat_service_.GetDirectHistoryPage(username, *payload_res->target,
                                                        payload_res->before_id, limit);
    if (!page_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInternalError,
                               request_id, page_res.error().message);
        return;
    }

    broadcaster_.SendJson(
        ctx.socket, MakeChatHistoryResponse(request_id, "dm", *payload_res->target, *page_res));
}
