/**
 * @file friend_controller.cpp
 * @brief Implementation of the FriendController class handling friend
 * requests and the friends list over WebSocket.
 */

#include <controllers/friend_controller.hpp>
#include <common/ws.hpp>
#include <common/payloads.hpp>
#include <services/auth_service.hpp>

using json = nlohmann::json;

namespace {

contract::ErrorCode ToErrorCode(Error::Code code) {
    switch (code) {
        case Error::Code::kConflict: return contract::ErrorCode::kFriendRequestExists;
        case Error::Code::kNotFound: return contract::ErrorCode::kFriendRequestNotFound;
        case Error::Code::kBadRequest: return contract::ErrorCode::kFriendRequestInvalid;
        default: return contract::ErrorCode::kInternalError;
    }
}

}  // namespace

FriendController::FriendController(IActionRouter& router, IBroadcaster& broadcast,
                                   IPresenceStore& presence)
    : action_router_(router), broadcaster_(broadcast), presence_(presence) {
    action_router_.On(ws::ClientAction::kFriendRequest, [this](WsContext ctx, const json& msg) {
        HandleFriendRequest(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kFriendResponse, [this](WsContext ctx, const json& msg) {
        HandleFriendResponse(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kFriendListRequest,
                      [this](WsContext ctx, const json& msg) {
        HandleFriendListRequest(ctx, msg);
        return true;
    });
}

void FriendController::HandleFriendRequest(WsContext ctx, const json& message) {
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");
    const std::string& username = ctx.socket_data->username;

    if (!AuthService::IsFullMember(username)) {
        broadcaster_.SendError(ctx.socket, ctx.op_code,
                               contract::ErrorCode::kFriendRequestInvalid, request_id,
                               "Email verification required");
        return;
    }

    auto payload_res = ws::ParsePayload<ws::FriendRequestPayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, payload_res.error().message);
        return;
    }

    auto result = friend_service_.SendRequest(username, payload_res->username);
    if (!result) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, ToErrorCode(result.error().code),
                               request_id, result.error().message);
        return;
    }

    broadcaster_.SendSuccess(ctx.socket, ctx.op_code, request_id);
    SendFriendListTo(username);
    SendFriendListTo(payload_res->username);
}

void FriendController::HandleFriendResponse(WsContext ctx, const json& message) {
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");
    const std::string& username = ctx.socket_data->username;

    if (!AuthService::IsFullMember(username)) {
        broadcaster_.SendError(ctx.socket, ctx.op_code,
                               contract::ErrorCode::kFriendRequestInvalid, request_id,
                               "Email verification required");
        return;
    }

    auto payload_res = ws::ParsePayload<ws::FriendResponsePayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, payload_res.error().message);
        return;
    }

    auto result = friend_service_.RespondToRequest(username, payload_res->username,
                                                    payload_res->accept);
    if (!result) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, ToErrorCode(result.error().code),
                               request_id, result.error().message);
        return;
    }

    broadcaster_.SendSuccess(ctx.socket, ctx.op_code, request_id);
    SendFriendListTo(username);
    SendFriendListTo(payload_res->username);
}

void FriendController::HandleFriendListRequest(WsContext ctx, const json& message) {
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");
    SendFriendListTo(ctx.socket_data->username, request_id);
}

void FriendController::SendFriendListTo(const std::string& username,
                                        const std::string& request_id) {
    AppWebSocket* socket = presence_.GetSocket(username);
    if (socket == nullptr) return;

    // One round trip: friends + incoming + outgoing arrive as a single
    // json_object row (see FriendService::GetFriendLists).
    auto lists_res = friend_service_.GetFriendLists(username);
    if (!lists_res) return;
    const FriendLists& lists = *lists_res;

    json friends = json::array();
    for (const auto& friend_username : lists.friends) {
        friends.push_back({
            {"username", friend_username},
            {"online", presence_.IsOnline(friend_username)},
        });
    }

    auto resp = ws::MakeResponse(ws::ServerAction::kFriendList, request_id);
    resp["friends"] = friends;
    resp["incoming_requests"] = lists.incoming;
    resp["outgoing_requests"] = lists.outgoing;
    broadcaster_.SendJson(socket, resp);
}
