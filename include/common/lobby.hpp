#pragma once
#include <common/ws.hpp>
#include <App.h>
#include <action_router.hpp>
#include <websocket_context.hpp>
#include <webserver.hpp>

namespace game {
    class MatchInstance;
}

enum class BotTakeoverMode {
    kPlayInstantly,
    kWaitUntilTurnEnd
};

NLOHMANN_JSON_SERIALIZE_ENUM(BotTakeoverMode, {
    {BotTakeoverMode::kPlayInstantly, 0},
    {BotTakeoverMode::kWaitUntilTurnEnd, 1}
})

struct LobbySettings {
    bool is_public = false;
    int turn_time_limit_ms = 15'000;
    std::vector<std::string> active_mods; 
    
    bool save_state = false;
    bool quit_deletes_match = false;

    int bot_count = 0;
    BotTakeoverMode bot_mode = BotTakeoverMode::kWaitUntilTurnEnd;
    // New players will join by taking over a bot
    bool allow_bot_takeover = true;
    // Quitting players will be replaced by a bot
    bool allow_bot_replacement = true;

    int starting_cards       = 7;                                    
    int count_zeros          = 1;
    int count_numbered       = 2;
    int count_skips          = 2;
    int count_reverses       = 2;
    int count_draw_two       = 2;
    int count_wild           = 4;
    int count_wild_draw_four = 4;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LobbySettings, 
    turn_time_limit_ms, active_mods, bot_count, bot_mode, starting_cards,
    allow_bot_takeover, allow_bot_replacement, quit_deletes_match, save_state, is_public
)

struct LobbyMember {
    std::string     username;
    AppWebSocket*   socket;        // nullptr when disconnected
    bool            is_connected;
    bool            is_bot;
    
    std::chrono::steady_clock::time_point disconnected_at{};

    LobbyMember(std::string u, AppWebSocket* s, bool c, bool b) : username(std::move(u)), socket(s), is_connected(c), is_bot(b)  {}
};

struct Lobby {
    uint32_t                 id;
    std::string              invite_code;   // 6-char A-Z0-9, the join token
    std::string              host;          // username of creator
    std::vector<LobbyMember> members;
    std::string              name;
    LobbySettings            settings;

    // INFO: If null, we are in the lobby. If populated, a match is ongoing.
    std::unique_ptr<game::MatchInstance> match;
};
