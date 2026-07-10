/**
 * @file chat_controller.cpp
 * @brief Implementation of the ChatController class handling chat messages
 * over WebSocket.
 */

#include <controllers/chat_controller.hpp>
#include <common/ws.hpp>
#include <common/payloads.hpp>

using json = nlohmann::json;

namespace {
constexpr const char* kGlobalChatTopic = "global";
}  // namespace

ChatController::ChatController(IActionRouter& router, IBroadcaster& broadcast,
                               IPresenceStore& presence)
    : action_router_(router), broadcaster_(broadcast), presence_(presence) {
    action_router_.On(ws::ClientAction::kChatSend, [this](WsContext ctx, const json& msg) {
        HandleChatSend(ctx, msg);
        return true;
    });
}

void ChatController::OnOpen(AppWebSocket* socket, PerSocketData* socket_data) {
    (void)socket_data;
    broadcaster_.Subscribe(socket, kGlobalChatTopic);
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
