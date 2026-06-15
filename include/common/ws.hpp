#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <logger.hpp>
#include <result.hpp>
#include <websocket_context.hpp>

/**
 * @file ws.hpp
 * @brief Definition of the bidirectional WebSocket messaging protocol.
 * * Contains the enumerations of the actions (Client -> Server and Server -> Client),
 * string/enum mapping constants and utility functions for safely extracting
 * the payloads from incoming JSON.
 */

namespace ws {
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

    /**
     * @enum ClientAction
     * @brief Actions (requests) sent from the Clients to the Server.
     * @tag WS-ENUM-002
     */
    enum class ClientAction {
        kLobbyStartMatch,        /**< Request: Start the match in the current lobby. */
        kLobbyList,              /**< Request: Get the public lobbies. */
        kLobbyCreate,            /**< Request: Create a new lobby with settings. */
        kLobbyRejoin,            /**< Request: Reconnect to a lobby after a disconnection. */
        kLobbyJoin,              /**< Request: Join a lobby via code. */
        kLobbyLeave,             /**< Request: Leave the current lobby. */
        kLobbyUpdateSettings,    /**< Request: Update the settings (Host only). */
        kLobbyDeleteSavedMatch,  /**< Request: Delete a saved match. */
        kLobbyListSavedMatches,  /**< Request: Get saved matches for the current group. */
        kLobbyResumeSavedMatch,  /**< Request: Resume a saved match. */
        kLobbyPromote,           /**< Request: Hand over the Host role to another player. */
        kLobbyKick,              /**< Request: Kick a player (Host only). */

        kGameSubmitInput,        /**< Request: Submit a mandatory input (e.g. colour choice). */
        kGamePlayCard,           /**< Request: Play a specific card. */
        kGameDrawCard,           /**< Request: Draw a card from the deck. */
        kGameCallUno,            /**< Request: Declare "UNO". */
        kGameExit,               /**< Request: Exit the ongoing match. */

        kChatSend,               /**< Request: Send a message in the lobby chat. */
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

    inline const std::unordered_map<ClientAction, std::string> kClientActionStr {
        { ClientAction::kGamePlayCard,           "game_play_card"           },
        { ClientAction::kGameDrawCard,           "game_draw_card"           },
        { ClientAction::kGameCallUno,            "game_call_uno"            },

        { ClientAction::kChatSend,               "chat_send"                },

        { ClientAction::kLobbyList,              "lobby_list"               },
        { ClientAction::kLobbyStartMatch,        "lobby_start_match"        },
        { ClientAction::kLobbyCreate,            "lobby_create"             },
        { ClientAction::kLobbyRejoin,            "lobby_rejoin"             },
        { ClientAction::kLobbyJoin,              "lobby_join"               },
        { ClientAction::kLobbyLeave,             "lobby_leave"              },
        { ClientAction::kLobbyUpdateSettings,    "lobby_update_settings"    },
        { ClientAction::kLobbyDeleteSavedMatch,  "lobby_delete_saved_match" },
        { ClientAction::kLobbyListSavedMatches,  "lobby_list_saved_matches" },
        { ClientAction::kLobbyResumeSavedMatch,  "lobby_resume_saved_match" },
        { ClientAction::kLobbyPromote,           "lobby_promote"            },
        { ClientAction::kLobbyKick,              "lobby_kick"               },
    
        { ClientAction::kGameSubmitInput,        "game_submit_input"        },
        { ClientAction::kGamePlayCard,           "game_play_card"           },
        { ClientAction::kGameDrawCard,           "game_draw_card"           },
        { ClientAction::kGameCallUno,            "game_call_uno"            },
        { ClientAction::kGameExit,               "game_exit"                },
    
        { ClientAction::kChatSend,               "chat_send"                },
    };
    
    
    inline const std::unordered_map<std::string, ClientAction> kStrClientAction {
        { "lobby_list", ClientAction::kLobbyList                            },
        { "lobby_start_match", ClientAction::kLobbyStartMatch               },
        { "lobby_create", ClientAction::kLobbyCreate                        },
        { "lobby_join", ClientAction::kLobbyJoin                            },
        { "lobby_leave", ClientAction::kLobbyLeave                          },

        { "play_card", ClientAction::kGamePlayCard                          },
        { "draw_card", ClientAction::kGameDrawCard                          },
        { "call_uno", ClientAction::kGameCallUno                            },

        { "chat_send", ClientAction::kChatSend                              },

        { "lobby_list", ClientAction::kLobbyList                            },
        { "lobby_create", ClientAction::kLobbyCreate                        },
        { "lobby_rejoin", ClientAction::kLobbyRejoin                        },
        { "lobby_join", ClientAction::kLobbyJoin                            },
        { "lobby_leave", ClientAction::kLobbyLeave                          },
        { "lobby_update_settings", ClientAction::kLobbyUpdateSettings       },
        { "lobby_delete_saved_match", ClientAction::kLobbyDeleteSavedMatch  },
        { "lobby_list_saved_matches", ClientAction::kLobbyListSavedMatches  },
        { "lobby_resume_saved_match", ClientAction::kLobbyResumeSavedMatch  },
        { "lobby_promote", ClientAction::kLobbyPromote                      },
        { "lobby_kick", ClientAction::kLobbyKick                            },
    
        { "game_submit_input", ClientAction::kGameSubmitInput               },
        { "game_play_card", ClientAction::kGamePlayCard                     },
        { "game_draw_card", ClientAction::kGameDrawCard                     },
        { "game_call_uno", ClientAction::kGameCallUno                       },
        { "game_exit", ClientAction::kGameExit                              },
    
        { "chat_send", ClientAction::kChatSend                              },
    };

    /**
     * @brief Extracts a mandatory field from a JSON payload, returning a monadic Result.
     * * Performs type validation. If the field is missing or the type is wrong, returns
     * an error of type Error::InvalidInput.
     * * @tparam T Expected data type (e.g. int, std::string).
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
     * * @tparam T Expected data type.
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
     * @param action The type of action (ServerAction) to insert into the payload.
     * @param request_id (Optional) ID of the original request for frontend tracking.
     * @return nlohmann::json JSON structure ready to be populated with other fields.
     * @tag WS-UTIL-003
     */
    inline nlohmann::json MakeResponse(ws::ServerAction action, const std::string& request_id = "") {
        // Force nlohmann::json to treat this strictly as an object map
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
     * @brief Sends a JSON payload formatted as an error on the specific socket.
     * @param ws Pointer to the recipient socket.
     * @param op The uWebSockets OpCode (e.g. text).
     * @param reason The descriptive error string (reason).
     * @param request_id (Optional) The ID of the packet that triggered the error.
     * @tag WS-UTIL-004
     */
    inline void SendError(AppWebSocket* ws, uWS::OpCode op, const std::string& reason, const std::string& request_id) {
        auto msg = MakeResponse(ServerAction::kError, request_id);
        msg["reason"] = reason;
        ws->send(msg.dump(), op);
    };
}
