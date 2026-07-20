#pragma once
#include <common/ws.hpp>
#include <App.h>
#include <action_router.hpp>
#include <functional>
#include <random>
#include <websocket_context.hpp>
#include <webserver.hpp>

/**
 * @file lobby.hpp
 * @brief Defines the base data structures for representing the lobbies.
 * * Decouples the pure data state (settings, members, identifiers)
 * from the management logic that resides in the LobbyController.
 */

namespace match {
    class MatchInstance;
}

/**
 * @enum BotTakeoverMode
 * @brief Behaviour mode when a bot takes over or is replaced.
 * @tag CMN-LOBBY-ENUM-001
 */
enum class BotTakeoverMode {
    kPlayInstantly,     /**< The bot plays instantly as soon as it is its turn. */
    kWaitUntilTurnEnd   /**< The bot waits for the turn timer to expire before playing. */
};

NLOHMANN_JSON_SERIALIZE_ENUM(BotTakeoverMode, {
    {BotTakeoverMode::kPlayInstantly, 0},
    {BotTakeoverMode::kWaitUntilTurnEnd, 1}
})

/**
 * @struct LobbySettings
 * @brief Contains the detailed configuration and rules of a specific lobby.
 * These settings are JSON-serializable to be transmitted to the clients.
 * @tag CMN-LOBBY-STR-001
 */
struct LobbySettings {
    /**< Indicates whether the lobby appears in the public list. */
    bool is_public = false;
    int turn_time_limit_ms = 15'000;        /**< Time limit for a turn (in milliseconds). */
    std::vector<std::string> active_mods;   /**< Optional mods or rules active for the match. */

    bool save_state = false;                /**< If true, the match state is saved to the DB. */
    /**< If true, a player quitting destroys the saved match. */
    bool quit_deletes_match = false;

    int bot_count = 0;                      /**< Number of initial bots in the lobby. */
    BotTakeoverMode bot_mode = BotTakeoverMode::kWaitUntilTurnEnd; /**< Play mode of the bots. */

    /**< If true, a new human player can replace a bot mid-match. */
    bool allow_bot_takeover = true;
    /**< If true, a player who leaves is replaced by a bot instead of stopping the game. */
    bool allow_bot_replacement = true;

    int starting_cards       = 7;           /**< Number of cards drawn at the start. */
    int count_zeros          = 1;           /**< Copies per colour of the number 0. */
    int count_numbered       = 2;           /**< Copies per colour of the numbers 1-9. */
    int count_skips          = 2;           /**< Copies per colour of the "Skip" card. */
    int count_reverses       = 2;           /**< Copies per colour of the "Reverse" card. */
    int count_draw_two       = 2;           /**< Copies per colour of the "Draw Two" card. */
    int count_wild           = 4;           /**< Total number of Wild cards. */
    int count_wild_draw_four = 4;           /**< Total number of Wild Draw Four cards. */

    /**
     * @brief Clamps numeric fields into contract bounds and strips unknown or
     * duplicate entries from active_mods in place.
     * @tag CMN-LOBBY-MTH-001
     */
    void Sanitize();
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LobbySettings,
    turn_time_limit_ms, active_mods, bot_count, bot_mode, starting_cards,
    allow_bot_takeover, allow_bot_replacement, quit_deletes_match, save_state, is_public
)

/**
 * @struct LobbyMember
 * @brief Represents a single participant within a lobby.
 * @tag CMN-LOBBY-STR-002
 */
struct LobbyMember {
    std::string     username;      /**< Username of the member. */
    /**< Pointer to the uWS socket. nullptr if the user is temporarily disconnected. */
    AppWebSocket* socket;
    /**< Connection state (true = online, false = offline in grace period). */
    bool            is_connected;
    bool            is_bot;        /**< True if this "member" is controlled by the AI. */
    /**< Stable per-member identity slot, assigned once via Lobby::NextFreeSeat() at
     * join time (lowest currently-unused index). Never recomputed from this
     * member's position in the `members` vector, so it survives other members
     * leaving/joining. Drives the player's color/seat identity on the client. */
    int             seat_index;

    /**< Timestamp of the last disconnection (for the eviction timer). */
    std::chrono::steady_clock::time_point disconnected_at{};

    LobbyMember(std::string u, AppWebSocket* s, bool c, bool b, int seat = -1)
        : username(std::move(u)), socket(s), is_connected(c), is_bot(b), seat_index(seat)  {}
};

/**
 * @enum MemberRemovalOutcome
 * @brief Describes how an in-progress match, if any, was affected by a member removal.
 * @tag CMN-LOBBY-ENUM-002
 */
enum class MemberRemovalOutcome {
    kMatchUnaffected,        /**< No match in progress, or match unaffected by this removal. */
    kMatchAborted,           /**< quit_deletes_match path: match was torn down. */
    kPlayerReplacedByBot,    /**< allow_bot_replacement path. */
    kPlayerDroppedFromEngine /**< Neither of the above: RemovePlayerMidGame path. */
};

/**
 * @struct MemberRemovalResult
 * @brief Outcome of a Lobby::RemoveMember call, for the caller to react to.
 * @tag CMN-LOBBY-STR-003
 */
struct MemberRemovalResult {
    bool found = false;          /**< False if the member wasn't in lobby.members at all. */
    /**< True if the member had a live socket at time of removal. */
    bool was_connected = false;
    /**< The member's socket pointer at time of removal, or nullptr. */
    AppWebSocket* socket = nullptr;
    /**< How an in-progress match, if any, was affected. */
    MemberRemovalOutcome match_outcome = MemberRemovalOutcome::kMatchUnaffected;
    /**< The username removed/replaced (meaningful only if a match was affected). */
    std::string old_username;
    /**< Only set when match_outcome == kPlayerReplacedByBot. */
    std::string new_bot_name;
    /**< Meaningful only for kPlayerReplacedByBot or kPlayerDroppedFromEngine. */
    bool was_their_turn = false;
};

/**
 * @enum JoinOutcome
 * @brief Describes which path Lobby::AddOrHijack took to admit a new member.
 * @tag CMN-LOBBY-ENUM-003
 */
enum class JoinOutcome {
    kHijackedBot,      /**< An existing bot member was replaced by the joiner. */
    kJoinedEmptySlot,  /**< The joiner filled a free member slot. */
    kLobbyFull         /**< Neither path was available; the lobby is at capacity. */
};

/**
 * @struct JoinResult
 * @brief Outcome of a Lobby::AddOrHijack call, for the caller to react to.
 * @tag CMN-LOBBY-STR-004
 */
struct JoinResult {
    JoinOutcome outcome;
    /**< Only set when outcome == kHijackedBot. */
    std::string old_bot_name;
};

/**
 * @struct Lobby
 * @brief Aggregates the entire structural state of a game room.
 * @tag CMN-LOBBY-STR-005
 */
struct Lobby {
    uint32_t                 id;            /**< Unique internal numeric identifier of the lobby. */
    std::string              invite_code;   /**< 6-character alphanumeric invite code. */
    /**< Username of the current creator/host of the lobby. */
    std::string              host;
    std::vector<LobbyMember> members;       /**< List of participants (Humans and Bots). */
    /**< Textual name of the lobby (for display in the list). */
    std::string              name;
    LobbySettings            settings;      /**< Current settings of the match in this lobby. */

    /** * @brief Instance of the game engine.
     * If nullptr, the lobby is in the waiting phase. If populated, a match is currently in progress.
     */
    std::unique_ptr<match::MatchInstance> match;

    /**
     * @brief Re-evaluates settings.bot_count, adding or purging bot members
     * until the bot count matches (clamped to the lobby capacity). No-op
     * while a match is in progress.
     * @param rng Shared RNG used to pick unique bot display names.
     * @tag CMN-LOBBY-MTH-002
     */
    void SyncBots(std::mt19937& rng);

    /**
     * @brief Picks a random bot display name not already taken in the lobby.
     * Falls back to a sequential "Bot_N" name if every reserved name is taken.
     * @param rng Shared RNG used for the random pick.
     * @return std::string An available bot display name.
     * @tag CMN-LOBBY-MTH-003
     */
    std::string PickBotName(std::mt19937& rng) const;

    /**
     * @brief Returns the lowest seat index in [0, contract::kMaxLobbyMembers) not
     * currently held by any member, for first-come-first-served, gap-filling
     * seat assignment (a freed seat is reused before any higher index is handed
     * out). Pure lookup: does not mutate `members`.
     * @return int The lowest free seat index.
     * @tag CMN-LOBBY-MTH-010
     */
    int NextFreeSeat() const;

    /**
     * @brief Generates a cryptographically random alphanumeric token, used both
     * for lobby invite codes and match identifiers.
     * @return std::string Random alphanumeric token.
     * @tag CMN-LOBBY-MTH-004
     */
    static std::string GenerateInviteCode();

    /**
     * @brief Removes a member from the lobby, applying departure policy: if a
     * match is in progress, either flags it for abort, replaces the departing
     * human with a bot, or drops them from the engine directly, per
     * settings.quit_deletes_match / settings.allow_bot_replacement. Always
     * erases the member from `members`. On the kMatchAborted outcome, `match`
     * is deliberately left intact so the caller can persist its state before
     * tearing it down, the caller must reset `match` itself after saving.
     * @param username Username of the member to remove.
     * @param rng Shared RNG, forwarded to PickBotName for the bot-replacement path.
     * @return MemberRemovalResult describing what happened, for the caller to
     * react to (broadcasting, persistence, callbacks), this method does not
     * touch sockets, the database, or controller-level callback lists.
     * @tag CMN-LOBBY-MTH-005
     */
    MemberRemovalResult RemoveMember(const std::string& username, std::mt19937& rng);

    /**
     * @brief Promotes the first connected non-bot member to host. No-op if no
     * such member exists.
     * @return true if a new host was promoted, false if no eligible member was found.
     * @tag CMN-LOBBY-MTH-006
     */
    bool PromoteNextHost();

    /**
     * @brief Adds a user to the lobby: hijacks a bot slot if
     * allow_bot_takeover is set and a bot member exists, otherwise fills an
     * empty slot if under capacity, otherwise reports the lobby as full.
     * @param username Username of the joining player.
     * @param socket Their websocket connection.
     * @return JoinResult describing which path was taken.
     * @tag CMN-LOBBY-MTH-007
     */
    JoinResult AddOrHijack(const std::string& username, AppWebSocket* socket);

    /**
     * @brief Builds a fully-populated, sanitized Lobby with a unique invite code,
     * retrying code generation up to 10 times on collision.
     * @param id Numeric lobby id, already allocated by the caller.
     * @param host Username of the creating host; also added as the first member.
     * @param host_socket Websocket of the creating host, assigned to their member entry.
     * @param is_public Whether the lobby appears in the public list.
     * @param name Display name of the lobby.
     * @param turn_time_limit_ms Initial turn time limit, before Sanitize() clamps it.
     * @param starting_cards Initial starting card count, before Sanitize() clamps it.
     * @param code_taken Predicate returning true if a candidate invite code is
     * already registered elsewhere (queries the caller's own registry, e.g.
     * LobbyController::code_to_id_).
     * @return The populated Lobby, ready to be inserted into the caller's registry.
     * @throws std::runtime_error if no unique code could be generated in 10 attempts,
     * matching the previous LobbyController::HandleCreate behavior exactly.
     * @tag CMN-LOBBY-MTH-008
     */
    static Lobby Create(uint32_t id, const std::string& host, AppWebSocket* host_socket,
                         bool is_public, const std::string& name, int turn_time_limit_ms,
                         int starting_cards,
                         const std::function<bool(const std::string&)>& code_taken);

    /**
     * @brief Scans for disconnected, non-bot members whose grace period has
     * expired as of `now`. Read-only: does not remove members or touch match
     * state, leaving eviction and its side effects to the caller.
     * @param now Reference timestamp to measure elapsed disconnect time against.
     * @param grace_ms Grace period in milliseconds before a disconnected
     * member is considered expired.
     * @return std::vector<std::string> Usernames of expired members.
     * @tag CMN-LOBBY-MTH-009
     */
    std::vector<std::string> CollectExpiredDisconnects(
        std::chrono::steady_clock::time_point now, int64_t grace_ms) const;
};
