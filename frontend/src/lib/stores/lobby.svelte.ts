/**
 * @file lobby.svelte.ts
 * @brief Reactive global store for managing lobbies and saved matches.
 * Handles creation, joining, settings updates and the public list.
 */

import { storeNavigation } from "./navigation.svelte";
import { storeToast } from "./toast.svelte";
import { ClientAction, ServerAction, ws } from "./ws.svelte";

/**
 * @enum BotTakeoverMode
 * @brief Behaviour mode when a bot replaces or is replaced by a player.
 */
export enum BotTakeoverMode {
    /** The bot plays its move instantly as soon as its turn arrives. */
    PlayInstantly,
    /** The bot waits up to 5 seconds for its turn, or until the end of the turn for other AFK players. */
    WaitUntilTurnEnd
}

/**
 * @interface RuleDefinition
 * @brief Describes a single game rule as provided by the server.
 */
export interface RuleDefinition {
    /** Unique identifier used in active_mods (e.g. "seven_zero"). */
    id: string;
    /** Short human-readable name for UI display. */
    label: string;
    /** One-sentence explanation of the rule's effect. */
    description: string;
}

/**
 * @interface SavedMatch
 * @brief Basic data of a match saved on the server.
 */
export interface SavedMatch {
    /** Unique identifier of the save in the database. */
    match_id: string;
    /** Date and time of the save. */
    saved_at: string;
    /** List of the usernames of the players present in that match. */
    players: string[];
}

/**
 * @interface LobbySettings
 * @brief Represents the configuration and rules chosen for the lobby.
 * Defines the match parameters and the composition of the initial deck.
 */
export interface LobbySettings {
    /** If true, the lobby will appear in the public list of matches. */
    is_public: boolean;
    /** Array of special rules (Mods) enabled for this match. */
    active_mods: string[];
    /** Time limit in milliseconds allowed to complete a turn (e.g. 15000). */
    turn_time_limit_ms: number;

    /** If true, the match state will be saved to the database on every move. */
    save_state: boolean;
    /** If true, a player's voluntary quit will permanently delete the save. */
    quit_deletes_match: boolean;

    /** Number of cards dealt to each player at the start of the match. */
    starting_cards: number;

    /** Number of bots to add automatically when the match starts. */
    bot_count: number;
    /** Bot behaviour (e.g. play immediately or wait for the timer). */
    bot_mode: BotTakeoverMode;
    /** If true, new human players can join an ongoing match by replacing a bot. */
    allow_bot_takeover: boolean;
    /** If true, a human player who quits is replaced by a bot instead of ending the game. */
    allow_bot_replacement: boolean;

    /** Copies per colour of the number 0 in the deck. */
    count_zeros: number;
    /** Copies per colour of the numbers 1 to 9 in the deck. */
    count_numbered: number;
    /** Copies per colour of the "Skip" card. */
    count_skips: number;
    /** Copies per colour of the "Reverse" card. */
    count_reverses: number;
    /** Copies per colour of the "Draw Two (+2)" card. */
    count_draw_two: number;
    /** Total number of Wild (Colour Change) cards in the deck. */
    count_wild: number;
    /** Total number of Wild Draw Four (+4) cards in the deck. */
    count_wild_draw_four: number;
}

/**
 * @interface LobbyMember
 * @brief Represents a single connected player inside a lobby.
 */
export interface LobbyMember {
    /** The display name of the player. */
    username: string;
    /** Indicates whether the player is currently connected via WebSocket or is reconnecting. */
    is_connected: boolean;
    /** True if this player is the owner/host of the lobby and can start the match. */
    is_host: boolean;
    /** True if this "member" is actually a bot account managed by the server. */
    is_bot: boolean;
}

/**
 * @interface Lobby
 * @brief Represents a complete game lobby with all its state data.
 */
export interface Lobby {
    /** The unique 6-character alphanumeric code used to join. */
    invite_code: string;
    /** The username of the creator (Host) of the lobby. */
    host: string;
    /** The custom name of the lobby shown in the public list. */
    name: string;
    /** The current number of members in the lobby (maximum 4). */
    member_count: number;
    /** Detailed list of the players currently in the lobby. */
    members: LobbyMember[];
    /** The settings configured for this game room. */
    settings: LobbySettings;
}

/**
 * @interface ListedLobby
 * @brief Minimized data sent by the server to render the list of public lobbies.
 * Saves bandwidth by not sending the full member list and the detailed rules.
 */
export interface ListedLobby {
    /** The custom name of the lobby. */
    name: string;
    /** Total number of human players currently connected. */
    member_count: number;
    /** Total number of bots present in this lobby. */
    bot_count: number;
    /** The invite code allowing the client to join with one click. */
    invite_code: string;
}

/**
 * @class StoreLobby
 * @brief Singleton store that manages all lobby-related state and the associated WebSocket interactions.
 * @tag FRONT-LOBBY-001
 */
class StoreLobby {
    /** Saved matches compatible with the current player list. Null if not in a lobby. */
    savedMatches = $state<SavedMatch[] | null>(null);
    /** The lobby the user is currently in. Null if not in a lobby. */
    current = $state<Lobby | null>(null);
    /** Rule definitions synced from the server on lobby join. */
    availableRules = $state<RuleDefinition[]>([]);

    /** The list of public lobbies available to join. */
    available = $state<ListedLobby[]>([]);

    /** True while fetching the list of public lobbies from the server. */
    isLoadingList = $state(false);

    /** True while a lobby creation or join request is in progress. */
    isLoadingJoin = $state(false);

    /** True while the list of saved matches is being fetched. */
    isLoadingSavedMatchList = $state(false);

    /**
     * @brief Derived property to quickly check whether the user is in a lobby.
     * @returns True if the user is in a lobby, false otherwise.
     */
    get isInLobby(): boolean {
        return this.current !== null;
    }

    /**
     * @brief Initializes the store and binds the basic connection events.
     * @tag FRONT-LOBBY-MTH-001
     */
    constructor() {
        ws.onOpen(async () => {
            this.#registerListeners();
            await this.#tryRejoin();
        });
    }

    /**
     * @brief Promotes a user to the host role of the lobby.
     * @param username The name of the player to promote.
     * @tag FRONT-LOBBY-MTH-002
     */
    async promote(username: string): Promise<void> {
        await ws.connect();
        const response = await ws.emitAndWait(ClientAction.LobbyPromote, { username });

        if (!response.ok) {
            storeToast.error(response.reason);
        } else {
            storeToast.success(`Promoted ${username}!`);
        }
    }

    /**
     * @brief Kicks a player from the lobby.
     * @param username The name of the player to kick.
     * @tag FRONT-LOBBY-MTH-003
     */
    async kick(username: string): Promise<void> {
        await ws.connect();
        const response = await ws.emitAndWait(ClientAction.LobbyKick, { username });

        if (!response.ok) {
            storeToast.error(response.reason)
        } else {
            storeToast.success(`Kicked ${username}!`)
        }
    }

    /**
     * @brief Updates the lobby settings (including name and visibility).
     * @param settings The settings fields to modify.
     * @tag FRONT-LOBBY-MTH-004
     */
    async updateSettings(
        settings: Partial<LobbySettings> & Partial<{ name: string }>
    ): Promise<void> {
        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyUpdateSettings, settings);
            if (!response.ok) {
                storeToast.error(response.reason);
                return;
            }

            storeToast.success("Settings updated!");
        } catch (error) {
            storeToast.error("Failed to update lobby settings.");
        }
    }

    /**
     * @brief Creates a new game lobby on the server.
     * Automatically connects to the WebSocket and emits the creation payload.
     * @param data The configuration for the new lobby.
     * @returns A Promise that resolves when the network request completes.
     * @tag FRONT-LOBBY-MTH-005
     */
    async create(data: { is_public: boolean; name: string }): Promise<void> {
        this.isLoadingJoin = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyCreate, data);

            if (!response.ok) {
                storeToast.error(response.reason);
            }
        } finally {
            this.isLoadingJoin = false;
        }
    }

    /**
     * @brief Joins an existing lobby using a 6-character invite code.
     * @param code The unique 6-character identifier of the lobby.
     * @returns A Promise that resolves when the join attempt completes.
     * @tag FRONT-LOBBY-MTH-006
     */
    async join(code: string): Promise<void> {
        if (!code) {
            storeToast.error("Enter an invite code.");
            return;
        }

        this.isLoadingJoin = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyJoin, { code });

            if (!response.ok) {
                storeToast.error(response.reason);
            }
        } finally {
            this.isLoadingJoin = false;
        }
    }

    /**
     * @brief Fetches the updated list of available public lobbies from the server.
     * Updates the `available` state array.
     * @returns A Promise that resolves when the list has been updated.
     * @tag FRONT-LOBBY-MTH-007
     */
    async fetchList(): Promise<void> {
        this.isLoadingList = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyList);

            if (response.ok) {
                this.available = response.get<ListedLobby[]>("lobbies") ?? [];
            }
        } finally {
            this.isLoadingList = false;
        }
    }

    /**
     * @brief Emits a request to leave the current lobby.
     * @tag FRONT-LOBBY-MTH-008
     */
    leave(): void {
        ws.emit(ClientAction.LobbyLeave);
    }

    /**
     * @brief Subscribes to all WebSocket events sent by the server related to lobbies.
     * Handles events such as joining, receiving updates or being evicted.
     * @tag FRONT-LOBBY-PRIV-001
     */
    #registerListeners(): void {
        ws.on(ServerAction.LobbyJoined, async (data) => {
            const lobby = data.lobby as Lobby;
            this.current = lobby;
            this.availableRules = (data.available_rules as RuleDefinition[]) ?? [];

            localStorage.setItem("lobby_code", lobby.invite_code);
            storeNavigation.goto("lobby");

            await this.#fetchSavedMatches();
        });

        ws.on(ServerAction.LobbyUpdated, async (data) => {
            const updatedLobby = data.lobby as Lobby;
            if (!updatedLobby) return;

            if (this.current) {
                this.current = updatedLobby;
                console.log(this.current);
            }

            const idx = this.available.findIndex((l) => l.invite_code === updatedLobby.invite_code);
            if (idx !== -1) {
                this.available[idx].member_count = updatedLobby.member_count;
                this.available[idx].bot_count = updatedLobby.settings.bot_count;
                this.available[idx].name = updatedLobby.name;
            }

            await this.#fetchSavedMatches();
        });

        const handleDisconnection = () => {
            this.#reset();
            storeNavigation.goto("lobbies");
        };

        ws.on(ServerAction.LobbyLeft, handleDisconnection);
        ws.on(ServerAction.LobbyEvicted, handleDisconnection);
    }

    /**
     * @brief Fetches from the server the previous saves compatible with the players in the lobby.
     * @tag FRONT-LOBBY-PRIV-002
     */
    async #fetchSavedMatches(): Promise<void> {
        if (!this.current) return;

        this.isLoadingSavedMatchList = true;
        try {
            const result = await ws.emitAndWait(ClientAction.LobbyListSavedMatches);
            if (result.ok) {
                this.savedMatches = result.getOr<SavedMatch[]>("saved_matches", []);
            } else {
                storeToast.error("Saved games list error");
            }
        } catch (err) {
            console.error("Failed to parse saved rooms details:", err);
        } finally {
            this.isLoadingSavedMatchList = false;
        }
    }

    /**
     * @brief Silently attempts to reconnect to a lobby using a stored session code.
     * Reads `lobby_code` from localStorage. Triggered automatically after a reload or disconnection.
     * @returns A Promise that resolves when the reconnection flow completes.
     * @tag FRONT-LOBBY-PRIV-003
     */
    async #tryRejoin(): Promise<void> {
        const code = localStorage.getItem("lobby_code");
        if (!code) return;

        try {
            const unsubscribeGameRejoin = ws.on(ServerAction.GameStateUpdated, (_data) => {
                storeNavigation.goto("game");

                unsubscribeGameRejoin();
            });

            const response = await ws.emitAndWait(ClientAction.LobbyRejoin, { code });

            if (!response.ok) {
                this.#reset();
                if (response.action !== ServerAction.LobbyEvicted) {
                    storeToast.error(`Could not rejoin lobby: ${response.reason}`);
                }
                return;
            }

            const lobby = response.get<Lobby>("lobby");
            if (!lobby) throw new Error("Invalid lobby data received.");

            this.current = lobby;
            this.availableRules = response.getOr<RuleDefinition[]>("available_rules", []);
            storeNavigation.goto("lobby");

            await this.#fetchSavedMatches();
        } catch (error) {
            this.#reset();
        }
    }

    /**
     * @brief Clears the active lobby state and local storage.
     * @tag FRONT-LOBBY-PRIV-004
     */
    #reset(): void {
        this.current = null;
        this.availableRules = [];
        localStorage.removeItem("lobby_code");
    }
}

export const storeLobby = new StoreLobby();
