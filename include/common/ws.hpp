#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <common/contract.hpp>
#include <logger.hpp>
#include <result.hpp>
#include <websocket_context.hpp>

/**
 * @file ws.hpp
 * @brief Server-side WebSocket protocol utilities.
 *
 * Payload structs live in the generated header produced from contract/asyncapi.yaml:
 *   <ws/messages.h>  (generated at build time by hatchbed/asyncapi_gencpp)
 *
 * This file provides:
 *  - ClientAction  — struct of constexpr string constants for every client→server action.
 *  - ServerAction  — enum used by MakeResponse() for type-safe outgoing message construction.
 *  - kServerActionStr — enum → string mapping consumed by MakeResponse() and SendError().
 *  - Get / GetOr   — envelope field extraction helpers (used for request_id).
 *  - MakeResponse / SendError — response construction utilities.
 */

namespace ws {
    /**
     * @struct ClientAction
     * @brief String constants for every action sent from Clients to the Server.
     *
     * Using a struct instead of an enum allows the constants to be used directly
     * as std::string arguments without a separate lookup map.
     * @tag WS-STR-001
     */
    struct ClientAction {
        static constexpr const char* kLobbyStartMatch       = "lobby_start_match";
        static constexpr const char* kLobbyList             = "lobby_list";
        static constexpr const char* kLobbyCreate           = "lobby_create";
        static constexpr const char* kLobbyRejoin           = "lobby_rejoin";
        static constexpr const char* kLobbyJoin             = "lobby_join";
        static constexpr const char* kLobbyLeave            = "lobby_leave";
        static constexpr const char* kLobbyUpdateSettings   = "lobby_update_settings";
        static constexpr const char* kLobbyDeleteSavedMatch = "lobby_delete_saved_match";
        static constexpr const char* kLobbyListSavedMatches = "lobby_list_saved_matches";
        static constexpr const char* kLobbyResumeSavedMatch = "lobby_resume_saved_match";
        static constexpr const char* kLobbyPromote          = "lobby_promote";
        static constexpr const char* kLobbyKick             = "lobby_kick";
        static constexpr const char* kGameSubmitInput       = "game_submit_input";
        static constexpr const char* kGamePlayCard          = "game_play_card";
        static constexpr const char* kGameDrawCard          = "game_draw_card";
        static constexpr const char* kGameCallUno           = "game_call_uno";
        static constexpr const char* kGameExit              = "game_exit";
        static constexpr const char* kChatSend              = "chat_send";

        ClientAction() = delete;
    };

    /**
     * @enum ServerAction
     * @brief Actions sent from the Server to the connected Clients.
     * @tag WS-ENUM-001
     */
    enum class ServerAction {
        kSuccess,           /**< Generic success with arbitrary data attached. */
        kError,             /**< Generic error containing a reason. */

        kLobbyList,         /**< Sending the list of available public lobbies. */
        kLobbyUpdated,      /**< Broadcast: the lobby state has changed. */
        kLobbyJoined,       /**< Success: the recipient has joined the lobby. */
        kLobbyLeft,         /**< Success: the recipient has left the lobby. */
        kLobbyEvicted,      /**< Eviction: lobby closed because it has no human players. */

        kGameStateUpdated,  /**< Broadcast: the state of the ongoing match has changed. */
        kGameOver,          /**< Broadcast: the match has ended. */

        kChatMessage,       /**< Broadcast: a new chat message. */
    };

    inline const std::unordered_map<ServerAction, std::string> kServerActionStr {
        { ServerAction::kSuccess,          "success"            },
        { ServerAction::kError,            "error"              },
        { ServerAction::kLobbyList,        "lobby_list"         },
        { ServerAction::kLobbyEvicted,     "lobby_evicted"      },
        { ServerAction::kLobbyUpdated,     "lobby_updated"      },
        { ServerAction::kLobbyJoined,      "lobby_joined"       },
        { ServerAction::kLobbyLeft,        "lobby_left"         },
        { ServerAction::kGameStateUpdated, "game_state_updated" },
        { ServerAction::kGameOver,         "game_over"          },
        { ServerAction::kChatMessage,      "chat_message"       },
    };

    /**
     * @brief Extracts a mandatory field from a JSON payload, returning a monadic Result.
     * @tparam T Expected data type (e.g. int, std::string).
     * @param json Incoming JSON payload (untrusted).
     * @param key Key of the attribute to look up.
     * @return Result<T> The extracted value or an encapsulated error state.
     * @tag WS-UTIL-001
     */
    template <typename T>
    inline Result<T> Get(const nlohmann::json& json, std::string_view key) {
        auto it = json.find(key);
        if (it == json.end()) {
            return std::unexpected(Error::InvalidInput("Missing required field: '" + std::string(key) + "'"));
        }
        if (it->is_null()) {
            return std::unexpected(Error::InvalidInput("Required field '" + std::string(key) + "' cannot be null"));
        }

        if constexpr (std::is_same_v<T, std::string>) {
            if (it->is_string()) return it->get<std::string>();
            if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
        }
        else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            if (it->is_number_integer()) return it->get<T>();
            if (it->is_string()) {
                try {
                    if constexpr (std::is_signed_v<T>) return static_cast<T>(std::stoll(it->get<std::string>()));
                    else return static_cast<T>(std::stoull(it->get<std::string>()));
                } catch (...) {}
            }
        }
        else if constexpr (std::is_same_v<T, bool>) {
            if (it->is_boolean()) return it->get<bool>();
        }
        else if constexpr (std::is_floating_point_v<T>) {
            if (it->is_number()) return it->get<T>();
        }

        return std::unexpected(Error::InvalidInput("Type mismatch for field: '" + std::string(key) + "'"));
    }

    /**
     * @brief Extracts an optional field from a JSON, applying a fallback in case of absence/error.
     * @tparam T Expected data type.
     * @param json JSON payload.
     * @param key Key of the attribute to look up.
     * @param default_value Fallback value returned if the data is absent/invalid.
     * @return T The extracted value or the provided fallback.
     * @tag WS-UTIL-002
     */
    template <typename T>
    inline T GetOr(const nlohmann::json& json, std::string_view key, const T& default_value) {
        auto res = Get<T>(json, key);
        return res ? *res : default_value;
    }

    /**
     * @brief Builds the base scaffolding for a server response JSON message.
     * @param action The ServerAction type to insert into the payload.
     * @param request_id (Optional) ID of the original request for frontend tracking.
     * @return nlohmann::json JSON structure ready to be populated with other fields.
     * @tag WS-UTIL-003
     */
    inline nlohmann::json MakeResponse(ws::ServerAction action, const std::string& request_id = "") {
        nlohmann::json json = nlohmann::json::object({
            {"action", kServerActionStr.at(action)}
        });

        if (!request_id.empty()) {
            json["request_id"] = request_id;
        }

        Logger::Log("[WS] response sent: ", kServerActionStr.at(action), "(", request_id, ")");
        return json;
    }

    /**
     * @brief Sends a contract error frame on the specific socket.
     * @param ws Pointer to the recipient socket.
     * @param op The uWebSockets OpCode (e.g. text).
     * @param code A contract::ErrorCode identifying the failure. The frontend
     *             owns the human-readable text; the backend sends only the code.
     * @param request_id The ID of the packet that triggered the error.
     * @param detail (Optional) supplementary context for dynamic errors (e.g. a
     *               payload parse message). Never the primary user-facing string.
     * @tag WS-UTIL-004
     */
    inline void SendError(AppWebSocket* ws, uWS::OpCode op, contract::ErrorCode code,
                          const std::string& request_id, const std::string& detail = "") {
        auto msg = MakeResponse(ServerAction::kError, request_id);
        msg["code"] = contract::kErrorCodeStr.at(code);
        if (!detail.empty()) msg["detail"] = detail;
        ws->send(msg.dump(), op);
    }

    /**
     * @brief Sends a generic success frame, optionally carrying extra fields.
     * @param ws Pointer to the recipient socket.
     * @param op The uWebSockets OpCode (e.g. text).
     * @param request_id The ID of the request being acknowledged.
     * @param data (Optional) JSON object merged into the response envelope.
     * @tag WS-UTIL-005
     */
    inline void SendSuccess(AppWebSocket* ws, uWS::OpCode op, const std::string& request_id,
                            nlohmann::json data = nlohmann::json::object()) {
        auto msg = MakeResponse(ServerAction::kSuccess, request_id);
        msg.merge_patch(std::move(data));
        ws->send(msg.dump(), op);
    }
}
