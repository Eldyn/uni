#pragma once
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

enum class ServerAction {
    // ── Connection ──────────────────────────────────────
    kSyncData,          // username + room assigned after join
    kError,             // generic error with code + reason

    // ── Lobby ───────────────────────────────────────────
    kLobbyList,         // list of open lobbies
    kLobbyUpdated,      // a lobby's state changed (player joined/left, settings changed)
    kLobbyJoined,       // you successfully joined a lobby
    kLobbyLeft,         // you or someone else left the lobby
    kInviteCreated,     // server generated an invite link token

    // ── Pre-game ────────────────────────────────────────
    kPlayerJoined,      // another player entered the lobby
    kPlayerLeft,        // another player disconnected / left
    kPlayerReady,       // a player toggled ready
    kGameStarting,      // countdown before game begins (3...2...1)
    kRulesetSync,       // current ruleset sent to all players

    // ── Game: state ─────────────────────────────────────
    kStateUpdate,       // full game state (your hand, top card, turn, scores)
    kCardRejected,      // your play_card was invalid — includes reason
    kCardDrawn,         // you drew a card (private — sent only to you)
    kTurnChanged,       // whose turn it is now
    kColorChanged,      // active colour changed (after wild)
    kUnoCallSuccess,    // a UNO call was valid
    kUnoCallFailed,     // a UNO call was invalid (penalised)
    kUnoChallenged,     // someone challenged your +4

    // ── Game: events ────────────────────────────────────
    kCardPlayed,        // broadcast: who played what (no hand info)
    kPlayerDrewCard,    // broadcast: a player drew (count only, not what)
    kSkipApplied,       // skip landed on a player
    kReverseApplied,    // direction reversed
    kDrawStackApplied,  // +2/+4 stack resolved — who drew how many
    kGameOver,          // winner declared, final scores
    kRoundOver,         // if playing multi-round ruleset

    // ── Store / cosmetics ───────────────────────────────
    kInventorySync,     // your owned skins + decks
    kPurchaseConfirmed, // a store purchase succeeded
    kPurchaseFailed,    // a store purchase failed

    // ── Social ──────────────────────────────────────────
    kChatMessage,       // a chat message in the lobby/game
};

enum class ClientAction {
    // ── Lobby ───────────────────────────────────────────
    kLobbyList,         // request current lobby list
    kLobbyCreate,       // create a new lobby with settings
    kLobbyJoin,         // join by lobby id
    kLobbyLeave,        // leave current lobby
    kLobbyInvite,       // generate an invite link for current lobby
    kLobbySetRuleset,   // host changes ruleset before game starts
    kLobbySetSkin,      // player picks a card skin for this match
    kReady,             // player toggles ready state

    // ── Game ────────────────────────────────────────────
    kPlayCard,          // play a card from hand
    kDrawCard,          // draw from deck
    kCallUno,           // declare UNO
    kChallengeUno,      // challenge someone who forgot to call UNO
    kChallengePlusFour, // challenge a +4 (claim it was playable)
    kChooseColor,       // after playing a wild — declare colour
    kAckGameOver,       // client acknowledges the game over screen

    // ── Store ───────────────────────────────────────────
    kStorePurchase,     // buy a skin or deck
    kInventoryRequest,  // fetch owned items

    // ── Social ──────────────────────────────────────────
    kChatSend,          // send a chat message
};

inline const std::unordered_map<ClientAction, std::string> kClientActionStr {
    { ClientAction::kLobbyList,         "lobby_list"          },
    { ClientAction::kLobbyCreate,       "lobby_create"        },
    { ClientAction::kLobbyJoin,         "lobby_join"          },
    { ClientAction::kLobbyLeave,        "lobby_leave"         },
    { ClientAction::kLobbyInvite,       "lobby_invite"        },
    { ClientAction::kLobbySetRuleset,   "lobby_set_ruleset"   },
    { ClientAction::kLobbySetSkin,      "lobby_set_skin"      },
    { ClientAction::kReady,             "ready"               },
    { ClientAction::kPlayCard,          "play_card"           },
    { ClientAction::kDrawCard,          "draw_card"           },
    { ClientAction::kCallUno,           "call_uno"            },
    { ClientAction::kChallengeUno,      "challenge_uno"       },
    { ClientAction::kChallengePlusFour, "challenge_plus_four" },
    { ClientAction::kChooseColor,       "choose_color"        },
    { ClientAction::kAckGameOver,       "ack_game_over"       },
    { ClientAction::kStorePurchase,     "store_purchase"      },
    { ClientAction::kInventoryRequest,  "inventory_request"   },
    { ClientAction::kChatSend,          "chat_send"           },
};

inline const std::unordered_map<ServerAction, std::string> kServerActionStr {
    { ServerAction::kSyncData,          "sync_data"           },
    { ServerAction::kError,             "error"               },
    { ServerAction::kLobbyList,         "lobby_list"          },
    { ServerAction::kLobbyUpdated,      "lobby_updated"       },
    { ServerAction::kLobbyJoined,       "lobby_joined"        },
    { ServerAction::kLobbyLeft,         "lobby_left"          },
    { ServerAction::kInviteCreated,     "invite_created"      },
    { ServerAction::kPlayerJoined,      "player_joined"       },
    { ServerAction::kPlayerLeft,        "player_left"         },
    { ServerAction::kPlayerReady,       "player_ready"        },
    { ServerAction::kGameStarting,      "game_starting"       },
    { ServerAction::kRulesetSync,       "ruleset_sync"        },
    { ServerAction::kStateUpdate,       "state_update"        },
    { ServerAction::kCardRejected,      "card_rejected"       },
    { ServerAction::kCardDrawn,         "card_drawn"          },
    { ServerAction::kTurnChanged,       "turn_changed"        },
    { ServerAction::kColorChanged,      "color_changed"       },
    { ServerAction::kUnoCallSuccess,    "uno_call_success"    },
    { ServerAction::kUnoCallFailed,     "uno_call_failed"     },
    { ServerAction::kUnoChallenged,     "uno_challenged"      },
    { ServerAction::kCardPlayed,        "card_played"         },
    { ServerAction::kPlayerDrewCard,    "player_drew_card"    },
    { ServerAction::kSkipApplied,       "skip_applied"        },
    { ServerAction::kReverseApplied,    "reverse_applied"     },
    { ServerAction::kDrawStackApplied,  "draw_stack_applied"  },
    { ServerAction::kGameOver,          "game_over"           },
    { ServerAction::kRoundOver,         "round_over"          },
    { ServerAction::kInventorySync,     "inventory_sync"      },
    { ServerAction::kPurchaseConfirmed, "purchase_confirmed"  },
    { ServerAction::kPurchaseFailed,    "purchase_failed"     },
    { ServerAction::kChatMessage,       "chat_message"        },
};

// Reverse lookup — used in ActionRouter registration
inline const std::unordered_map<std::string, ClientAction> kStrClientAction {
    { "lobby_list",          ClientAction::kLobbyList         },
    { "lobby_create",        ClientAction::kLobbyCreate       },
    { "lobby_join",          ClientAction::kLobbyJoin         },
    { "lobby_leave",         ClientAction::kLobbyLeave        },
    { "lobby_invite",        ClientAction::kLobbyInvite       },
    { "lobby_set_ruleset",   ClientAction::kLobbySetRuleset   },
    { "lobby_set_skin",      ClientAction::kLobbySetSkin      },
    { "ready",               ClientAction::kReady             },
    { "play_card",           ClientAction::kPlayCard          },
    { "draw_card",           ClientAction::kDrawCard          },
    { "call_uno",            ClientAction::kCallUno           },
    { "challenge_uno",       ClientAction::kChallengeUno      },
    { "challenge_plus_four", ClientAction::kChallengePlusFour },
    { "choose_color",        ClientAction::kChooseColor       },
    { "ack_game_over",       ClientAction::kAckGameOver       },
    { "store_purchase",      ClientAction::kStorePurchase     },
    { "inventory_request",   ClientAction::kInventoryRequest  },
    { "chat_send",           ClientAction::kChatSend          },
};

// Convenience — build a server response JSON with the correct action string
inline nlohmann::json MakeResponse(ServerAction action) {
    return nlohmann::json({ {"action", kServerActionStr.at(action)} });
}
