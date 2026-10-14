#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <websocket_context.hpp>
#include <common/ws.hpp>
#include <common/contract.hpp>

/**
 * @class IBroadcaster
 * @brief Abstract interface for all WebSocket transport operations.
 *
 * Pure-virtual Send/Publish/Subscribe/Unsubscribe form the seam between
 * controllers and the uWS runtime. Non-virtual convenience methods
 * (SendJson, SendError, SendSuccess, PublishJson) build on those primitives
 * so the logic lives in one place, not scattered across call sites.
 *
 * The `compress` flag is forwarded to the uWS production adapter;
 * the fake ignores it.
 */
class IBroadcaster {
public:
    virtual ~IBroadcaster() = default;

    /**
     * @brief Send a raw string frame to a single socket.
     * @param droppable True ONLY for superseded-by-next frames (periodic
     * snapshots): the production backpressure guard may skip the frame when
     * the socket is not keeping up, and the next broadcast heals the loss.
     * Every other frame (events, barrier, defs, match_start, abort, DM,
     * errors, match_over) must pass false (the default) so it is always
     * queued with pre-Task-9 buffering semantics and never silently lost.
     * @return true when the transport accepted the frame; false when it was
     * dropped (only possible when droppable is true, on the production
     * adapter). Callers sending lossy snapshots may ignore this; one-shot
     * frames must retry on false.
     */
    virtual bool Send(AppWebSocket* socket, const std::string& msg,
                      uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true,
                      bool droppable = false) = 0;

    /** @brief Publish a raw string frame to all subscribers of a topic. */
    virtual void Publish(const std::string& topic, const std::string& msg,
                         uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true) = 0;

    /** @brief Subscribe a socket to a pub/sub topic. */
    virtual void Subscribe(AppWebSocket* socket, const std::string& topic) = 0;

    /** @brief Unsubscribe a socket from a pub/sub topic. */
    virtual void Unsubscribe(AppWebSocket* socket, const std::string& topic) = 0;

    // --- Non-virtual convenience wrappers ---

    void SendJson(AppWebSocket* socket, const nlohmann::json& j,
                  uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true) {
        Send(socket, j.dump(), op, compress);
    }

    void PublishJson(const std::string& topic, const nlohmann::json& j,
                     uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true) {
        Publish(topic, j.dump(), op, compress);
    }

    void SendError(AppWebSocket* socket, uWS::OpCode op, contract::ErrorCode code,
                   const std::string& request_id, const std::string& detail = "") {
        auto msg = ws::MakeResponse(ws::ServerAction::kError, request_id);
        msg["code"] = contract::kErrorCodeStr.at(code);
        if (!detail.empty()) msg["detail"] = detail;
        Send(socket, msg.dump(), op);
    }

    void SendSuccess(AppWebSocket* socket, uWS::OpCode op, const std::string& request_id,
                     nlohmann::json data = nlohmann::json::object()) {
        auto msg = ws::MakeResponse(ws::ServerAction::kSuccess, request_id);
        msg.merge_patch(std::move(data));
        Send(socket, msg.dump(), op);
    }
};
