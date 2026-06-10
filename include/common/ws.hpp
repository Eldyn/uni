#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <logger.hpp>
#include <result.hpp>
#include <websocket_context.hpp>

namespace ws {
    enum class ServerAction {
        kSuccess,           ///< Generic Success + arbitrary data.
        kError,             ///< Generic Error + reason + arbitrary data.
    
        kLobbyList,         ///< Sends out a list of publicly available lobbies.
        kLobbyUpdated,      ///< broadcast: lobby state changed.
        kLobbyJoined,       ///< The receiver joined the lobby successfully.
        kLobbyLeft,         ///< The receiver left the lobby sucessfully,
        kLobbyEvicted,      ///< Lobby Evicted: no humans were left in the lobby so the server deleted it.
    
        kGameStateUpdated,  ///< broadcast: game state changed.
        kGameOver,          ///< broadcast: game ended.

        kChatMessage,       ///< broadcast: message from a client.
    };

    enum class ClientAction {
        kLobbyStartMatch,        ///< Request to start the match 
        kLobbyList,             ///< Request to receive a list of publicly available lobbies.
        kLobbyCreate,           ///< Request to create a lobby with the provided settings.
        kLobbyRejoin,           ///< Request to rejoin a lobby with the provided lobby code.
        kLobbyJoin,             ///< Request to join a lobby with the provided lobby code.
        kLobbyLeave,            ///< Request to leave the lobby.
        kLobbyUpdateSettings,   ///< Request to update the lobby's settings.
        kLobbyDeleteSavedMatch, ///< Request to delete a saved match.
        kLobbyListSavedMatches, ///< Request to receive a list of saved matches with the 
                                ///< provided player list.
        kLobbyResumeSavedMatch, ///< Request to resume the saved match with the provided match id.
        kLobbyPromote,          ///< Request to promote to host the provided user.
        kLobbyKick,             ///< Request to kick out the provided user.
    
        kGameSubmitInput,       ///< Request to send input for a generic Effect requiring it.
        kGamePlayCard,          ///< Request to play a card with the provided id.
        kGameDrawCard,          ///< Request to draw a card from the draw pile.
        kGameCallUno,           ///< Request to declare UNO.
        kGameExit,              ///< Request to exit the game.
    
        kChatSend,              ///< Request to send a chat message.
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
     * @brief Extracts a mandatory field from JSON payload, returning a monadic Result.
     * 
     * Validates structural prerequisites. Maps any parsing deviations or structural 
     * missing fields straight into an Error::InvalidInput type context[cite: 2].
     * 
     * @tparam T The targeted destination data type.
     * @param json Inbound untrusted JSON payload from client channel.
     * @param key Target string data label name.
     * @return Result<T> Holding the type-verified object value or a valid error object state[cite: 1].
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
     * @brief Extracts an optional field from JSON payload with a safe fallback default.
     * 
     * Runs key discovery. If it misses or contains an incompatible data structure, 
     * execution skips error tracking paths entirely and safely steps back to default_value.
     * 
     * @tparam T Desired parameter data type (automatically inferred).
     * @param json Inbound untrusted JSON payload from client channel.
     * @param key Target string data label name.
     * @param default_value The safe fallback variable state configuration value.
     * @return T The extracted value on extraction validation success, or default_value on verification mismatch.
     */
    template <typename T>
    inline T GetOr(const nlohmann::json& json, std::string_view key, const T& default_value) {
        auto res = Get<T>(json, key);
        return res ? *res : default_value;
    }

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

    inline void SendError(AppWebSocket* ws, uWS::OpCode op, const std::string& reason, const std::string& request_id) {
        auto msg = MakeResponse(ServerAction::kError, request_id);
        msg["reason"] = reason;
        ws->send(msg.dump(), op);
    };
}
